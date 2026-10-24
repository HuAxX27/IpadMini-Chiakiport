// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A3.2: what CKVideoSink counts beyond ck_video_sink_take_stats() (contract
// C2), for the CK Video test app (ios/Video/ckvideo) and A7's measurements.
// Not a contract: it may change with the sink.

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
} CKVideoSinkStats;

/**
 * Everything since the previous call to this or to ck_video_sink_take_stats(),
 * then resets it: use one of the two.
 */
void ck_video_sink_take_all_stats(CKVideoSink *sink, CKVideoSinkStats *stats);

#ifdef __cplusplus
}
#endif

#endif // CK_VIDEO_SINK_STATS_H
