// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A4.2: see ck_audio_tone.h.

#include "ck_audio_tone.h"

#include <math.h>

#define TWO_PI 6.283185307179586
#define FADE_MS 20

void ck_audio_tone_init(CKAudioTone *tone, uint32_t rate, uint32_t channels, double hz, double dbfs)
{
	tone->channels = channels;
	tone->step = TWO_PI * hz / rate;
	tone->phase = 0.0;
	tone->peak = 32767.0 * pow(10.0, dbfs / 20.0);
	tone->fade_frames = rate * FADE_MS / 1000;
	tone->fade_done = 0;
}

void ck_audio_tone_fill(int16_t *pcm, uint32_t frames, void *user)
{
	CKAudioTone *tone = user;
	for(uint32_t i = 0; i < frames; i++)
	{
		double gain = 1.0;
		if(tone->fade_done < tone->fade_frames)
		{
			gain = (double)tone->fade_done / tone->fade_frames;
			tone->fade_done++;
		}
		double v = tone->peak * gain * sin(tone->phase);
		int16_t sample = (int16_t)lrint(v);
		for(uint32_t c = 0; c < tone->channels; c++)
			*pcm++ = sample;
		tone->phase += tone->step;
		if(tone->phase >= TWO_PI)
			tone->phase -= TWO_PI;
	}
}
