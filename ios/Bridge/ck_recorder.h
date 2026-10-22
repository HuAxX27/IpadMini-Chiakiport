// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A2.8: records a session to two files for testing video and audio without a
// console. PATH.h264 gets every buffer of the video callback as the core hands
// it over: Annex B, the profile header first and then each frame, the format
// ios/Video's tests and ffplay read. PATH.opus gets the Opus frames in an Ogg
// stream (RFC 7845), which keeps where each frame ends and plays in ffplay or
// VLC; a frame lost on the network becomes a one-byte packet that tells the
// decoder to conceal it, so the timing stays right.
//
// Nothing is allocated per frame: the files are written through buffers made
// at open. The video functions run on one thread and the audio ones on
// another (the core's), and ck_recorder_close() comes after both stopped.
// Tested in ios/tests/core/recorder.c.

#ifndef CK_RECORDER_H
#define CK_RECORDER_H

#include <chiaki/audio.h>
#include <chiaki/audioreceiver.h>
#include <chiaki/log.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Largest Opus packet kept, far above the PS5's (a few hundred bytes); bigger ones are dropped. */
#define CK_RECORDER_OPUS_PACKET_MAX 4000

typedef struct ck_recorder_t CKRecorder;

typedef struct ck_recorder_stats_t
{
	uint32_t video_buffers;
	uint64_t video_bytes;
	/** Opus packets written, and of those how many stand for frames lost on the network. */
	uint32_t audio_packets;
	uint32_t audio_lost;
	/** Audio frames left out: before the header, lost before the first frame, too big or with a bad TOC. */
	uint32_t audio_dropped;
	/** Length of the audio written, in samples at 48 kHz. */
	uint64_t audio_samples;
	/** A write failed (a full disk, say) and that file stopped there. */
	bool video_error;
	bool audio_error;
} CKRecorderStats;

/**
 * Creates or empties PATH.h264 and PATH.opus. path is at most 1000 bytes.
 * Returns NULL, after logging why, when a file cannot be opened.
 */
CKRecorder *ck_recorder_open(const char *path, ChiakiLog *log);

/**
 * Finishes the Ogg stream, closes both files and frees the recorder. Fills
 * *stats, when not NULL, with the final counts, including an error the last
 * write of a file only found at closing. NULL is fine.
 */
void ck_recorder_close(CKRecorder *recorder, CKRecorderStats *stats);

/** Appends a buffer of the video callback (contract C2) to PATH.h264. */
void ck_recorder_video(CKRecorder *recorder, const uint8_t *buf, size_t buf_size);

/**
 * The audio header of the stream. The first one starts the Ogg stream; a
 * later one with the same channels and rate is ignored, and one that differs
 * stops the audio recording, since one Ogg Opus stream cannot change them.
 */
void ck_recorder_audio_header(CKRecorder *recorder, const ChiakiAudioHeader *header);

/** One Opus frame in playback order; NULL with size 0 is a frame lost on the network (contract C3). */
void ck_recorder_audio_frame(CKRecorder *recorder, const uint8_t *buf, size_t buf_size);

/** A ChiakiAudioSink that records, for chiaki_session_set_audio_sink() or -[CKSession setAudioSink:]. */
void ck_recorder_get_audio_sink(CKRecorder *recorder, ChiakiAudioSink *sink);

/** What was written so far. Once both threads stopped, or from their own thread for its half. */
void ck_recorder_stats(const CKRecorder *recorder, CKRecorderStats *stats);

#ifdef __cplusplus
}
#endif

#endif // CK_RECORDER_H
