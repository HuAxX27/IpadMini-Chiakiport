// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A4.2: a sine for the test tone (ckaudio tono) and the tests. It goes on
// across buffers of any size without a jump, and fades in over its first
// 20 ms so the start does not click either. Portable C, tested on Linux.

#ifndef CK_AUDIO_TONE_H
#define CK_AUDIO_TONE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ck_audio_tone_t
{
	uint32_t channels;
	/** Radians per frame, and where the sine is now, in [0, 2 pi). */
	double step;
	double phase;
	/** Peak, in sample units. */
	double peak;
	uint32_t fade_frames;
	uint32_t fade_done;
} CKAudioTone;

/** dbfs is the peak under full scale: -12 is a quarter of it. The same sample goes to every channel. */
void ck_audio_tone_init(CKAudioTone *tone, uint32_t rate, uint32_t channels, double hz, double dbfs);

/** Writes the next frames frames, interleaved. Has the shape of CKAudioOutputPull with the tone as user. */
void ck_audio_tone_fill(int16_t *pcm, uint32_t frames, void *tone);

#ifdef __cplusplus
}
#endif

#endif // CK_AUDIO_TONE_H
