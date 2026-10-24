// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "ck_video_core.h"

#include "ck_h264.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

struct ck_video_core_t
{
	pthread_mutex_t mutex;
	CKVideoDecoder decoder;
	/** Where each frame is written as AVCC: made once, and grown only by a larger frame. */
	uint8_t *avcc;
	size_t avcc_capacity;
	CKH264ParamStore params;
	/** The decoder was made from params and still works. */
	bool configured;
	/**
	 * The decoder holds no picture to refer to: it is not made yet, it broke,
	 * or its last keyframe failed. Only a keyframe can start it again.
	 */
	bool need_keyframe;
	/** When need_keyframe was set, and the frames refused since. */
	uint64_t wait_start_us;
	uint32_t wait_refused;
	/** CKVideoSimulation bits for the next frame. */
	unsigned simulate;
	CKVideoCoreStats stats;
};

CKVideoCore *ck_video_core_new(const CKVideoDecoder *decoder)
{
	CKVideoCore *core = calloc(1, sizeof(*core));
	if(!core)
		return NULL;
	core->avcc_capacity = ck_h264_avcc_max_size(CK_VIDEO_CORE_INITIAL_FRAME_SIZE);
	core->avcc = malloc(core->avcc_capacity);
	if(!core->avcc || pthread_mutex_init(&core->mutex, NULL) != 0)
	{
		free(core->avcc);
		free(core);
		return NULL;
	}
	core->decoder = *decoder;
	ck_h264_param_store_reset(&core->params);
	return core;
}

static void wait_for_keyframe(CKVideoCore *core)
{
	if(core->need_keyframe)
		return;
	core->need_keyframe = true;
	core->wait_start_us = core->decoder.now_us(core->decoder.user);
	core->wait_refused = 0;
}

static void drop_decoder(CKVideoCore *core)
{
	core->decoder.drop(core->decoder.user);
	core->configured = false;
}

void ck_video_core_free(CKVideoCore *core)
{
	if(!core)
		return;
	drop_decoder(core);
	pthread_mutex_destroy(&core->mutex);
	free(core->avcc);
	free(core);
}

// Converts buf into core->avcc, growing it when the frame does not fit.
static CKH264Result convert(CKVideoCore *core, const uint8_t *buf, size_t buf_size, CKH264FrameInfo *info)
{
	CKH264Result r = ck_h264_annexb_to_avcc(buf, buf_size, CK_H264_DROP_FOR_VIDEOTOOLBOX,
		core->avcc, core->avcc_capacity, info);
	if(r != CK_H264_ERR_OUT_TOO_SMALL)
		return r;
	// A frame larger than any before: room for this one and some to spare.
	size_t capacity = ck_h264_avcc_max_size(buf_size);
	uint8_t *bigger = realloc(core->avcc, capacity);
	if(!bigger)
		return r;
	core->avcc = bigger;
	core->avcc_capacity = capacity;
	return ck_h264_annexb_to_avcc(buf, buf_size, CK_H264_DROP_FOR_VIDEOTOOLBOX, core->avcc, core->avcc_capacity, info);
}

static void count_decode(CKVideoCore *core, uint64_t start_us)
{
	uint64_t us = core->decoder.now_us(core->decoder.user) - start_us;
	core->stats.decode_us += us;
	if(us > core->stats.decode_us_max)
		core->stats.decode_us_max = us > UINT32_MAX ? UINT32_MAX : (uint32_t)us;
}

// A frame the decoder can start from: an IDR, or one made only of I (or
// SI) slices, which the PS5 may send when the core asks for a keyframe.
static bool is_keyframe(const uint8_t *avcc, size_t size, uint32_t nal_types)
{
	if(nal_types & CK_H264_NAL_BIT(CK_H264_NAL_IDR))
		return true;
	bool slices = false;
	size_t pos = 0;
	while(size - pos > CK_H264_AVCC_LENGTH_SIZE)
	{
		size_t len = ((size_t)avcc[pos] << 24) | ((size_t)avcc[pos + 1] << 16) | ((size_t)avcc[pos + 2] << 8)
			| avcc[pos + 3];
		pos += CK_H264_AVCC_LENGTH_SIZE;
		if(!len || len > size - pos)
			break;
		if(CK_H264_NAL_TYPE(avcc[pos]) == CK_H264_NAL_SLICE)
		{
			int type = ck_h264_slice_type(avcc + pos, len);
			if(type != CK_H264_SLICE_I && type != CK_H264_SLICE_SI)
				return false;
			slices = true;
		}
		pos += len;
	}
	return slices;
}

static void refuse_waiting(CKVideoCore *core)
{
	core->stats.frames_waiting++;
	core->wait_refused++;
}

static void keyframe_decoded(CKVideoCore *core)
{
	if(!core->need_keyframe)
		return;
	core->need_keyframe = false;
	if(!core->wait_refused)
		return;
	// The picture stood still from the failure until now.
	uint64_t us = core->decoder.now_us(core->decoder.user) - core->wait_start_us;
	core->stats.keyframe_waits++;
	if(us > core->stats.keyframe_wait_us_max)
		core->stats.keyframe_wait_us_max = us > UINT32_MAX ? UINT32_MAX : (uint32_t)us;
}

static bool sample_locked(CKVideoCore *core, const uint8_t *buf, size_t buf_size, int32_t frames_lost)
{
	if(frames_lost > 0)
		core->stats.frames_lost += (uint32_t)frames_lost;

	CKH264FrameInfo info;
	CKH264Result r = convert(core, buf, buf_size, &info);
	CKH264ParamUpdate update = r == CK_H264_OK ? ck_h264_param_store_update(&core->params, &info.param_sets)
		: CK_H264_PARAMS_UNCHANGED;
	if(r != CK_H264_OK || update == CK_H264_PARAMS_TOO_LARGE)
	{
		core->stats.frames_skipped++;
		return false;
	}
	if(update == CK_H264_PARAMS_CHANGED)
		core->configured = false;
	// Only the SPS and PPS, as after a profile switch: kept for the next frame.
	if(!info.nal_written)
		return true;

	// Until a keyframe comes, any other frame would refer to pictures the
	// decoder does not have and show garbage: it is lost for the core, which
	// then asks the console for a keyframe.
	bool keyframe = is_keyframe(core->avcc, info.avcc_size, info.nal_types);
	if(!core->configured)
	{
		if(!ck_h264_param_store_ready(&core->params))
		{
			core->stats.frames_skipped++;
			return false;
		}
		// A new decoder, or one with new parameters, starts at a keyframe,
		// and is made only then.
		wait_for_keyframe(core);
		if(!keyframe)
		{
			refuse_waiting(core);
			return false;
		}
		core->stats.configures++;
		core->configured = core->decoder.configure(core->decoder.user, core->params.sps, core->params.sps_size,
			core->params.pps, core->params.pps_size);
		if(!core->configured)
		{
			core->stats.configure_failures++;
			core->stats.frames_skipped++;
			return false;
		}
	}
	else if(core->need_keyframe && !keyframe)
	{
		refuse_waiting(core);
		return false;
	}

	CKVideoDecodeResult result;
	unsigned simulate = core->simulate;
	core->simulate = 0;
	if(simulate & CK_VIDEO_SIMULATE_BROKEN_DECODER)
		result = CK_VIDEO_DECODE_BROKEN;
	else if(simulate & CK_VIDEO_SIMULATE_LOST_FRAME)
		result = CK_VIDEO_DECODE_FAILED;
	else
	{
		uint64_t start_us = core->decoder.now_us(core->decoder.user);
		result = core->decoder.decode(core->decoder.user, core->avcc, info.avcc_size);
		count_decode(core, start_us);
	}
	switch(result)
	{
		case CK_VIDEO_DECODE_OK:
			core->stats.frames_decoded++;
			if(keyframe)
				keyframe_decoded(core);
			return true;
		case CK_VIDEO_DECODE_BROKEN:
			core->stats.decoder_broken++;
			drop_decoder(core);
			wait_for_keyframe(core);
			break;
		case CK_VIDEO_DECODE_FAILED:
		default:
			// A keyframe that fails leaves nothing to refer to. A P frame
			// that fails leaves the pictures before it, which the core points
			// the next frames at.
			if(keyframe)
				wait_for_keyframe(core);
			break;
	}
	core->stats.frames_failed++;
	return false;
}

bool ck_video_core_sample(CKVideoCore *core, const uint8_t *buf, size_t buf_size, int32_t frames_lost, bool frame_recovered)
{
	(void)frame_recovered;
	pthread_mutex_lock(&core->mutex);
	bool taken = sample_locked(core, buf, buf_size, frames_lost);
	pthread_mutex_unlock(&core->mutex);
	return taken;
}

void ck_video_core_reset(CKVideoCore *core)
{
	pthread_mutex_lock(&core->mutex);
	drop_decoder(core);
	ck_h264_param_store_reset(&core->params);
	core->need_keyframe = false;
	core->simulate = 0;
	pthread_mutex_unlock(&core->mutex);
}

void ck_video_core_simulate(CKVideoCore *core, CKVideoSimulation what)
{
	pthread_mutex_lock(&core->mutex);
	core->simulate |= (unsigned)what;
	pthread_mutex_unlock(&core->mutex);
}

void ck_video_core_take_stats(CKVideoCore *core, CKVideoCoreStats *stats)
{
	pthread_mutex_lock(&core->mutex);
	*stats = core->stats;
	memset(&core->stats, 0, sizeof(core->stats));
	pthread_mutex_unlock(&core->mutex);
}
