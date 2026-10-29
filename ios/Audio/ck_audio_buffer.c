// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A4.3: see ck_audio_buffer.h.
//
// Positions count frames since the start and wrap at 2^32; the ring's size is
// a power of two, so a position's slot is position & mask. The writer owns
// write_pos, the held-back tail and the splice; the reader owns read_pos, the
// fades and the clock correction's window. Each publishes its position with a
// release store and reads the other's with an acquire load. The requests, the
// flush and the stats are the only other shared fields, all atomics.

#include "ck_audio_buffer.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/** The clock correction's band around target_ms. */
#define BAND_ABOVE_MS 5
#define BAND_BELOW_MS 5
/** max_ms leaves the clock correction room over target_ms. */
#define MAX_OVER_TARGET_MS 15

typedef enum
{
	/** Silence, until target frames are there. */
	STATE_BUFFERING,
	STATE_PLAYING,
	/** Playing the last fade_left frames quieter and quieter, then silence. */
	STATE_FADING_OUT,
} ReaderState;

struct ck_audio_buffer_t
{
	ChiakiLog *log;
	uint32_t rate;
	uint32_t channels;
	/** In frames. */
	uint32_t target;
	uint32_t max;
	uint32_t fade;
	uint32_t max_push;
	uint32_t band_above;
	uint32_t band_below;
	int16_t *ring;
	uint32_t mask;

	atomic_uint write_pos;
	atomic_uint read_pos;

	// The writer's.
	uint32_t write;
	/** The last frames pushed, not in the ring yet: fade of them once the stream is going. */
	int16_t *tail;
	uint32_t tail_len;
	/** A frame was dropped since the last one kept: the next one kept is crossfaded into the tail. */
	bool splice;
	/** write at the last flush: the reader drops what comes before, so it does not count. */
	uint32_t flushed_at;
	bool warned_size;

	// The reader's.
	uint32_t read;
	ReaderState state;
	/** Frames played since the fade-in began; it is over at fade. */
	uint32_t fade_in;
	/** The fade-out: its length and the frames of it still to play. */
	uint32_t fade_len;
	uint32_t fade_left;
	uint32_t flush_seen;
	/** A flush came while playing: after the fade-out, skip to flush_to. */
	bool flush_after_fade;
	uint32_t flush_to;
	uint64_t window_sum;
	uint32_t window_min;
	uint32_t window_pulls;
	uint32_t window_frames;

	/** The clock correction's requests to the writer: one frame more, one frame less. */
	atomic_bool want_more;
	atomic_bool want_less;
	/** A flush from the writer: where the new stream starts, and a count the reader compares. */
	atomic_uint flush_pos;
	atomic_uint flush_gen;

	atomic_uint pushed;
	atomic_uint dropped;
	atomic_uint requested;
	atomic_uint underruns;
	atomic_uint level_sum;
	atomic_uint level_n;
	atomic_uint level_min;
	atomic_uint level_max;
	atomic_bool playing;
};

static uint32_t ms_to_frames(uint32_t rate, uint32_t ms)
{
	return (uint32_t)((uint64_t)rate * ms / 1000);
}

static uint32_t frames_to_ms(uint32_t rate, uint32_t frames)
{
	return (uint32_t)(((uint64_t)frames * 1000 + rate / 2) / rate);
}

static uint32_t min_u32(uint32_t a, uint32_t b)
{
	return a < b ? a : b;
}

CKAudioBuffer *ck_audio_buffer_new(ChiakiLog *log, const CKAudioBufferConfig *config)
{
	if(config->rate < 8000 || config->rate > 192000 || config->channels < 1 || config->channels > 2
		|| config->target_ms < 2 * CK_AUDIO_BUFFER_FADE_MS || config->max_ms < config->target_ms + MAX_OVER_TARGET_MS
		|| config->max_ms > 1000)
	{
		CHIAKI_LOGE(log, "Audio buffer: refused config, %u Hz, %u channels, target %u ms, max %u ms",
			config->rate, config->channels, config->target_ms, config->max_ms);
		return NULL;
	}
	CKAudioBuffer *b = calloc(1, sizeof(*b));
	if(!b)
		return NULL;
	b->log = log;
	b->rate = config->rate;
	b->channels = config->channels;
	b->target = ms_to_frames(config->rate, config->target_ms);
	b->max = ms_to_frames(config->rate, config->max_ms);
	b->fade = ms_to_frames(config->rate, CK_AUDIO_BUFFER_FADE_MS);
	b->max_push = b->max / 2;
	b->band_above = ms_to_frames(config->rate, BAND_ABOVE_MS);
	b->band_below = ms_to_frames(config->rate, BAND_BELOW_MS);
	// Up to max frames count, and up to as many more before a flush wait for the reader to skip them.
	uint32_t size = 1;
	while(size < 2 * b->max)
		size <<= 1;
	b->mask = size - 1;
	b->ring = malloc((size_t)size * b->channels * sizeof(int16_t));
	b->tail = malloc((size_t)b->fade * b->channels * sizeof(int16_t));
	if(!b->ring || !b->tail)
	{
		CHIAKI_LOGE(log, "Audio buffer: no memory for %u frames", size);
		ck_audio_buffer_free(b);
		return NULL;
	}
	b->state = STATE_BUFFERING;
	atomic_init(&b->write_pos, 0);
	atomic_init(&b->read_pos, 0);
	atomic_init(&b->want_more, false);
	atomic_init(&b->want_less, false);
	atomic_init(&b->flush_pos, 0);
	atomic_init(&b->flush_gen, 0);
	atomic_init(&b->pushed, 0);
	atomic_init(&b->dropped, 0);
	atomic_init(&b->requested, 0);
	atomic_init(&b->underruns, 0);
	atomic_init(&b->level_sum, 0);
	atomic_init(&b->level_n, 0);
	atomic_init(&b->level_min, UINT32_MAX);
	atomic_init(&b->level_max, 0);
	atomic_init(&b->playing, false);
	return b;
}

void ck_audio_buffer_free(CKAudioBuffer *buffer)
{
	if(!buffer)
		return;
	free(buffer->ring);
	free(buffer->tail);
	free(buffer);
}

/** The writer: copies frames to the ring at write, without publishing them. */
static void ring_write(CKAudioBuffer *b, const int16_t *src, uint32_t frames)
{
	uint32_t ch = b->channels;
	uint32_t at = b->write & b->mask;
	uint32_t first = min_u32(frames, b->mask + 1 - at);
	memcpy(b->ring + (size_t)at * ch, src, (size_t)first * ch * sizeof(int16_t));
	memcpy(b->ring, src + (size_t)first * ch, (size_t)(frames - first) * ch * sizeof(int16_t));
	b->write += frames;
}

void ck_audio_buffer_push(CKAudioBuffer *b, const int16_t *pcm, uint32_t frames)
{
	if(!frames)
		return;
	uint32_t ch = b->channels;
	atomic_fetch_add_explicit(&b->pushed, 1, memory_order_relaxed);
	if(frames > b->max_push)
	{
		if(!b->warned_size)
			CHIAKI_LOGE(b->log, "Audio buffer: dropping frames of %u samples, longer than half the buffer", frames);
		b->warned_size = true;
		atomic_fetch_add_explicit(&b->dropped, 1, memory_order_relaxed);
		return;
	}

	uint32_t read = atomic_load_explicit(&b->read_pos, memory_order_acquire);
	// What the ring holds, and of it what counts: not the stream before a flush, which the reader skips.
	uint32_t used = b->write - read + b->tail_len;
	uint32_t level = (int32_t)(b->flushed_at - read) > 0 ? b->write - b->flushed_at + b->tail_len : used;
	bool less = atomic_exchange_explicit(&b->want_less, false, memory_order_relaxed);
	if(less || level + frames > b->max || used + frames > b->mask + 1)
	{
		// The tail stays: the next frame kept is crossfaded into it.
		atomic_fetch_add_explicit(&b->dropped, 1, memory_order_relaxed);
		b->splice = true;
		return;
	}

	if(b->splice && b->tail_len == b->fade && frames >= b->fade)
	{
		// The tail goes on from the sound before the hole: fade it out while the frame fades in.
		for(uint32_t i = 0; i < b->fade; i++)
		{
			float w = (float)(i + 1) / (float)(b->fade + 1);
			for(uint32_t c = 0; c < ch; c++)
			{
				size_t s = (size_t)i * ch + c;
				b->tail[s] = (int16_t)((float)b->tail[s] * (1.0f - w) + (float)pcm[s] * w);
			}
		}
		pcm += (size_t)b->fade * ch;
		frames -= b->fade;
	}
	b->splice = false;

	// Into the ring go the tail and the frame but for their last fade frames, the new tail.
	uint32_t total = b->tail_len + frames;
	if(total <= b->fade)
	{
		memcpy(b->tail + (size_t)b->tail_len * ch, pcm, (size_t)frames * ch * sizeof(int16_t));
		b->tail_len = total;
		return;
	}
	uint32_t out = total - b->fade;
	uint32_t from_tail = min_u32(out, b->tail_len);
	ring_write(b, b->tail, from_tail);
	ring_write(b, pcm, out - from_tail);
	uint32_t tail_kept = b->tail_len - from_tail;
	memmove(b->tail, b->tail + (size_t)from_tail * ch, (size_t)tail_kept * ch * sizeof(int16_t));
	memcpy(b->tail + (size_t)tail_kept * ch, pcm + (size_t)(out - from_tail) * ch,
		(size_t)(b->fade - tail_kept) * ch * sizeof(int16_t));
	b->tail_len = b->fade;
	atomic_store_explicit(&b->write_pos, b->write, memory_order_release);
}

bool ck_audio_buffer_take_request(CKAudioBuffer *b)
{
	if(!atomic_exchange_explicit(&b->want_more, false, memory_order_relaxed))
		return false;
	atomic_fetch_add_explicit(&b->requested, 1, memory_order_relaxed);
	return true;
}

void ck_audio_buffer_flush(CKAudioBuffer *b)
{
	b->tail_len = 0;
	b->splice = false;
	b->flushed_at = b->write;
	atomic_store_explicit(&b->want_more, false, memory_order_relaxed);
	atomic_store_explicit(&b->want_less, false, memory_order_relaxed);
	atomic_store_explicit(&b->flush_pos, b->write, memory_order_relaxed);
	atomic_fetch_add_explicit(&b->flush_gen, 1, memory_order_release);
}

/**
 * The reader: copies n frames from read to dst and moves read past them,
 * through the fade-in while it lasts and, when fading, the fade-out.
 */
static void play(CKAudioBuffer *b, int16_t *dst, uint32_t n, bool fading)
{
	uint32_t ch = b->channels;
	uint32_t at = b->read & b->mask;
	uint32_t first = min_u32(n, b->mask + 1 - at);
	memcpy(dst, b->ring + (size_t)at * ch, (size_t)first * ch * sizeof(int16_t));
	memcpy(dst + (size_t)first * ch, b->ring, (size_t)(n - first) * ch * sizeof(int16_t));
	b->read += n;
	for(uint32_t i = 0; i < n && (fading || b->fade_in < b->fade); i++)
	{
		float g = 1.0f;
		if(b->fade_in < b->fade)
			g = (float)b->fade_in++ / (float)b->fade;
		if(fading)
			g *= (float)--b->fade_left / (float)b->fade_len;
		for(uint32_t c = 0; c < ch; c++)
			dst[(size_t)i * ch + c] = (int16_t)((float)dst[(size_t)i * ch + c] * g);
	}
}

static void start_fade_out(CKAudioBuffer *b, uint32_t frames)
{
	b->state = STATE_FADING_OUT;
	b->fade_len = frames;
	b->fade_left = frames;
}

static void record_level(CKAudioBuffer *b, uint32_t level)
{
	// Without take_stats() for a long while, start over before the sum can wrap.
	if(atomic_fetch_add_explicit(&b->level_n, 1, memory_order_relaxed) >= (1u << 16))
	{
		atomic_store_explicit(&b->level_n, 1, memory_order_relaxed);
		atomic_store_explicit(&b->level_sum, 0, memory_order_relaxed);
	}
	atomic_fetch_add_explicit(&b->level_sum, level, memory_order_relaxed);
	// Only this thread lowers or raises them; a take_stats() racing with it loses one sample.
	if(level < atomic_load_explicit(&b->level_min, memory_order_relaxed))
		atomic_store_explicit(&b->level_min, level, memory_order_relaxed);
	if(level > atomic_load_explicit(&b->level_max, memory_order_relaxed))
		atomic_store_explicit(&b->level_max, level, memory_order_relaxed);
}

/** Starts the clock correction's next second. */
static void reset_window(CKAudioBuffer *b)
{
	b->window_sum = 0;
	b->window_min = UINT32_MAX;
	b->window_pulls = 0;
	b->window_frames = 0;
}

/**
 * The clock correction: one second of levels while playing, then maybe a
 * request. A late frame lowers the level for a moment and a late output
 * thread raises it (the queue holds that much less), so only the lowest level
 * says the buffer is too full and only the average says it is too empty.
 */
static void correct_clock(CKAudioBuffer *b, uint32_t level, uint32_t frames)
{
	b->window_sum += level;
	if(level < b->window_min)
		b->window_min = level;
	b->window_pulls++;
	b->window_frames += frames;
	if(b->window_frames < b->rate)
		return;
	uint32_t average = (uint32_t)(b->window_sum / b->window_pulls);
	if(b->window_min > b->target + b->band_above)
		atomic_store_explicit(&b->want_less, true, memory_order_relaxed);
	else if(average + b->band_below < b->target)
		atomic_store_explicit(&b->want_more, true, memory_order_relaxed);
	reset_window(b);
}

void ck_audio_buffer_pull(int16_t *pcm, uint32_t frames, void *user)
{
	CKAudioBuffer *b = user;
	uint32_t gen = atomic_load_explicit(&b->flush_gen, memory_order_acquire);
	uint32_t write = atomic_load_explicit(&b->write_pos, memory_order_acquire);

	if(gen != b->flush_seen)
	{
		b->flush_seen = gen;
		uint32_t to = atomic_load_explicit(&b->flush_pos, memory_order_relaxed);
		// Already past it (this side read on into the new stream), nothing to drop.
		if((int32_t)(to - b->read) > 0)
		{
			if(b->state == STATE_BUFFERING)
				b->read = to;
			else
			{
				uint32_t left = b->state == STATE_FADING_OUT ? b->fade_left : b->fade;
				start_fade_out(b, min_u32(left, to - b->read));
				b->flush_after_fade = true;
				b->flush_to = to;
			}
		}
	}

	uint32_t avail = write - b->read;
	if(b->state == STATE_BUFFERING && avail >= b->target)
	{
		// What is over target came in a burst after a stall: start from the newest.
		b->read = write - b->target;
		avail = b->target;
		b->state = STATE_PLAYING;
		b->fade_in = 0;
		reset_window(b);
		atomic_store_explicit(&b->playing, true, memory_order_relaxed);
	}
	record_level(b, avail);

	uint32_t done = 0;
	if(b->state == STATE_PLAYING)
	{
		if(avail >= frames + b->fade)
		{
			play(b, pcm, frames, false);
			done = frames;
			correct_clock(b, avail, frames);
		}
		else
		{
			// Running dry: what is there but its last fade frames plays as is, those fade out.
			uint32_t fade = min_u32(avail, b->fade);
			done = avail - fade;
			play(b, pcm, done, false);
			start_fade_out(b, fade);
			atomic_fetch_add_explicit(&b->underruns, 1, memory_order_relaxed);
		}
	}
	if(b->state == STATE_FADING_OUT)
	{
		uint32_t n = min_u32(frames - done, b->fade_left);
		play(b, pcm + (size_t)done * b->channels, n, true);
		done += n;
		if(!b->fade_left)
		{
			b->state = STATE_BUFFERING;
			atomic_store_explicit(&b->playing, false, memory_order_relaxed);
			if(b->flush_after_fade && (int32_t)(b->flush_to - b->read) > 0)
				b->read = b->flush_to;
			b->flush_after_fade = false;
		}
	}
	memset(pcm + (size_t)done * b->channels, 0, (size_t)(frames - done) * b->channels * sizeof(int16_t));
	atomic_store_explicit(&b->read_pos, b->read, memory_order_release);
}

void ck_audio_buffer_take_stats(CKAudioBuffer *b, CKAudioBufferStats *stats)
{
	memset(stats, 0, sizeof(*stats));
	stats->pushed = atomic_exchange(&b->pushed, 0);
	stats->dropped = atomic_exchange(&b->dropped, 0);
	stats->requested = atomic_exchange(&b->requested, 0);
	stats->underruns = atomic_exchange(&b->underruns, 0);
	uint32_t n = atomic_exchange(&b->level_n, 0);
	uint32_t sum = atomic_exchange(&b->level_sum, 0);
	uint32_t lo = atomic_exchange(&b->level_min, UINT32_MAX);
	uint32_t hi = atomic_exchange(&b->level_max, 0);
	if(n)
	{
		stats->level_ms = frames_to_ms(b->rate, sum / n);
		stats->level_min_ms = lo == UINT32_MAX ? 0 : frames_to_ms(b->rate, lo);
		stats->level_max_ms = frames_to_ms(b->rate, hi);
	}
	else
	{
		// The output is not pulling: the level as it is.
		uint32_t level = atomic_load(&b->write_pos) - atomic_load(&b->read_pos);
		stats->level_ms = stats->level_min_ms = stats->level_max_ms = frames_to_ms(b->rate, level);
	}
	stats->playing = atomic_load_explicit(&b->playing, memory_order_relaxed);
}
