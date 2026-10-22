// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "ck_h264.h"

#include <string.h>

// Index of the first 00 00 01 at or after from, or size when there is none.
// Steps up to three bytes at a time, since most bytes are neither 0 nor 1.
static size_t find_start_code(const uint8_t *buf, size_t from, size_t size)
{
	size_t i = from;
	while(i + 2 < size)
	{
		if(buf[i + 2] > 1)
			i += 3;
		else if(buf[i + 1])
			i += 2;
		else if(!buf[i] && buf[i + 2] == 1)
			return i;
		else
			i++;
	}
	return size;
}

bool ck_h264_annexb_next_nal(const uint8_t *buf, size_t size, size_t *pos, const uint8_t **nal, size_t *nal_size)
{
	if(!buf || !pos || *pos >= size)
		return false;

	size_t start = find_start_code(buf, *pos, size);
	while(start < size)
	{
		size_t nal_start = start + 3;
		size_t next = find_start_code(buf, nal_start, size);
		// A NAL unit never ends in 0x00 (H.264 7.4.1), so trailing zeros are
		// trailing_zero_8bits or the first byte of a 4-byte start code.
		size_t nal_end = next;
		while(nal_end > nal_start && !buf[nal_end - 1])
			nal_end--;
		start = next;
		if(nal_end > nal_start)
		{
			*nal = buf + nal_start;
			*nal_size = nal_end - nal_start;
			*pos = next;
			return true;
		}
	}
	*pos = size;
	return false;
}

size_t ck_h264_avcc_max_size(size_t annexb_size)
{
	size_t extra = annexb_size / 4;
	if(annexb_size > SIZE_MAX - extra)
		return SIZE_MAX;
	return annexb_size + extra;
}

CKH264Result ck_h264_annexb_to_avcc(const uint8_t *in, size_t in_size, uint32_t drop_mask,
	uint8_t *out, size_t out_capacity, CKH264FrameInfo *info)
{
	CKH264FrameInfo fi;
	memset(&fi, 0, sizeof(fi));
	if(!out)
		out_capacity = 0;

	if(in_size > CK_H264_MAX_INPUT_SIZE)
	{
		if(info)
			*info = fi;
		return CK_H264_ERR_INPUT_TOO_LARGE;
	}

	// Below CK_H264_MAX_INPUT_SIZE neither a length nor the running total can
	// overflow, even with a 32-bit size_t.
	size_t needed = 0;
	size_t pos = 0;
	const uint8_t *nal;
	size_t nal_size;
	while(ck_h264_annexb_next_nal(in, in_size, &pos, &nal, &nal_size))
	{
		unsigned type = CK_H264_NAL_TYPE(nal[0]);
		fi.nal_count++;
		fi.nal_types |= CK_H264_NAL_BIT(type);
		if(type == CK_H264_NAL_SPS && !fi.param_sets.sps)
		{
			fi.param_sets.sps = nal;
			fi.param_sets.sps_size = nal_size;
		}
		else if(type == CK_H264_NAL_PPS && !fi.param_sets.pps)
		{
			fi.param_sets.pps = nal;
			fi.param_sets.pps_size = nal_size;
		}
		if(drop_mask & CK_H264_NAL_BIT(type))
			continue;

		size_t unit_size = CK_H264_AVCC_LENGTH_SIZE + nal_size;
		// Once a unit does not fit, keep counting so the caller learns the size needed.
		if(needed <= out_capacity && unit_size <= out_capacity - needed)
		{
			uint8_t *dst = out + needed;
			dst[0] = (uint8_t)(nal_size >> 24);
			dst[1] = (uint8_t)(nal_size >> 16);
			dst[2] = (uint8_t)(nal_size >> 8);
			dst[3] = (uint8_t)nal_size;
			memcpy(dst + CK_H264_AVCC_LENGTH_SIZE, nal, nal_size);
		}
		needed += unit_size;
		fi.nal_written++;
	}

	CKH264Result result = CK_H264_OK;
	if(!fi.nal_count)
		result = CK_H264_ERR_NO_NAL;
	else
	{
		fi.avcc_size = needed;
		if(needed > out_capacity)
			result = CK_H264_ERR_OUT_TOO_SMALL;
	}
	if(info)
		*info = fi;
	return result;
}

bool ck_h264_find_param_sets(const uint8_t *buf, size_t size, CKH264ParamSets *sets)
{
	CKH264FrameInfo fi;
	// Dropping every type writes nothing, so no output buffer is needed.
	ck_h264_annexb_to_avcc(buf, size, UINT32_MAX, NULL, 0, &fi);
	*sets = fi.param_sets;
	return sets->sps && sets->pps;
}

void ck_h264_param_store_reset(CKH264ParamStore *store)
{
	memset(store, 0, sizeof(*store));
}

static bool param_set_differs(const uint8_t *stored, size_t stored_size, const uint8_t *set, size_t set_size)
{
	return set && (set_size != stored_size || memcmp(stored, set, set_size) != 0);
}

CKH264ParamUpdate ck_h264_param_store_update(CKH264ParamStore *store, const CKH264ParamSets *sets)
{
	if((sets->sps && sets->sps_size > CK_H264_PARAM_SET_MAX_SIZE)
		|| (sets->pps && sets->pps_size > CK_H264_PARAM_SET_MAX_SIZE))
		return CK_H264_PARAMS_TOO_LARGE;

	CKH264ParamUpdate update = CK_H264_PARAMS_UNCHANGED;
	if(param_set_differs(store->sps, store->sps_size, sets->sps, sets->sps_size))
	{
		memcpy(store->sps, sets->sps, sets->sps_size);
		store->sps_size = sets->sps_size;
		update = CK_H264_PARAMS_CHANGED;
	}
	if(param_set_differs(store->pps, store->pps_size, sets->pps, sets->pps_size))
	{
		memcpy(store->pps, sets->pps, sets->pps_size);
		store->pps_size = sets->pps_size;
		update = CK_H264_PARAMS_CHANGED;
	}
	return update;
}

bool ck_h264_param_store_ready(const CKH264ParamStore *store)
{
	return store->sps_size > 0 && store->pps_size > 0;
}
