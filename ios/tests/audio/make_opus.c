// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A4.1: makes a recording like the ones cksession -g makes from a PS5, for the
// tests that cannot count on a real one (check_decode.py).
//
//   make_opus PATH SECONDS LOST_EVERY
//
// Writes PATH.opus through ios/Bridge/ck_recorder.c, the recorder cksession
// uses, with Opus packets like the PS5's: CELT only, full band, stereo, 10 ms
// frames at a constant 64 kbps (80 bytes each, TOC 0xf4, as in the captures
// of 2026-10-22). The sound is two tones that slide and swell, one per
// channel, so a channel swap or a wrong frame shows. Every LOST_EVERY-th frame
// is handed over as lost (0: none). PATH.h264 stays empty.

#include "Bridge/ck_recorder.h"

#include <opus/opus.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define RATE 48000
#define CHANNELS 2
#define FRAME 480
#define PI 3.14159265358979

int main(int argc, char **argv)
{
	if(argc != 4)
	{
		fprintf(stderr, "usage: make_opus PATH SECONDS LOST_EVERY\n");
		return 2;
	}
	int seconds = atoi(argv[2]);
	int lost_every = atoi(argv[3]);
	ChiakiLog log;
	chiaki_log_init(&log, CHIAKI_LOG_ERROR | CHIAKI_LOG_WARNING, chiaki_log_cb_print, NULL);

	int err;
	OpusEncoder *encoder = opus_encoder_create(RATE, CHANNELS, OPUS_APPLICATION_RESTRICTED_LOWDELAY, &err);
	if(err != OPUS_OK)
		return 1;
	opus_encoder_ctl(encoder, OPUS_SET_BITRATE(64000));
	opus_encoder_ctl(encoder, OPUS_SET_VBR(0));

	CKRecorder *recorder = ck_recorder_open(argv[1], &log);
	if(!recorder)
		return 1;
	ChiakiAudioHeader header = { CHANNELS, 16, RATE, FRAME, 0 };
	ck_recorder_audio_header(recorder, &header);

	double phase[CHANNELS] = { 0.0, 0.0 };
	int frames = seconds * RATE / FRAME;
	for(int f = 0; f < frames; f++)
	{
		opus_int16 pcm[FRAME * CHANNELS];
		for(int i = 0; i < FRAME; i++)
		{
			double t = (double)(f * FRAME + i) / RATE;
			// Left slides 300 to 900 Hz and back every 4 s; right is 1.5 times it. Both swell twice a second.
			double hz = 600.0 - 300.0 * cos(2.0 * PI * t / 4.0);
			double gain = 6000.0 * (0.6 + 0.4 * sin(2.0 * PI * 2.0 * t));
			for(int c = 0; c < CHANNELS; c++)
			{
				phase[c] += 2.0 * PI * hz * (c ? 1.5 : 1.0) / RATE;
				pcm[i * CHANNELS + c] = (opus_int16)lrint(gain * sin(phase[c]));
			}
		}
		uint8_t packet[1500];
		int size = opus_encode(encoder, pcm, FRAME, packet, sizeof(packet));
		if(size < 0)
			return 1;
		if(lost_every > 0 && f % lost_every == lost_every - 1)
			ck_recorder_audio_frame(recorder, NULL, 0);
		else
			ck_recorder_audio_frame(recorder, packet, (size_t)size);
	}
	CKRecorderStats stats;
	ck_recorder_close(recorder, &stats);
	opus_encoder_destroy(encoder);
	printf("packets=%u lost=%u samples=%llu\n", stats.audio_packets, stats.audio_lost,
		(unsigned long long)stats.audio_samples);
	return stats.audio_error ? 1 : 0;
}
