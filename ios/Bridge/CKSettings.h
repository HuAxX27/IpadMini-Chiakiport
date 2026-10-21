// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Contract C5 (A6, A7 <-> all): stream settings and the metrics every module reports.
// Owner: A0. A change to this file goes in its own PR.

#ifndef CK_SETTINGS_H
#define CK_SETTINGS_H

#include <chiaki/session.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ck_settings_t
{
	/** 360p, 540p or 720p. 1080p is beyond the A5 and the 1024x768 screen. */
	ChiakiVideoResolutionPreset resolution;
	ChiakiVideoFPSPreset fps;
	/** 0 keeps the core's bitrate for the preset. */
	uint32_t bitrate_kbps;
	// No codec field: it is always CHIAKI_CODEC_H264, the A5 has no HEVC decoder.
} CKSettings;

static inline void ck_settings_set_default(CKSettings *settings)
{
	settings->resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_540p;
	settings->fps = CHIAKI_VIDEO_FPS_PRESET_30;
	settings->bitrate_kbps = 0;
}

/** Filled by CKSession once per second from the video sink, the audio player and the core. */
typedef struct ck_metrics_t
{
	float fps_decoded;
	float decode_ms_avg;
	uint32_t frames_lost;
	uint32_t audio_frames_concealed;
	uint32_t audio_buffered_ms;
	uint32_t bitrate_kbps;
	uint32_t resident_kb;
} CKMetrics;

#ifdef __cplusplus
}
#endif

#endif // CK_SETTINGS_H
