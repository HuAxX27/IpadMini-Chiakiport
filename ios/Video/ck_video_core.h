// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A3.2: the plain C half of CKVideoSink (contract C2, CKVideoSink.h). It
// takes what the core hands the sink, turns it into what a hardware decoder
// wants with ck_h264.c (AVCC frames, and the SPS and PPS apart), decides
// when the decoder must be made again and which frames go to it, and keeps
// the counters behind ck_video_sink_take_stats(). The decoder sits behind
// CKVideoDecoder: VideoToolbox in CKVideoSink.m, a fake in the Linux tests
// (ios/tests/ck_video_tests.c).
//
// Every call takes the sink's lock, and the decoder's functions run inside
// it, so they never overlap.

#ifndef CK_VIDEO_CORE_H
#define CK_VIDEO_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ck_video_decode_result_t
{
	/** Decoded, and on its way to the screen. */
	CK_VIDEO_DECODE_OK,
	/** The decoder rejected this frame and can take the next one. */
	CK_VIDEO_DECODE_FAILED,
	/** The decoder no longer works (VideoToolbox invalidated it): it is dropped and made again. */
	CK_VIDEO_DECODE_BROKEN,
} CKVideoDecodeResult;

typedef struct ck_video_decoder_t
{
	/**
	 * Makes the decoder for a stream with this SPS and PPS (without start
	 * codes), or adapts the one it has. Called before the first frame and
	 * whenever they change. Returns false when it cannot; the frames are then
	 * lost until the next try, at the next frame.
	 */
	bool (*configure)(void *user, const uint8_t *sps, size_t sps_size, const uint8_t *pps, size_t pps_size);
	/**
	 * Decodes one frame and shows it, before returning. avcc holds its NAL
	 * units, each after a CK_H264_AVCC_LENGTH_SIZE-byte big-endian length,
	 * without SPS, PPS, access unit delimiters or filler. The buffer is the
	 * sink's and is reused after the call.
	 */
	CKVideoDecodeResult (*decode)(void *user, const uint8_t *avcc, size_t avcc_size);
	/** Lets go of the decoder, if it has one. The next frame goes to configure() first. */
	void (*drop)(void *user);
	/** A monotonic clock in microseconds, to time decode(). */
	uint64_t (*now_us)(void *user);
	void *user;
} CKVideoDecoder;

/** What the sink did since the previous ck_video_core_take_stats(). */
typedef struct ck_video_core_stats_t
{
	/** Frames decode() decoded, and those it rejected. */
	uint32_t frames_decoded;
	uint32_t frames_failed;
	/** Frames that never reached decode(): no SPS and PPS yet, no decoder, or no NAL unit in them. */
	uint32_t frames_skipped;
	/** Frames the core reported lost before the ones it handed over. */
	uint32_t frames_lost;
	/** Time spent in decode(), in microseconds, over frames_decoded + frames_failed. */
	uint64_t decode_us;
	/** The slowest decode() call, in microseconds. */
	uint32_t decode_us_max;
	/** Calls to configure(), and of those, the ones that failed. */
	uint32_t configures;
	uint32_t configure_failures;
	/** Times decode() said the decoder broke. */
	uint32_t decoder_broken;
} CKVideoCoreStats;

typedef struct ck_video_core_t CKVideoCore;

/** The output buffer the sink starts with: a frame up to this size never makes it grow. */
#define CK_VIDEO_CORE_INITIAL_FRAME_SIZE (256 * 1024)

/** decoder is copied. Returns NULL without memory. */
CKVideoCore *ck_video_core_new(const CKVideoDecoder *decoder);

/** Drops the decoder and frees the core. NULL is fine. */
void ck_video_core_free(CKVideoCore *core);

/**
 * What ck_video_sink_sample_cb() does, with the same arguments and the rules
 * of contract C2: a buffer with only the SPS and PPS returns true without
 * decoding, and a frame that could not be decoded returns false.
 */
bool ck_video_core_sample(CKVideoCore *core, const uint8_t *buf, size_t buf_size, int32_t frames_lost, bool frame_recovered);

/** Drops the decoder and forgets the SPS and PPS, e.g. before a new session. */
void ck_video_core_reset(CKVideoCore *core);

/** The counters since the previous call, then resets them. */
void ck_video_core_take_stats(CKVideoCore *core, CKVideoCoreStats *stats);

#ifdef __cplusplus
}
#endif

#endif // CK_VIDEO_CORE_H
