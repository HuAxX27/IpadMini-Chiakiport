// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A4.3: what the audio tests call a click. The second difference
// x[n] - 2 x[n-1] + x[n-2] of a smooth sound stays small (a tone of amplitude
// A and frequency f peaks at A (2 pi f / rate)^2); a step to silence, a hole
// or a hard splice makes it as large as the jump. The tests take the largest
// one in the clean decode of the same packets and count the places where the
// buffered sound goes well over it.

#ifndef CK_AUDIO_CLICK_H
#define CK_AUDIO_CLICK_H

#include <stddef.h>
#include <stdint.h>

/** Largest second difference over every channel of interleaved PCM. */
double click_max_d2(const int16_t *pcm, size_t frames, uint32_t channels);

/**
 * Places where the second difference passes limit, counting one per
 * millisecond at most. *first gets the frame of the first one, when not NULL
 * and there is one.
 */
uint32_t click_count(const int16_t *pcm, size_t frames, uint32_t channels, uint32_t rate, double limit, size_t *first);

#endif // CK_AUDIO_CLICK_H
