// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Tests for the test tone (A4.2, ios/Audio/ck_audio_tone.c): the sine goes on
// across buffers of any size exactly as in one long buffer, every channel gets
// the same sample, the peak and the frequency are the asked ones, and the
// 20 ms fade-in starts from silence. Run by ios/tests/audio_tests.sh.

#include "Audio/ck_audio_tone.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond) do { \
	if(!(cond)) { \
		fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
		failures++; \
	} \
} while(0)

#define CHECK_EQ(a, b) do { \
	long long a_ = (long long)(a), b_ = (long long)(b); \
	if(a_ != b_) { \
		fprintf(stderr, "%s:%d: %s == %lld, expected %s == %lld\n", __FILE__, __LINE__, #a, a_, #b, b_); \
		failures++; \
	} \
} while(0)

#define RATE 48000
#define FRAMES (RATE * 2)

static int16_t whole[FRAMES * 2];
static int16_t pieces[FRAMES * 2];

static void test_buffers_join(void)
{
	CKAudioTone tone;
	ck_audio_tone_init(&tone, RATE, 2, 440.0, -12.0);
	ck_audio_tone_fill(whole, FRAMES, &tone);

	// Odd sizes, as an output whose buffers do not line up with anything would ask.
	static const uint32_t sizes[] = { 480, 1, 37, 1024, 479, 2000, 3 };
	ck_audio_tone_init(&tone, RATE, 2, 440.0, -12.0);
	uint32_t done = 0;
	for(size_t i = 0; done < FRAMES; i = (i + 1) % (sizeof(sizes) / sizeof(sizes[0])))
	{
		uint32_t n = sizes[i];
		if(n > FRAMES - done)
			n = FRAMES - done;
		ck_audio_tone_fill(pieces + done * 2, n, &tone);
		done += n;
	}
	CHECK(memcmp(whole, pieces, sizeof(whole)) == 0);
}

static void test_shape(void)
{
	CKAudioTone tone;
	ck_audio_tone_init(&tone, RATE, 2, 440.0, -12.0);
	ck_audio_tone_fill(whole, FRAMES, &tone);

	// Both channels carry the same sample.
	int same = 1;
	for(uint32_t i = 0; i < FRAMES; i++)
		same = same && whole[2 * i] == whole[2 * i + 1];
	CHECK(same);

	// The fade starts from silence and is over after 20 ms (960 frames).
	CHECK_EQ(whole[0], 0);
	int peak_fade = 0, peak = 0;
	for(uint32_t i = 0; i < 960; i++)
		peak_fade = abs(whole[2 * i]) > peak_fade ? abs(whole[2 * i]) : peak_fade;
	for(uint32_t i = 960; i < FRAMES; i++)
		peak = abs(whole[2 * i]) > peak ? abs(whole[2 * i]) : peak;
	// -12 dBFS is 32767 x 0.251 = 8231.
	CHECK(peak >= 8220 && peak <= 8231);
	CHECK(peak_fade < peak);

	// 440 Hz: 880 sign changes a second, counted after the fade.
	int crossings = 0;
	for(uint32_t i = 961; i < 961 + RATE; i++)
		crossings += (whole[2 * (i - 1)] < 0) != (whole[2 * i] < 0);
	CHECK(crossings >= 878 && crossings <= 882);

	// No jump: one sample to the next never moves more than the sine's steepest step.
	double max_step = 8231.0 * 2.0 * 3.14159265358979 * 440.0 / RATE + 1.0;
	int smooth = 1;
	for(uint32_t i = 1; i < FRAMES; i++)
		smooth = smooth && fabs((double)whole[2 * i] - whole[2 * (i - 1)]) <= max_step;
	CHECK(smooth);
}

static void test_mono(void)
{
	CKAudioTone tone;
	int16_t mono[1000];
	ck_audio_tone_init(&tone, RATE, 1, 1000.0, 0.0);
	ck_audio_tone_fill(mono, 1000, &tone);
	int peak = 0;
	for(int i = 960; i < 1000; i++)
		peak = abs(mono[i]) > peak ? abs(mono[i]) : peak;
	// 0 dBFS reaches full scale without wrapping around.
	CHECK(peak > 32000);
}

int main(void)
{
	test_buffers_join();
	test_shape();
	test_mono();
	if(failures)
	{
		fprintf(stderr, "tone_test: %d failure(s)\n", failures);
		return 1;
	}
	printf("tone_test: ok\n");
	return 0;
}
