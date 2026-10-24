// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A2.8: Opus packets like the PS5's (stereo, 48 kHz, 10 ms frames) made with
// libopus from a sine, for recorder.c and recorder_tool.c.

#ifndef CK_TESTS_OPUS_SAMPLE_H
#define CK_TESTS_OPUS_SAMPLE_H

#include <opus/opus.h>

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#define OPUS_SAMPLE_RATE 48000
#define OPUS_SAMPLE_CHANNELS 2
#define OPUS_SAMPLE_FRAME 480
#define OPUS_SAMPLE_PACKET_MAX 1500

typedef struct opus_sample_t
{
	OpusEncoder *encoder;
	uint64_t position;
} OpusSample;

/** bitrate in bits per second, constant; 0 keeps libopus's default. Returns 0 on success. */
static inline int opus_sample_init(OpusSample *sample, int32_t bitrate)
{
	int err;
	sample->encoder = opus_encoder_create(OPUS_SAMPLE_RATE, OPUS_SAMPLE_CHANNELS, OPUS_APPLICATION_AUDIO, &err);
	sample->position = 0;
	if(err != OPUS_OK)
		return err;
	// A fixed bitrate, so a pure tone does not shrink the packets.
	if(bitrate)
	{
		opus_encoder_ctl(sample->encoder, OPUS_SET_BITRATE(bitrate));
		opus_encoder_ctl(sample->encoder, OPUS_SET_VBR(0));
	}
	return 0;
}

static inline void opus_sample_fini(OpusSample *sample)
{
	opus_encoder_destroy(sample->encoder);
}

/** The next 10 ms of a 440 Hz tone. Returns the packet size, or a negative libopus error. */
static inline int opus_sample_next(OpusSample *sample, uint8_t *packet, size_t capacity)
{
	opus_int16 pcm[OPUS_SAMPLE_FRAME * OPUS_SAMPLE_CHANNELS];
	for(int i = 0; i < OPUS_SAMPLE_FRAME; i++)
	{
		double t = (double)(sample->position + (uint64_t)i) / OPUS_SAMPLE_RATE;
		opus_int16 v = (opus_int16)(8000.0 * sin(2.0 * 3.14159265358979 * 440.0 * t));
		pcm[2 * i] = v;
		pcm[2 * i + 1] = v;
	}
	sample->position += OPUS_SAMPLE_FRAME;
	return opus_encode(sample->encoder, pcm, OPUS_SAMPLE_FRAME, packet, (opus_int32)capacity);
}

#endif // CK_TESTS_OPUS_SAMPLE_H
