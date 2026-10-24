// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A3.1: H.264 Annex B to AVCC and SPS/PPS extraction, in plain C so it is
// tested on Linux (ios/tests/ck_h264_tests.c). The core hands the video sink
// Annex B (start codes, see CKVideoSink.h); VideoToolbox wants AVCC (each NAL
// unit prefixed by its length) and the SPS and PPS in a
// CMVideoFormatDescription. Nothing here allocates: the caller owns every
// buffer, so the sink can reuse one per session.

#ifndef CK_H264_H
#define CK_H264_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** nal_unit_type values used here (H.264 table 7-1). */
#define CK_H264_NAL_SLICE 1
#define CK_H264_NAL_IDR 5
#define CK_H264_NAL_SEI 6
#define CK_H264_NAL_SPS 7
#define CK_H264_NAL_PPS 8
#define CK_H264_NAL_AUD 9
#define CK_H264_NAL_FILLER 12

/** slice_type values (H.264 table 7-6), modulo 5. */
#define CK_H264_SLICE_P 0
#define CK_H264_SLICE_B 1
#define CK_H264_SLICE_I 2
#define CK_H264_SLICE_SP 3
#define CK_H264_SLICE_SI 4

/** nal_unit_type of a NAL unit, from its first byte. */
#define CK_H264_NAL_TYPE(first_byte) ((unsigned)(first_byte) & 0x1fu)

/** Bit of a nal_unit_type in drop masks and in CKH264FrameInfo.nal_types. */
#define CK_H264_NAL_BIT(type) (UINT32_C(1) << (type))

/**
 * NAL units to leave out of the samples handed to VideoToolbox: the SPS and
 * PPS go into the CMVideoFormatDescription instead, and access unit
 * delimiters and filler data carry no picture data.
 */
#define CK_H264_DROP_FOR_VIDEOTOOLBOX \
	(CK_H264_NAL_BIT(CK_H264_NAL_SPS) | CK_H264_NAL_BIT(CK_H264_NAL_PPS) \
		| CK_H264_NAL_BIT(CK_H264_NAL_AUD) | CK_H264_NAL_BIT(CK_H264_NAL_FILLER))

/** Size of the big-endian AVCC length prefix. The format description must use the same NALUnitHeaderLength. */
#define CK_H264_AVCC_LENGTH_SIZE 4

/** Largest Annex B buffer accepted, far above any frame, so lengths never overflow 32 bits. */
#define CK_H264_MAX_INPUT_SIZE ((size_t)1 << 30)

/** Largest SPS or PPS the store keeps. The PS5 sends a 28-byte SPS and a 4-byte PPS. */
#define CK_H264_PARAM_SET_MAX_SIZE 256

typedef enum
{
	CK_H264_OK = 0,
	/** The input has no start code, or only empty NAL units. */
	CK_H264_ERR_NO_NAL,
	/** The output does not fit; CKH264FrameInfo.avcc_size holds the size needed. */
	CK_H264_ERR_OUT_TOO_SMALL,
	/** in_size is above CK_H264_MAX_INPUT_SIZE. */
	CK_H264_ERR_INPUT_TOO_LARGE
} CKH264Result;

/** The first SPS and PPS of a buffer, without start codes. A missing one has a NULL pointer and size 0. */
typedef struct ck_h264_param_sets_t
{
	const uint8_t *sps;
	size_t sps_size;
	const uint8_t *pps;
	size_t pps_size;
} CKH264ParamSets;

typedef struct ck_h264_frame_info_t
{
	/** Bytes written to out, or on CK_H264_ERR_OUT_TOO_SMALL the bytes needed. */
	size_t avcc_size;
	/** NAL units in the input, and how many of them were written (the rest were dropped). */
	uint32_t nal_count;
	uint32_t nal_written;
	/** CK_H264_NAL_BIT() of every nal_unit_type in the input, e.g. to tell an IDR frame or a header-only buffer. */
	uint32_t nal_types;
	/** Point into the input buffer: copy them (ck_h264_param_store_update) before the buffer goes away. */
	CKH264ParamSets param_sets;
} CKH264FrameInfo;

/**
 * Finds the next NAL unit of an Annex B buffer, starting at *pos (0 on the
 * first call). Sets *nal and *nal_size to the NAL unit without its start code
 * and without trailing zero bytes, moves *pos past it and returns true, or
 * returns false when no NAL unit is left. Bytes before the first start code
 * and empty NAL units are skipped. Emulation prevention bytes stay: AVCC
 * keeps them too.
 */
bool ck_h264_annexb_next_nal(const uint8_t *buf, size_t size, size_t *pos, const uint8_t **nal, size_t *nal_size);

/**
 * Finds the next access unit (one frame, with the SPS, PPS or SEI before it)
 * of an Annex B stream, such as a capture from A2.8, starting at *pos (0 on
 * the first call). Sets *au and *au_size to it, moves *pos to the next one
 * and returns true, or returns false when no NAL unit is left. A new access
 * unit starts at an AUD, SEI, SPS or PPS, or at a slice with
 * first_mb_in_slice 0, once the current one has a slice: H.264 7.4.1.2.3,
 * enough for streams without slices out of order.
 */
bool ck_h264_next_access_unit(const uint8_t *buf, size_t size, size_t *pos, const uint8_t **au, size_t *au_size);

/**
 * slice_type of a slice NAL unit (nal_unit_type 1 or 5, without its start
 * code), modulo 5: one of CK_H264_SLICE_*. Reads the slice header past
 * emulation prevention bytes. Returns -1 for any other NAL unit, or when the
 * header is cut short or out of range.
 */
int ck_h264_slice_type(const uint8_t *nal, size_t nal_size);

/**
 * Upper bound of the AVCC size of any Annex B buffer of annexb_size bytes,
 * for sizing the output once instead of per frame. Each NAL unit has a start
 * code of at least 3 bytes and grows by at most one byte, so the bound is
 * annexb_size + annexb_size / 4. Saturates at SIZE_MAX.
 */
size_t ck_h264_avcc_max_size(size_t annexb_size);

/**
 * Converts an Annex B buffer to AVCC with 4-byte big-endian lengths, leaving
 * out the NAL units whose CK_H264_NAL_BIT(type) is in drop_mask: pass
 * CK_H264_DROP_FOR_VIDEOTOOLBOX for a frame, 0 to keep everything. Writes
 * at most out_capacity bytes to out (which may be NULL when out_capacity is
 * 0) and fills *info when info is not NULL.
 *
 * A buffer whose NAL units are all dropped, such as the profile header the
 * core sends before the first frame, returns CK_H264_OK with nothing written
 * and its SPS and PPS in info->param_sets. On an error the content of out is
 * unspecified.
 */
CKH264Result ck_h264_annexb_to_avcc(const uint8_t *in, size_t in_size, uint32_t drop_mask,
	uint8_t *out, size_t out_capacity, CKH264FrameInfo *info);

/** Finds the first SPS and PPS of an Annex B buffer. Returns true when both are there. */
bool ck_h264_find_param_sets(const uint8_t *buf, size_t size, CKH264ParamSets *sets);

/**
 * Copies of the SPS and PPS the current CMVideoFormatDescription was made
 * from, so they outlive the core's buffers and a repeated SPS and PPS does
 * not rebuild it. Zero it, or call ck_h264_param_store_reset(), before use.
 */
typedef struct ck_h264_param_store_t
{
	uint8_t sps[CK_H264_PARAM_SET_MAX_SIZE];
	size_t sps_size;
	uint8_t pps[CK_H264_PARAM_SET_MAX_SIZE];
	size_t pps_size;
} CKH264ParamStore;

typedef enum
{
	CK_H264_PARAMS_UNCHANGED = 0,
	/** Rebuild the format description from the store. */
	CK_H264_PARAMS_CHANGED,
	/** A set is over CK_H264_PARAM_SET_MAX_SIZE. The store was not touched. */
	CK_H264_PARAMS_TOO_LARGE
} CKH264ParamUpdate;

void ck_h264_param_store_reset(CKH264ParamStore *store);

/**
 * Stores whichever of the SPS and PPS sets has and tells whether the store
 * changed. A buffer with only a new PPS keeps the stored SPS, and the other
 * way around.
 */
CKH264ParamUpdate ck_h264_param_store_update(CKH264ParamStore *store, const CKH264ParamSets *sets);

/** True when the store has both an SPS and a PPS, so a format description can be made. */
bool ck_h264_param_store_ready(const CKH264ParamStore *store);

#ifdef __cplusplus
}
#endif

#endif // CK_H264_H
