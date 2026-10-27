// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A4.2: the iPad's speaker, through AudioQueue. Not a contract: only the
// audio player (CKAudioSink.h) and the ckaudio tool use it.
//
// The output plays interleaved 16-bit PCM and asks for it with a pull
// callback, one buffer at a time, on AudioQueue's own thread. The callback
// must fill the whole buffer every time (silence when it has nothing) and
// must not block, lock or allocate: whatever it does delays the sound.
//
// Nothing is allocated while playing: the queue's buffers are made once, in
// ck_audio_output_new(). Every function but the pull callback runs on one
// thread, the main one in the app.

#ifndef CK_AUDIO_OUTPUT_H
#define CK_AUDIO_OUTPUT_H

#include <chiaki/log.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Most buffers a queue takes. Three are enough; more only add delay. */
#define CK_AUDIO_OUTPUT_BUFFERS_MAX 8

/** Fills frames frames of interleaved PCM (frames x channels samples). */
typedef void (*CKAudioOutputPull)(int16_t *pcm, uint32_t frames, void *user);

typedef struct ck_audio_output_t CKAudioOutput;

typedef struct ck_audio_output_config_t
{
	uint32_t rate;
	uint32_t channels;
	/** Frames in each buffer: 480 is 10 ms at 48 kHz. */
	uint32_t buffer_frames;
	/** Buffers in the queue, 2 to CK_AUDIO_OUTPUT_BUFFERS_MAX. */
	uint32_t buffer_count;
} CKAudioOutputConfig;

typedef struct ck_audio_output_stats_t
{
	/** Buffers filled since the previous call. */
	uint32_t callbacks;
	/** Longest time between two fills since the previous call, in microseconds. */
	uint32_t max_gap_us;
	/**
	 * Fills since the previous call that came after the whole queue had time
	 * to play: the queue ran dry before them, which is heard as a cut.
	 */
	uint32_t late;
	/** Times AudioQueue said its timeline jumped (an overload or a device change), since the previous call. */
	uint32_t discontinuities;
	/** Audio handed to the queue that it has not played yet, right now, in milliseconds. */
	uint32_t queued_ms;
	/** Whether the queue is playing; iOS stops it when another app or an alarm takes the audio. */
	bool running;
} CKAudioOutputStats;

/**
 * Makes the queue and its buffers, without starting it. Returns NULL, after
 * logging why, when the format or the device is refused.
 */
CKAudioOutput *ck_audio_output_new(ChiakiLog *log, const CKAudioOutputConfig *config, CKAudioOutputPull pull, void *user);

/** Stops the queue, if it plays, and frees it. NULL is fine. */
void ck_audio_output_free(CKAudioOutput *output);

/**
 * Fills every buffer through the pull callback and starts playing. Also
 * resumes after an interruption, without filling again. Returns false, after
 * logging why, when AudioQueue refuses.
 */
bool ck_audio_output_start(CKAudioOutput *output);

/** Stops at once and drops what is queued. ck_audio_output_start() plays again. */
void ck_audio_output_stop(CKAudioOutput *output);

/** What happened since the previous call. */
void ck_audio_output_take_stats(CKAudioOutput *output, CKAudioOutputStats *stats);

#ifdef __cplusplus
}
#endif

#endif // CK_AUDIO_OUTPUT_H
