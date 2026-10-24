// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A3.2 to A3.5: what CKVideoSink offers beyond contract C2, for the CK
// Video test app (ios/Video/ckvideo) and A7's measurements: everything it
// counts, and failures to simulate. Not a contract: it may change with the
// sink.

#ifndef CK_VIDEO_SINK_STATS_H
#define CK_VIDEO_SINK_STATS_H

#include "CKVideoSink.h"
#include "ck_video_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ck_video_sink_stats_t
{
	CKVideoCoreStats core;
	/** Decoded pictures handed to the layer. */
	uint32_t frames_shown;
	/** Times the layer had failed and was flushed before taking a picture. */
	uint32_t layer_failures;
	/**
	 * The size of the last decoded picture, and of the part of it to show
	 * (its clean rect, from the SPS's cropping), in pixels; 0 before the
	 * first. They stay as they are when the stats are taken.
	 */
	uint32_t image_width;
	uint32_t image_height;
	uint32_t visible_width;
	uint32_t visible_height;
} CKVideoSinkStats;

/**
 * Everything since the previous call to this or to ck_video_sink_take_stats(),
 * then resets it: use one of the two.
 */
void ck_video_sink_take_all_stats(CKVideoSink *sink, CKVideoSinkStats *stats);

/** Makes the next frame fail as asked (ck_video_core_simulate()), to try the recovery. Any thread. */
void ck_video_sink_simulate(CKVideoSink *sink, CKVideoSimulation what);

#ifdef __cplusplus
}
#endif

#endif // CK_VIDEO_SINK_STATS_H
