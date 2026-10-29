// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A4.2: the iPad's speaker, through RemoteIO or AudioQueue. Not a contract:
// only the audio player (CKAudioSink.h) and the ckaudio tool use it.
//
// The output plays interleaved 16-bit PCM and asks for it with a pull
// callback, one buffer at a time. The callback must fill the whole buffer
// every time (silence when it has nothing) and must not block, lock or
// allocate: whatever it does delays the sound.
//
// RemoteIO calls back on the audio system's real-time thread, once per
// hardware buffer (about the session's I/O buffer, 10 ms). AudioQueue calls
// back on its own thread, which is not real-time: on 2026-10-27 its cuts
// were heard each time the iPad rotated, even with 120 ms queued.
//
// Nothing is allocated while playing: the buffers are made once, in
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

/** Most buffers an AudioQueue takes. */
#define CK_AUDIO_OUTPUT_BUFFERS_MAX 8
/** Most frames RemoteIO asks for at once: iOS asks for 4096 with the screen locked. */
#define CK_AUDIO_OUTPUT_REMOTEIO_FRAMES_MAX 4096

typedef enum ck_audio_output_kind_t
{
	/** RemoteIO: the pull callback runs on the audio system's real-time thread. */
	CK_AUDIO_OUTPUT_REMOTEIO,
	/** AudioQueue: buffer_count buffers of buffer_frames, filled on AudioQueue's thread. */
	CK_AUDIO_OUTPUT_AUDIOQUEUE,
} CKAudioOutputKind;

/** Fills frames frames of interleaved PCM (frames x channels samples). */
typedef void (*CKAudioOutputPull)(int16_t *pcm, uint32_t frames, void *user);

typedef struct ck_audio_output_t CKAudioOutput;

typedef struct ck_audio_output_config_t
{
	CKAudioOutputKind kind;
	uint32_t rate;
	uint32_t channels;
	/** AudioQueue: frames in each buffer, 480 is 10 ms at 48 kHz. RemoteIO asks for what iOS decides. */
	uint32_t buffer_frames;
	/** AudioQueue: buffers in the queue, 2 to CK_AUDIO_OUTPUT_BUFFERS_MAX. */
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
	 * to play (AudioQueue), or more than two buffers after the one before
	 * (RemoteIO): the output ran dry before them, which is heard as a cut.
	 */
	uint32_t late;
	/** Times AudioQueue said its timeline jumped (an overload or a device change), since the previous call. */
	uint32_t discontinuities;
	/** AudioQueue: audio handed to the queue that it has not played yet, right now, in milliseconds. */
	uint32_t queued_ms;
	/** RemoteIO: the fewest and the most frames a callback asked for since the previous call. */
	uint32_t min_frames;
	uint32_t max_frames;
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
