// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A4.2: see ck_audio_output.h. AudioQueue calls back on its own thread (no
// run loop is given), each time it is done with a buffer; the buffer is filled
// through the pull callback and queued again right there, so the queue always
// holds buffer_count buffers while it plays.
//
// The fill thread only touches atomics and its own last_fill; the main thread
// reads the atomics and resets them. A fill that comes later than the whole
// queue lasts means the queue ran dry: that is the "late" count, which the
// two-minute tone test (P2-5) watches along with the ear.

#include "ck_audio_output.h"

#include <AudioToolbox/AudioToolbox.h>
#include <mach/mach_time.h>

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ck_audio_output_t
{
	ChiakiLog *log;
	CKAudioOutputConfig config;
	uint32_t bytes_per_frame;
	CKAudioOutputPull pull;
	void *user;

	AudioQueueRef queue;
	AudioQueueTimelineRef timeline;
	AudioQueueBufferRef buffers[CK_AUDIO_OUTPUT_BUFFERS_MAX];
	/** The buffers are filled and queued; the main thread's, cleared by a stop. */
	bool primed;

	/** Between start and stop: a buffer handed back outside it is not queued again. */
	atomic_bool playing;
	/** Set by a start: the next fill does not measure its gap, since the queue was not running before it. */
	atomic_bool gap_reset;
	/** The fill thread's: when the previous fill was, in mach ticks. */
	uint64_t last_fill;
	/** How long the whole queue plays, in mach ticks. */
	uint64_t queue_ticks;
	mach_timebase_info_data_t timebase;

	atomic_uint callbacks;
	atomic_uint max_gap_us;
	atomic_uint late;
	/** Frames queued since the last start that primed the buffers. Wraps after a day at 48 kHz. */
	atomic_uint frames_queued;
};

/** OSStatus as its four letters when it has them ('fmt?'), as a number otherwise. */
static const char *status_str(OSStatus status, char buf[16])
{
	uint32_t v = (uint32_t)status;
	char c[4] = { (char)(v >> 24), (char)(v >> 16), (char)(v >> 8), (char)v };
	bool printable = true;
	for(int i = 0; i < 4; i++)
		printable = printable && c[i] >= 0x20 && c[i] < 0x7f;
	if(printable)
		snprintf(buf, 16, "'%c%c%c%c'", c[0], c[1], c[2], c[3]);
	else
		snprintf(buf, 16, "%d", (int)status);
	return buf;
}

static uint32_t ticks_to_us(const CKAudioOutput *output, uint64_t ticks)
{
	uint64_t us = ticks * output->timebase.numer / output->timebase.denom / 1000;
	return us > UINT32_MAX ? UINT32_MAX : (uint32_t)us;
}

static OSStatus fill(CKAudioOutput *output, AudioQueueBufferRef buffer)
{
	uint32_t frames = output->config.buffer_frames;
	output->pull((int16_t *)buffer->mAudioData, frames, output->user);
	buffer->mAudioDataByteSize = frames * output->bytes_per_frame;
	OSStatus err = AudioQueueEnqueueBuffer(output->queue, buffer, 0, NULL);
	if(err == noErr)
		atomic_fetch_add_explicit(&output->frames_queued, frames, memory_order_relaxed);
	return err;
}

static void output_cb(void *user, AudioQueueRef queue, AudioQueueBufferRef buffer)
{
	(void)queue;
	CKAudioOutput *output = user;
	if(!atomic_load_explicit(&output->playing, memory_order_acquire))
		return;

	uint64_t now = mach_absolute_time();
	if(!atomic_exchange_explicit(&output->gap_reset, false, memory_order_relaxed) && output->last_fill)
	{
		uint64_t gap = now - output->last_fill;
		uint32_t gap_us = ticks_to_us(output, gap);
		// Only this thread raises it; a reset racing with it loses one sample of the maximum.
		if(gap_us > atomic_load_explicit(&output->max_gap_us, memory_order_relaxed))
			atomic_store_explicit(&output->max_gap_us, gap_us, memory_order_relaxed);
		if(gap > output->queue_ticks)
			atomic_fetch_add_explicit(&output->late, 1, memory_order_relaxed);
	}
	output->last_fill = now;
	atomic_fetch_add_explicit(&output->callbacks, 1, memory_order_relaxed);

	// Fails only while a stop is resetting the queue, which drops this buffer anyway.
	fill(output, buffer);
}

CKAudioOutput *ck_audio_output_new(ChiakiLog *log, const CKAudioOutputConfig *config, CKAudioOutputPull pull, void *user)
{
	if(config->rate < 8000 || config->rate > 192000 || config->channels < 1 || config->channels > 2
		|| config->buffer_frames < 64 || config->buffer_frames > config->rate
		|| config->buffer_count < 2 || config->buffer_count > CK_AUDIO_OUTPUT_BUFFERS_MAX || !pull)
	{
		CHIAKI_LOGE(log, "Audio output: refused format, %u Hz, %u channels, %u buffers of %u frames",
			config->rate, config->channels, config->buffer_count, config->buffer_frames);
		return NULL;
	}

	CKAudioOutput *output = calloc(1, sizeof(*output));
	if(!output)
		return NULL;
	output->log = log;
	output->config = *config;
	output->bytes_per_frame = config->channels * (uint32_t)sizeof(int16_t);
	output->pull = pull;
	output->user = user;
	atomic_init(&output->playing, false);
	atomic_init(&output->gap_reset, true);
	atomic_init(&output->callbacks, 0);
	atomic_init(&output->max_gap_us, 0);
	atomic_init(&output->late, 0);
	atomic_init(&output->frames_queued, 0);
	mach_timebase_info(&output->timebase);
	uint64_t queue_ns = (uint64_t)config->buffer_frames * config->buffer_count * 1000000000u / config->rate;
	output->queue_ticks = queue_ns * output->timebase.denom / output->timebase.numer;

	AudioStreamBasicDescription format;
	memset(&format, 0, sizeof(format));
	format.mSampleRate = config->rate;
	format.mFormatID = kAudioFormatLinearPCM;
	// Native endian: the A5 is little-endian, which is a flag of 0.
	format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
	format.mBytesPerPacket = output->bytes_per_frame;
	format.mFramesPerPacket = 1;
	format.mBytesPerFrame = output->bytes_per_frame;
	format.mChannelsPerFrame = config->channels;
	format.mBitsPerChannel = 16;

	char sbuf[16];
	OSStatus err = AudioQueueNewOutput(&format, output_cb, output, NULL, NULL, 0, &output->queue);
	if(err != noErr)
	{
		CHIAKI_LOGE(log, "Audio output: AudioQueueNewOutput failed with %s", status_str(err, sbuf));
		free(output);
		return NULL;
	}
	for(uint32_t i = 0; i < config->buffer_count; i++)
	{
		err = AudioQueueAllocateBuffer(output->queue, config->buffer_frames * output->bytes_per_frame, &output->buffers[i]);
		if(err != noErr)
		{
			CHIAKI_LOGE(log, "Audio output: AudioQueueAllocateBuffer failed with %s", status_str(err, sbuf));
			AudioQueueDispose(output->queue, true);
			free(output);
			return NULL;
		}
	}
	// Only for the stats: without a timeline the output still plays.
	err = AudioQueueCreateTimeline(output->queue, &output->timeline);
	if(err != noErr)
	{
		CHIAKI_LOGW(log, "Audio output: AudioQueueCreateTimeline failed with %s", status_str(err, sbuf));
		output->timeline = NULL;
	}
	return output;
}

void ck_audio_output_free(CKAudioOutput *output)
{
	if(!output)
		return;
	ck_audio_output_stop(output);
	if(output->timeline)
		AudioQueueDisposeTimeline(output->queue, output->timeline);
	// Frees the buffers too.
	AudioQueueDispose(output->queue, true);
	free(output);
}

bool ck_audio_output_start(CKAudioOutput *output)
{
	char sbuf[16];
	OSStatus err;
	if(!output->primed)
	{
		atomic_store(&output->frames_queued, 0);
		atomic_store(&output->playing, true);
		for(uint32_t i = 0; i < output->config.buffer_count; i++)
		{
			err = fill(output, output->buffers[i]);
			if(err != noErr)
			{
				CHIAKI_LOGE(output->log, "Audio output: AudioQueueEnqueueBuffer failed with %s", status_str(err, sbuf));
				ck_audio_output_stop(output);
				return false;
			}
		}
		output->primed = true;
	}
	atomic_store(&output->gap_reset, true);
	err = AudioQueueStart(output->queue, NULL);
	if(err != noErr)
	{
		CHIAKI_LOGE(output->log, "Audio output: AudioQueueStart failed with %s", status_str(err, sbuf));
		return false;
	}
	return true;
}

void ck_audio_output_stop(CKAudioOutput *output)
{
	atomic_store(&output->playing, false);
	// Synchronous: the fill thread is done with the buffers when it returns.
	AudioQueueStop(output->queue, true);
	output->primed = false;
}

void ck_audio_output_take_stats(CKAudioOutput *output, CKAudioOutputStats *stats)
{
	memset(stats, 0, sizeof(*stats));
	stats->callbacks = atomic_exchange(&output->callbacks, 0);
	stats->max_gap_us = atomic_exchange(&output->max_gap_us, 0);
	stats->late = atomic_exchange(&output->late, 0);

	UInt32 running = 0;
	UInt32 size = sizeof(running);
	if(AudioQueueGetProperty(output->queue, kAudioQueueProperty_IsRunning, &running, &size) == noErr)
		stats->running = running != 0;

	if(!output->timeline || !stats->running)
		return;
	AudioTimeStamp now;
	memset(&now, 0, sizeof(now));
	Boolean discontinuity = false;
	if(AudioQueueGetCurrentTime(output->queue, output->timeline, &now, &discontinuity) != noErr)
		return;
	if(discontinuity)
		stats->discontinuities = 1;
	if(!(now.mFlags & kAudioTimeStampSampleTimeValid))
		return;
	double queued = (double)atomic_load(&output->frames_queued) - now.mSampleTime;
	double total = (double)output->config.buffer_frames * output->config.buffer_count;
	if(queued < 0)
		queued = 0;
	if(queued > total)
		queued = total;
	stats->queued_ms = (uint32_t)(queued * 1000.0 / output->config.rate + 0.5);
}
