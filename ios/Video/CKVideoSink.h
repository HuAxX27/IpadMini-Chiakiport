// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Contract C2 (A2 <-> A3): how H.264 leaves the core and reaches VideoToolbox.
// Owner: A3. A change to this file goes in its own PR, approved by A0.

#ifndef CK_VIDEO_SINK_H
#define CK_VIDEO_SINK_H

#include <chiaki/session.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ck_video_sink_t CKVideoSink;

/**
 * Creates the sink. display_layer is the AVSampleBufferDisplayLayer (or the
 * CALayer the sink draws into) that the game screen (A6) owns, passed as an
 * opaque pointer so this header stays plain C. The sink retains it.
 */
CKVideoSink *ck_video_sink_new(void *display_layer);
void ck_video_sink_free(CKVideoSink *sink);

/**
 * Has the signature of ChiakiVideoSampleCallback: hand it to
 * chiaki_session_set_video_sample_cb() with the sink as user.
 *
 * Runs on the core's video thread. buf is one H.264 access unit in Annex B
 * (start codes), except right after a profile switch, when the core first
 * passes the profile header (SPS and PPS) with frames_lost == 0.
 * Return false when the frame could not be decoded: the core then treats it
 * as lost and does not use it as a reference.
 * Must return within one frame time (33 ms at 30 fps) and must not allocate
 * per frame.
 */
bool ck_video_sink_sample_cb(uint8_t *buf, size_t buf_size, int32_t frames_lost, bool frame_recovered, void *user);

/** Drops all decoder state, e.g. before a new session. Main thread only. */
void ck_video_sink_reset(CKVideoSink *sink);

/** Frames decoded and average decode time since the previous call, for CKMetrics. */
void ck_video_sink_take_stats(CKVideoSink *sink, uint32_t *frames_decoded, float *decode_ms_avg);

#ifdef __cplusplus
}
#endif

#endif // CK_VIDEO_SINK_H
