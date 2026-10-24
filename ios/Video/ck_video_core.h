// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A3.2 to A3.5: the plain C half of CKVideoSink (contract C2,
// CKVideoSink.h). It takes what the core hands the sink, turns it into what a
// hardware decoder wants with ck_h264.c (AVCC frames, and the SPS and PPS
// apart), decides when the decoder must be made again and which frames go to
// it, and keeps the counters behind ck_video_sink_take_stats().
//
// Recovery (A3.3): a frame the decoder rejects is lost for the core, which
// points the next frames at older pictures. A decoder that is new, broke, or
// lost its keyframe has no pictures to refer to, so it takes nothing but a
// keyframe (an IDR, or a frame of I slices only); the frames refused
// meanwhile make the core ask the console for one. The decoder sits behind
// CKVideoDecoder: VideoToolbox in CKVideoSink.m, a fake in the Linux tests
// (ios/tests/ck_video_tests.c).
//
// Pacing (A3.5): decode() shows the picture before it returns, so the time
// between two decoded frames is how long the screen held a picture. Frames
// are shown as soon as they arrive, with no buffer to even them out; the
// counters say how often one came late.
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
	/** Frames refused while the decoder waited for a keyframe. */
	uint32_t frames_waiting;
	/** Times the decoder waited for a keyframe and refused frames meanwhile, and the longest wait in microseconds. */
	uint32_t keyframe_waits;
	uint32_t keyframe_wait_us_max;
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
	/** Frames decoded more than CK_VIDEO_LATE_US after the frame before them. */
	uint32_t frames_late;
	/** The longest time between two decoded frames, in microseconds: the picture stood still that long. */
	uint32_t interval_us_max;
} CKVideoCoreStats;

/**
 * A frame later than this after the one before it is late: at 30 fps, one
 * and a half frame times, so the screen held the previous picture for at
 * least one refresh too many, a hitch one can see.
 */
#define CK_VIDEO_LATE_US 50000

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

/** Failures to simulate, for trying the recovery against a real console. */
typedef enum ck_video_simulation_t
{
	/** The decoder rejects the frame (CK_VIDEO_DECODE_FAILED). */
	CK_VIDEO_SIMULATE_LOST_FRAME = 1 << 0,
	/** The decoder breaks on the frame (CK_VIDEO_DECODE_BROKEN), as when VideoToolbox invalidates it. */
	CK_VIDEO_SIMULATE_BROKEN_DECODER = 1 << 1,
} CKVideoSimulation;

/**
 * Makes the next frame that would reach decode() fail as asked, without
 * calling it: the sink then recovers as from a real failure. Any thread.
 */
void ck_video_core_simulate(CKVideoCore *core, CKVideoSimulation what);

/** The counters since the previous call, then resets them. */
void ck_video_core_take_stats(CKVideoCore *core, CKVideoCoreStats *stats);

#ifdef __cplusplus
}
#endif

#endif // CK_VIDEO_CORE_H
