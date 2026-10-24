// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A2.7: what video the console actually sends. The core hands the video
// callback the profile header (SPS and PPS, Annex B) right after a profile
// switch, before the frames; this reads it, so the session can say whether
// the console sent the H.264 it asked for and whether the A5 decodes it.
// Plain C without allocations, tested in ios/tests/core/video_info.c.

#ifndef CK_VIDEO_INFO_H
#define CK_VIDEO_INFO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ck_video_codec_t
{
	CK_VIDEO_CODEC_UNKNOWN,
	CK_VIDEO_CODEC_H264,
	CK_VIDEO_CODEC_H265,
} CKVideoCodec;

typedef struct ck_video_info_t
{
	CKVideoCodec codec;
	/** The rest comes from the H.264 SPS and stays 0 for H.265. */
	uint8_t profile_idc;
	/** The SPS byte with constraint_set0_flag to constraint_set5_flag, highest bit first. */
	uint8_t constraint_flags;
	/** Ten times the level: 31 is level 3.1. */
	uint8_t level_idc;
	/** 1 is 4:2:0. */
	uint8_t chroma_format_idc;
	uint8_t bit_depth_luma;
	uint8_t bit_depth_chroma;
	/** frame_mbs_only_flag is 0: fields or field-frame macroblocks. */
	bool interlaced;
	/** The picture after cropping. */
	uint32_t width;
	uint32_t height;
} CKVideoInfo;

/**
 * Reads the parameter sets a buffer of the video callback starts with (an
 * access unit delimiter or SEI may come first). Returns true and fills *info
 * for an H.264 SPS that parses, or for an H.265 VPS, SPS or PPS (codec only).
 * Returns false for a frame, which starts with a slice, and for a broken SPS.
 * Only looks past the first NAL unit when it is a delimiter or an SEI, so it
 * costs nothing on frames.
 */
bool ck_video_info_parse(const uint8_t *buf, size_t size, CKVideoInfo *info);

/** "High", "Main", "Constrained Baseline"... or "unknown" for a profile without a name here. */
const char *ck_video_info_profile_name(const CKVideoInfo *info);

/**
 * Whether the A5's hardware decoder takes it: H.264 Baseline, Main or High,
 * 8-bit 4:2:0, progressive, level 4.1 at most and 1920x1080 at most.
 */
bool ck_video_info_a5_decodes(const CKVideoInfo *info);

#ifdef __cplusplus
}
#endif

#endif // CK_VIDEO_INFO_H
