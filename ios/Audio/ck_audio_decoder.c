// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A4.1: see ck_audio_decoder.h.

#include "ck_audio_decoder.h"

#include <opus/opus.h>

#include <stdlib.h>
#include <string.h>

bool ck_audio_decoder_init(CKAudioDecoder *decoder, ChiakiLog *log)
{
	memset(decoder, 0, sizeof(*decoder));
	decoder->log = log;
	// The size for stereo fits mono too, so a header never needs more.
	decoder->opus = malloc((size_t)opus_decoder_get_size(CK_AUDIO_DECODER_MAX_CHANNELS));
	if(!decoder->opus)
	{
		CHIAKI_LOGE(log, "Audio decoder: no memory for the Opus decoder");
		return false;
	}
	return true;
}

void ck_audio_decoder_fini(CKAudioDecoder *decoder)
{
	free(decoder->opus);
	decoder->opus = NULL;
	decoder->ready = false;
}

bool ck_audio_decoder_header(CKAudioDecoder *decoder, uint32_t channels, uint32_t rate, uint32_t frame_size)
{
	decoder->ready = false;
	bool rate_ok = rate == 8000 || rate == 12000 || rate == 16000 || rate == 24000 || rate == 48000;
	// Opus frames are 2.5, 5, 10, 20, 40 or 60 ms, and a packet holds up to 120 ms of them.
	bool frame_ok = rate_ok && frame_size && (frame_size * 400) % rate == 0
		&& frame_size * 1000 <= rate * 120;
	if(!rate_ok || !frame_ok || channels < 1 || channels > CK_AUDIO_DECODER_MAX_CHANNELS)
	{
		CHIAKI_LOGE(decoder->log, "Audio decoder: Opus does not decode %u channels at %u Hz in frames of %u samples",
			channels, rate, frame_size);
		return false;
	}
	int err = opus_decoder_init(decoder->opus, (opus_int32)rate, (int)channels);
	if(err != OPUS_OK)
	{
		CHIAKI_LOGE(decoder->log, "Audio decoder: opus_decoder_init failed: %s", opus_strerror(err));
		return false;
	}
	decoder->rate = rate;
	decoder->channels = channels;
	decoder->frame_size = frame_size;
	decoder->ready = true;
	return true;
}

/** One frame of filler from the decoder's state; silence if even that fails, so the timing holds. */
static uint32_t conceal(CKAudioDecoder *decoder, int16_t *pcm)
{
	int r = opus_decode(decoder->opus, NULL, 0, pcm, (int)decoder->frame_size, 0);
	if(r != (int)decoder->frame_size)
		memset(pcm, 0, (size_t)decoder->frame_size * decoder->channels * sizeof(int16_t));
	return decoder->frame_size;
}

uint32_t ck_audio_decoder_frame(CKAudioDecoder *decoder, const uint8_t *buf, size_t buf_size,
	int16_t *pcm, uint32_t max_frames)
{
	if(!decoder->ready)
		return 0;
	if(!buf || !buf_size)
	{
		if(max_frames < decoder->frame_size)
			return 0;
		decoder->stats.concealed++;
		return conceal(decoder, pcm);
	}

	int frames = opus_decode(decoder->opus, buf, (opus_int32)buf_size, pcm, (int)max_frames, 0);
	if(frames > 0)
	{
		decoder->stats.decoded++;
		return (uint32_t)frames;
	}
	if(frames == OPUS_BUFFER_TOO_SMALL || max_frames < decoder->frame_size)
	{
		CHIAKI_LOGE(decoder->log, "Audio decoder: a packet of %zu bytes does not fit in %u frames", buf_size, max_frames);
		return 0;
	}
	decoder->stats.errors++;
	// The 1st, 2nd, 4th, 8th... error: enough to see it, without a line per frame.
	if((decoder->stats.errors & (decoder->stats.errors - 1)) == 0)
		CHIAKI_LOGW(decoder->log, "Audio decoder: concealed a packet of %zu bytes libopus refused (%s), %u so far",
			buf_size, opus_strerror(frames), decoder->stats.errors);
	return conceal(decoder, pcm);
}
