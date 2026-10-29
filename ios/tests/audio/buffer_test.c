// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Tests for the jitter buffer (A4.3, ios/Audio/ck_audio_buffer.c): where the
// output starts, the frames over max_ms that are dropped and crossfaded, the
// fade-out when it runs dry and the fade-in after, the clock correction with
// a writer 0.5 % fast or slow, a flush in the middle of the sound, and both
// threads at once. player_test.c tries the whole player under 2 % loss.
// Run by ios/tests/audio_tests.sh.
//
// Most tests write frames whose samples say where they are in the stream
// (left: the low 15 bits of the position, right: the next 15), so the output
// tells exactly which frames it played. Where the buffer fades, the samples
// are scaled and only their size is checked.

// nanosleep() and rand_r().
#define _POSIX_C_SOURCE 200809L

#include "Audio/ck_audio_buffer.h"
#include "audio_click.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
#define CH 2
#define FRAME 480
#define FADE (RATE * CK_AUDIO_BUFFER_FADE_MS / 1000)
#define TARGET_MS 50
#define MAX_MS 80
#define TARGET (RATE * TARGET_MS / 1000)
#define PI 3.14159265358979

static ChiakiLog test_log;

static CKAudioBuffer *make_buffer(void)
{
	CKAudioBufferConfig config = { RATE, CH, TARGET_MS, MAX_MS };
	CKAudioBuffer *buffer = ck_audio_buffer_new(&test_log, &config);
	CHECK(buffer != NULL);
	return buffer;
}

/** A frame whose samples carry their position in the stream. */
static void coded(int16_t *pcm, uint32_t pos, uint32_t frames)
{
	for(uint32_t i = 0; i < frames; i++)
	{
		pcm[i * CH] = (int16_t)((pos + i) & 0x7fff);
		pcm[i * CH + 1] = (int16_t)(((pos + i) >> 15) & 0x7fff);
	}
}

static uint32_t position(const int16_t *sample)
{
	return (uint32_t)sample[0] | (uint32_t)sample[1] << 15;
}

static void push_coded(CKAudioBuffer *buffer, uint32_t pos)
{
	int16_t pcm[FRAME * CH];
	coded(pcm, pos, FRAME);
	ck_audio_buffer_push(buffer, pcm, FRAME);
}

/** Whether frames [from, to) of out are positions start, start + 1, ... exactly. */
static bool plays(const int16_t *out, uint32_t from, uint32_t to, uint32_t start)
{
	for(uint32_t i = from; i < to; i++)
		if(position(out + i * CH) != start + i - from)
		{
			fprintf(stderr, "   frame %u plays %u, expected %u\n", i, position(out + i * CH), start + i - from);
			return false;
		}
	return true;
}

/** Whether frames [from, to) are positions start... scaled by a gain going up (or down) from 0 to 1. */
static bool fades(const int16_t *out, uint32_t from, uint32_t to, uint32_t start, bool in)
{
	double prev = in ? -1.0 : 2.0;
	for(uint32_t i = from; i < to; i++)
	{
		int16_t expected[CH];
		coded(expected, start + i - from, 1);
		if(expected[0] < 100)
			continue;
		double gain = (double)out[i * CH] / expected[0];
		if(gain < -0.001 || gain > 1.001 || (in ? gain < prev - 0.01 : gain > prev + 0.01))
		{
			fprintf(stderr, "   frame %u has gain %.3f after %.3f\n", i, gain, prev);
			return false;
		}
		prev = gain;
	}
	// The gain went all the way: from about 0 up to about 1, or down to about 0.
	if(in ? prev < 0.9 : prev > 0.1)
	{
		fprintf(stderr, "   the fade ends at gain %.3f\n", prev);
		return false;
	}
	return true;
}

static bool silent(const int16_t *out, uint32_t from, uint32_t to)
{
	for(uint32_t i = from * CH; i < to * CH; i++)
		if(out[i])
			return false;
	return true;
}

static void test_start_cap_and_splice(void)
{
	CKAudioBuffer *buffer = make_buffer();
	int16_t out[FRAME * CH];
	CKAudioBufferStats stats;

	ck_audio_buffer_pull(out, FRAME, buffer);
	CHECK(silent(out, 0, FRAME));
	ck_audio_buffer_take_stats(buffer, &stats);
	CHECK(!stats.playing);
	CHECK_EQ(stats.level_ms, 0);

	// 100 ms with nobody pulling: 80 ms fit, the last two frames are dropped.
	for(uint32_t f = 0; f < 10; f++)
		push_coded(buffer, f * FRAME);
	ck_audio_buffer_take_stats(buffer, &stats);
	CHECK_EQ(stats.pushed, 10);
	CHECK_EQ(stats.dropped, 2);

	// 75 ms are in the ring (the last 5 ms are held back): it plays the newest 50 ms, fading in.
	ck_audio_buffer_pull(out, FRAME, buffer);
	uint32_t start = 8 * FRAME - FADE - TARGET;
	CHECK(fades(out, 0, FADE, start, true));
	CHECK_EQ(out[0], 0);
	CHECK(plays(out, FADE, FRAME, start + FADE));
	ck_audio_buffer_take_stats(buffer, &stats);
	CHECK(stats.playing);
	CHECK_EQ(stats.level_ms, TARGET_MS);

	// After the hole of the two dropped frames: the held-back 5 ms crossfade into the next frame.
	push_coded(buffer, 10 * FRAME);
	push_coded(buffer, 11 * FRAME);
	static int16_t all[FRAME * CH * 8];
	for(uint32_t p = 0; p < 6; p++)
		ck_audio_buffer_pull(all + p * FRAME * CH, FRAME, buffer);
	uint32_t before = 8 * FRAME - FADE - (start + FRAME);
	CHECK(plays(all, 0, before, start + FRAME));
	for(uint32_t i = before; i < before + FADE; i++)
	{
		// Each sample lies between the two it mixes: the one before the hole and the one after.
		int16_t a[CH], b[CH];
		coded(a, 8 * FRAME - FADE + (i - before), 1);
		coded(b, 10 * FRAME + (i - before), 1);
		for(int c = 0; c < CH; c++)
		{
			int16_t lo = a[c] < b[c] ? a[c] : b[c], hi = a[c] < b[c] ? b[c] : a[c];
			CHECK(all[i * CH + c] >= lo - 1 && all[i * CH + c] <= hi + 1);
		}
	}
	CHECK(plays(all, before + FADE, 5 * FRAME, 10 * FRAME + FADE));
	// Then it runs dry: the last 5 ms fade out.
	CHECK(fades(all, 5 * FRAME, 5 * FRAME + FADE, 10 * FRAME + FADE + 5 * FRAME - before - FADE, false));
	ck_audio_buffer_take_stats(buffer, &stats);
	CHECK_EQ(stats.underruns, 1);
	ck_audio_buffer_free(buffer);
}

static void test_underrun_and_resume(void)
{
	CKAudioBuffer *buffer = make_buffer();
	static int16_t out[FRAME * CH * 6];
	CKAudioBufferStats stats;
	for(uint32_t f = 0; f < 6; f++)
		push_coded(buffer, f * FRAME);
	// 55 ms in the ring: it starts 5 ms in and plays until 5 ms are left, which fade out.
	for(uint32_t p = 0; p < 5; p++)
		ck_audio_buffer_pull(out + p * FRAME * CH, FRAME, buffer);
	uint32_t ring = 6 * FRAME - FADE;
	uint32_t start = ring - TARGET;
	CHECK(fades(out, 0, FADE, start, true));
	uint32_t played = ring - FADE - start;
	CHECK(plays(out, FADE, played, start + FADE));
	CHECK(fades(out, played, played + FADE, start + played, false));
	CHECK_EQ(out[(played + FADE - 1) * CH], 0);
	CHECK(silent(out, played + FADE, 5 * FRAME));
	ck_audio_buffer_take_stats(buffer, &stats);
	CHECK_EQ(stats.underruns, 1);
	CHECK(!stats.playing);

	ck_audio_buffer_pull(out, FRAME, buffer);
	CHECK(silent(out, 0, FRAME));

	// 50 ms more: it plays them from where it stopped, fading in.
	for(uint32_t f = 6; f < 11; f++)
		push_coded(buffer, f * FRAME);
	ck_audio_buffer_pull(out, FRAME, buffer);
	CHECK(fades(out, 0, FADE, ring, true));
	CHECK(plays(out, FADE, FRAME, ring + FADE));
	ck_audio_buffer_take_stats(buffer, &stats);
	CHECK(stats.playing);
	CHECK_EQ(stats.underruns, 0);
	ck_audio_buffer_free(buffer);
}

static void test_flush(void)
{
	CKAudioBuffer *buffer = make_buffer();
	static int16_t out[FRAME * CH * 3];
	for(uint32_t f = 0; f < 6; f++)
		push_coded(buffer, f * FRAME);
	ck_audio_buffer_pull(out, FRAME, buffer);
	uint32_t next = 6 * FRAME - FADE - TARGET + FRAME;

	// A new stream: what is left of the old one fades out in 5 ms, then the new one starts from silence.
	ck_audio_buffer_flush(buffer);
	uint32_t other = 1u << 20;
	for(uint32_t f = 0; f < 6; f++)
		push_coded(buffer, other + f * FRAME);
	ck_audio_buffer_pull(out, FRAME, buffer);
	CHECK(fades(out, 0, FADE, next, false));
	CHECK(silent(out, FADE, FRAME));
	ck_audio_buffer_pull(out, FRAME, buffer);
	uint32_t start = other + 6 * FRAME - FADE - TARGET;
	CHECK(fades(out, 0, FADE, start, true));
	CHECK(plays(out, FADE, FRAME, start + FADE));
	ck_audio_buffer_free(buffer);
}

/** A tone, 440 Hz left and 660 Hz right, going on from *phase. */
static void tone(int16_t *pcm, uint32_t frames, double *phase)
{
	for(uint32_t i = 0; i < frames; i++, *phase += 2.0 * PI * 440.0 / RATE)
	{
		pcm[i * CH] = (int16_t)lrint(8000.0 * sin(*phase));
		pcm[i * CH + 1] = (int16_t)lrint(8000.0 * sin(*phase * 1.5));
	}
}

/** Click limit for the tone: three times its own largest second difference. */
static double tone_click_limit(void)
{
	static int16_t pcm[RATE * CH];
	double phase = 0.0;
	tone(pcm, RATE, &phase);
	return 3.0 * click_max_d2(pcm, RATE, CH);
}

/**
 * 60 s with the writer's frames coming every 10 ms x (1 + skew), on time, and
 * the output pulling every 10 ms. A frame asked for goes on with the tone, as
 * Opus concealment would.
 */
static void run_clock(double skew, uint32_t *dropped, uint32_t *requested)
{
	CKAudioBuffer *buffer = make_buffer();
	enum { SECONDS = 60 };
	static int16_t out[SECONDS * RATE * CH];
	int16_t pcm[FRAME * CH];
	double phase = 0.0;
	uint32_t pushes = 0, pulls = 0, underruns = 0;
	*dropped = *requested = 0;
	bool levels_ok = true;
	while(pulls < SECONDS * 100)
	{
		double t_push = pushes * 0.010 * (1.0 + skew), t_pull = pulls * 0.010;
		if(t_push <= t_pull)
		{
			if(ck_audio_buffer_take_request(buffer))
			{
				tone(pcm, FRAME, &phase);
				ck_audio_buffer_push(buffer, pcm, FRAME);
			}
			tone(pcm, FRAME, &phase);
			ck_audio_buffer_push(buffer, pcm, FRAME);
			pushes++;
			continue;
		}
		ck_audio_buffer_pull(out + (size_t)pulls * FRAME * CH, FRAME, buffer);
		if(++pulls % 100)
			continue;
		CKAudioBufferStats stats;
		ck_audio_buffer_take_stats(buffer, &stats);
		*dropped += stats.dropped;
		*requested += stats.requested;
		underruns += stats.underruns;
		if(pulls > 100 && (stats.level_ms < 40 || stats.level_ms > 80))
		{
			fprintf(stderr, "   skew %+.3f, second %u: level %u ms\n", skew, pulls / 100, stats.level_ms);
			levels_ok = false;
		}
	}
	CHECK(levels_ok);
	CHECK_EQ(underruns, 0);
	printf("   skew %+.3f: %u dropped, %u asked for\n", skew, *dropped, *requested);
	size_t first = 0;
	uint32_t clicks = click_count(out, (size_t)pulls * FRAME, CH, RATE, tone_click_limit(), &first);
	if(clicks)
		fprintf(stderr, "   skew %+.3f: %u clicks, the first at %.3f s\n", skew, clicks, (double)first / RATE);
	CHECK_EQ(clicks, 0);
	ck_audio_buffer_free(buffer);
}

static void test_clock(void)
{
	uint32_t dropped, requested;
	// The PS5 0.5 % fast: 5 ms too many every second. A drop takes 15 ms out (the frame and the
	// 5 ms the crossfade overlaps): one every three seconds.
	run_clock(-0.005, &dropped, &requested);
	CHECK(dropped >= 17 && dropped <= 23);
	CHECK_EQ(requested, 0);
	// 0.5 % slow: a frame asked for every two seconds.
	run_clock(0.005, &dropped, &requested);
	CHECK_EQ(dropped, 0);
	CHECK(requested >= 25 && requested <= 35);
	// The same clock: nothing to correct.
	run_clock(0.0, &dropped, &requested);
	CHECK_EQ(dropped, 0);
	CHECK_EQ(requested, 0);
}

typedef struct thread_test_t
{
	CKAudioBuffer *buffer;
	int16_t *out;
	uint32_t pulls;
	unsigned seed;
} ThreadTest;

static void sleep_us(long us)
{
	struct timespec ts = { us / 1000000, (us % 1000000) * 1000 };
	nanosleep(&ts, NULL);
}

static void *writer(void *user)
{
	ThreadTest *test = user;
	int16_t pcm[FRAME * CH];
	double phase = 0.0;
	for(uint32_t f = 0; f < test->pulls; f++)
	{
		tone(pcm, FRAME, &phase);
		if(ck_audio_buffer_take_request(test->buffer))
		{
			ck_audio_buffer_push(test->buffer, pcm, FRAME);
			tone(pcm, FRAME, &phase);
		}
		ck_audio_buffer_push(test->buffer, pcm, FRAME);
		// 10 ms on average, in bursts and gaps of up to 12 ms.
		sleep_us(4000 + (long)(rand_r(&test->seed) % 12000));
	}
	return NULL;
}

static void test_threads(void)
{
	ThreadTest test;
	test.buffer = make_buffer();
	test.pulls = 300;
	test.seed = 7;
	test.out = calloc((size_t)test.pulls * FRAME * CH, sizeof(int16_t));
	CHECK(test.out != NULL);
	pthread_t thread;
	CHECK_EQ(pthread_create(&thread, NULL, writer, &test), 0);
	for(uint32_t p = 0; p < test.pulls; p++)
	{
		ck_audio_buffer_pull(test.out + (size_t)p * FRAME * CH, FRAME, test.buffer);
		sleep_us(10000);
	}
	pthread_join(thread, NULL);
	CKAudioBufferStats stats;
	ck_audio_buffer_take_stats(test.buffer, &stats);
	CHECK_EQ(stats.pushed, test.pulls + stats.requested);
	// Whatever the scheduler did, every gap fades and every drop crossfades.
	size_t first = 0;
	uint32_t clicks = click_count(test.out, (size_t)test.pulls * FRAME, CH, RATE, tone_click_limit(), &first);
	if(clicks)
		fprintf(stderr, "   threads: %u clicks, the first at %.3f s\n", clicks, (double)first / RATE);
	CHECK_EQ(clicks, 0);
	printf("   threads: %u underruns, %u dropped, %u asked for\n", stats.underruns, stats.dropped, stats.requested);
	free(test.out);
	ck_audio_buffer_free(test.buffer);
}

static void test_config(void)
{
	CKAudioBufferConfig bad[] = {
		{ 48000, 3, 50, 80 },
		{ 48000, 2, 5, 80 },
		{ 48000, 2, 50, 60 },
		{ 4000, 2, 50, 80 },
	};
	for(size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
		CHECK(ck_audio_buffer_new(&test_log, &bad[i]) == NULL);
	// A frame longer than half the buffer can never fit: dropped, once said.
	CKAudioBuffer *buffer = make_buffer();
	static int16_t big[RATE / 20 * CH];
	ck_audio_buffer_push(buffer, big, RATE / 20);
	CKAudioBufferStats stats;
	ck_audio_buffer_take_stats(buffer, &stats);
	CHECK_EQ(stats.dropped, 1);
	ck_audio_buffer_free(buffer);
}

int main(void)
{
	chiaki_log_init(&test_log, CHIAKI_LOG_ERROR | CHIAKI_LOG_WARNING, chiaki_log_cb_print, NULL);
	test_start_cap_and_splice();
	test_underrun_and_resume();
	test_flush();
	test_clock();
	test_threads();
	test_config();
	if(failures)
	{
		fprintf(stderr, "buffer_test: %d failures\n", failures);
		return 1;
	}
	printf("buffer_test: ok\n");
	return 0;
}
