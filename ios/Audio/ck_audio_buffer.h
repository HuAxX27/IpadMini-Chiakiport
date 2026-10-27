// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A4.3: the jitter buffer between the core's audio thread, which decodes each
// frame as it comes off the network, and the output's thread, which takes a
// buffer at a time on the iPad's clock. Portable C11, tested on Linux
// (ios/tests/audio/buffer_test.c). Not a contract: the audio player
// (CKAudioSink.h) uses it.
//
// A ring of interleaved 16-bit PCM with one writer and one reader and no
// lock: neither thread ever waits for the other. The output waits for
// target_ms of sound before it plays, so a frame that comes late is covered.
// What it does so nothing is heard as a click:
//
//  - When the ring runs dry (the network stalled), the output fades out the
//    last 5 ms it has and plays silence until target_ms is back, then fades
//    in. After a stall the frames that were held up come all at once: the
//    output starts again from the newest target_ms and skips the rest.
//  - A frame that would take the ring over max_ms is dropped, and the sound
//    before the hole is crossfaded into the sound after it, over 5 ms: a drop
//    takes out the frame and the 5 ms the crossfade overlaps.
//  - The PS5's clock and the iPad's are never quite the same, so the level
//    creeps up or down. Once a second the output looks at its levels: when
//    even the lowest is over target_ms + 5 ms the next frame is dropped; when
//    the average is under target_ms - 5 ms the writer is asked for one more
//    frame (ck_audio_buffer_take_request()), which the player makes with Opus
//    concealment. The lowest level, not the average, because the output's
//    thread coming late raises the level without delaying the sound: the
//    output holds that much less.
//
// The writer holds back the last 5 ms it was given, to have something to
// crossfade with when it drops a frame; that is 5 ms more delay.
//
// Allocates only in ck_audio_buffer_new().

#ifndef CK_AUDIO_BUFFER_H
#define CK_AUDIO_BUFFER_H

#include <chiaki/log.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Length of every fade and crossfade. */
#define CK_AUDIO_BUFFER_FADE_MS 5

typedef struct ck_audio_buffer_t CKAudioBuffer;

typedef struct ck_audio_buffer_config_t
{
	uint32_t rate;
	uint32_t channels;
	/** What the output waits for before it plays, and the level the clock correction holds. */
	uint32_t target_ms;
	/** A frame that would take the level over this is dropped. A frame lasts max_ms / 2 at most. */
	uint32_t max_ms;
} CKAudioBufferConfig;

typedef struct ck_audio_buffer_stats_t
{
	/** Frames written, dropped (over max_ms, or to bring the level down) and asked of the writer, since the previous call. */
	uint32_t pushed;
	uint32_t dropped;
	uint32_t requested;
	/** Times the ring ran dry and the output faded out, since the previous call. */
	uint32_t underruns;
	/** The level the output found before each of its pulls since the previous call: average, lowest, highest. */
	uint32_t level_ms;
	uint32_t level_min_ms;
	uint32_t level_max_ms;
	/** Whether the output is playing sound, or waiting for target_ms. */
	bool playing;
} CKAudioBufferStats;

/** Returns NULL, after logging why, for a config it cannot hold or without memory. */
CKAudioBuffer *ck_audio_buffer_new(ChiakiLog *log, const CKAudioBufferConfig *config);
void ck_audio_buffer_free(CKAudioBuffer *buffer);

/** The writer: one frame of frames x channels samples, in playback order. */
void ck_audio_buffer_push(CKAudioBuffer *buffer, const int16_t *pcm, uint32_t frames);

/** The writer: whether to make up one frame and push it now, before the next real one. */
bool ck_audio_buffer_take_request(CKAudioBuffer *buffer);

/** The writer: a new stream starts. What is buffered is dropped, after the output fades out what it plays. */
void ck_audio_buffer_flush(CKAudioBuffer *buffer);

/** The reader: fills frames frames, silence when there is nothing. A CKAudioOutputPull, with the buffer as user. */
void ck_audio_buffer_pull(int16_t *pcm, uint32_t frames, void *buffer);

/** Any thread. */
void ck_audio_buffer_take_stats(CKAudioBuffer *buffer, CKAudioBufferStats *stats);

#ifdef __cplusplus
}
#endif

#endif // CK_AUDIO_BUFFER_H
