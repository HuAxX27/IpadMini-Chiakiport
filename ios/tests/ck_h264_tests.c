// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Linux tests for the Annex B to AVCC converter in ios/Video/ck_h264.c (A3.1).
// Run them all with ./ios/tests/ck_h264_tests.sh, which also builds them for
// armv7 under qemu and compares the output with ffmpeg's. The binary has
// three modes:
//
//   ck_h264_tests                 unit tests
//   ck_h264_tests check FILE...   checks Annex B captures (.h264), e.g. from A2.8
//   ck_h264_tests avcc IN OUT     writes IN as AVCC keeping every NAL unit, to compare with ffmpeg

#include "Video/ck_h264.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int failures;

#define CHECK(cond) \
	do \
	{ \
		if(!(cond)) \
		{ \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
			failures++; \
		} \
	} while(0)

// The PS5 stream header from VitaRPS5's first hardware log (test/sps_probe_tests.c):
// a 4-byte start code and a 28-byte SPS, then a 4-byte start code and a 4-byte PPS.
static const uint8_t PS5_HEADER[] = {
	0x00, 0x00, 0x00, 0x01, 0x67, 0x4d, 0x40, 0x20, 0x91, 0x8a, 0x03, 0xc0, 0x45, 0xfb,
	0xc0, 0x5a, 0x80, 0x80, 0x80, 0xa0, 0x00, 0x00, 0xfa, 0x40, 0x00, 0x75, 0x30, 0x11,
	0x34, 0x38, 0x74, 0x54, 0x00, 0x00, 0x00, 0x01, 0x68, 0xee, 0x3c, 0x80};

#define TYPE_BIT(type) CK_H264_NAL_BIT(type)
#define SLICE_BITS (TYPE_BIT(CK_H264_NAL_SLICE) | TYPE_BIT(CK_H264_NAL_IDR))

// --- NAL unit lists -------------------------------------------------------

typedef struct
{
	const uint8_t *data;
	size_t size;
} Nal;

typedef struct
{
	Nal *nal;
	size_t count;
	size_t capacity;
} NalList;

static void list_add(NalList *list, const uint8_t *data, size_t size)
{
	if(list->count == list->capacity)
	{
		list->capacity = list->capacity ? list->capacity * 2 : 64;
		list->nal = realloc(list->nal, list->capacity * sizeof(Nal));
		if(!list->nal)
		{
			fprintf(stderr, "out of memory\n");
			exit(2);
		}
	}
	list->nal[list->count].data = data;
	list->nal[list->count].size = size;
	list->count++;
}

static void list_free(NalList *list)
{
	free(list->nal);
	memset(list, 0, sizeof(*list));
}

static bool lists_equal(const NalList *a, const NalList *b)
{
	if(a->count != b->count)
		return false;
	for(size_t i = 0; i < a->count; i++)
	{
		if(a->nal[i].size != b->nal[i].size || memcmp(a->nal[i].data, b->nal[i].data, a->nal[i].size) != 0)
			return false;
	}
	return true;
}

static bool is_start_code(const uint8_t *buf, size_t size, size_t i)
{
	return i + 3 <= size && !buf[i] && !buf[i + 1] && buf[i + 2] == 1;
}

// A literal, byte-by-byte reading of H.264 Annex B, independent of the
// converter's faster scanner: NAL units sit between 00 00 01 start codes and
// lose their trailing zero bytes; anything before the first start code and
// empty NAL units are ignored.
static void reference_parse(const uint8_t *buf, size_t size, NalList *list)
{
	list->count = 0;
	size_t i = 0;
	while(i < size && !is_start_code(buf, size, i))
		i++;
	while(i < size)
	{
		size_t start = i + 3;
		size_t end = start;
		while(end < size && !is_start_code(buf, size, end))
			end++;
		size_t last = end;
		while(last > start && !buf[last - 1])
			last--;
		if(last > start)
			list_add(list, buf + start, last - start);
		i = end;
	}
}

// Splits AVCC into its NAL units. False when the lengths do not add up to
// size exactly or a NAL unit is empty or ends in a zero byte.
static bool avcc_parse(const uint8_t *buf, size_t size, NalList *list)
{
	list->count = 0;
	size_t i = 0;
	while(i < size)
	{
		if(size - i < CK_H264_AVCC_LENGTH_SIZE)
			return false;
		size_t n = ((size_t)buf[i] << 24) | ((size_t)buf[i + 1] << 16) | ((size_t)buf[i + 2] << 8) | buf[i + 3];
		i += CK_H264_AVCC_LENGTH_SIZE;
		if(n == 0 || n > size - i || !buf[i + n - 1])
			return false;
		list_add(list, buf + i, n);
		i += n;
	}
	return true;
}

static uint32_t list_types(const NalList *list)
{
	uint32_t types = 0;
	for(size_t i = 0; i < list->count; i++)
		types |= TYPE_BIT(CK_H264_NAL_TYPE(list->nal[i].data[0]));
	return types;
}

// --- Fixed cases ------------------------------------------------------------

static void test_ps5_header(void)
{
	CKH264ParamSets sets;
	CHECK(ck_h264_find_param_sets(PS5_HEADER, sizeof(PS5_HEADER), &sets));
	CHECK(sets.sps == PS5_HEADER + 4 && sets.sps_size == 28);
	CHECK(sets.pps == PS5_HEADER + 36 && sets.pps_size == 4);

	// Keeping everything, both 4-byte start codes become 4-byte lengths.
	uint8_t out[64];
	CKH264FrameInfo info;
	CHECK(ck_h264_annexb_to_avcc(PS5_HEADER, sizeof(PS5_HEADER), 0, out, sizeof(out), &info) == CK_H264_OK);
	CHECK(info.avcc_size == sizeof(PS5_HEADER));
	CHECK(info.nal_count == 2 && info.nal_written == 2);
	static const uint8_t len_sps[] = {0, 0, 0, 28};
	static const uint8_t len_pps[] = {0, 0, 0, 4};
	CHECK(memcmp(out, len_sps, 4) == 0 && memcmp(out + 4, PS5_HEADER + 4, 28) == 0);
	CHECK(memcmp(out + 32, len_pps, 4) == 0 && memcmp(out + 36, PS5_HEADER + 36, 4) == 0);

	// What the sink does with it: nothing to decode, and the store fills up.
	CHECK(ck_h264_annexb_to_avcc(PS5_HEADER, sizeof(PS5_HEADER), CK_H264_DROP_FOR_VIDEOTOOLBOX, out, sizeof(out), &info) == CK_H264_OK);
	CHECK(info.avcc_size == 0 && info.nal_written == 0 && info.nal_count == 2);
	CHECK(info.nal_types == (TYPE_BIT(CK_H264_NAL_SPS) | TYPE_BIT(CK_H264_NAL_PPS)));
	CHECK(info.param_sets.sps == sets.sps && info.param_sets.pps == sets.pps);

	CKH264ParamStore store;
	ck_h264_param_store_reset(&store);
	CHECK(!ck_h264_param_store_ready(&store));
	CHECK(ck_h264_param_store_update(&store, &info.param_sets) == CK_H264_PARAMS_CHANGED);
	CHECK(ck_h264_param_store_ready(&store));
	CHECK(store.sps_size == 28 && memcmp(store.sps, PS5_HEADER + 4, 28) == 0);
	CHECK(store.pps_size == 4 && memcmp(store.pps, PS5_HEADER + 36, 4) == 0);
	CHECK(ck_h264_param_store_update(&store, &info.param_sets) == CK_H264_PARAMS_UNCHANGED);
}

static void test_start_codes_and_zeros(void)
{
	static const uint8_t in[] = {
		0xff, 0x00, 0x01,                         // junk before the first start code
		0x00, 0x00, 0x01, 0x09, 0xf0,             // AUD, 3-byte start code
		0x00, 0x00, 0x00, 0x01, 0x06, 0x05, 0x11, 0x80, // SEI, 4-byte start code
		0x00, 0x00, 0x01, 0x65, 0x88, 0x80, 0x00, 0x00, // IDR slice, then trailing zeros
		0x00, 0x00, 0x00, 0x00, 0x01, 0x41, 0x9a, 0x80, // P slice after extra zeros
		0x00, 0x00};                              // trailing zeros at the end
	static const uint8_t expected[] = {
		0x00, 0x00, 0x00, 0x02, 0x09, 0xf0,
		0x00, 0x00, 0x00, 0x04, 0x06, 0x05, 0x11, 0x80,
		0x00, 0x00, 0x00, 0x03, 0x65, 0x88, 0x80,
		0x00, 0x00, 0x00, 0x03, 0x41, 0x9a, 0x80};
	uint8_t out[sizeof(expected) + 8];
	CKH264FrameInfo info;
	CHECK(ck_h264_annexb_to_avcc(in, sizeof(in), 0, out, sizeof(out), &info) == CK_H264_OK);
	CHECK(info.avcc_size == sizeof(expected) && memcmp(out, expected, sizeof(expected)) == 0);
	CHECK(info.nal_count == 4 && info.nal_written == 4);
	CHECK(info.nal_types == (TYPE_BIT(9) | TYPE_BIT(6) | TYPE_BIT(5) | TYPE_BIT(1)));
	CHECK(!info.param_sets.sps && !info.param_sets.pps && info.param_sets.sps_size == 0);

	// The iterator walks the same NAL units and ends at the end of the buffer.
	size_t pos = 0;
	const uint8_t *nal;
	size_t nal_size;
	size_t count = 0;
	while(ck_h264_annexb_next_nal(in, sizeof(in), &pos, &nal, &nal_size))
		count++;
	CHECK(count == 4 && pos == sizeof(in));
	CHECK(!ck_h264_annexb_next_nal(in, sizeof(in), &pos, &nal, &nal_size));
}

static void test_emulation_prevention_is_kept(void)
{
	static const uint8_t in[] = {0x00, 0x00, 0x01, 0x65, 0x00, 0x00, 0x03, 0x01, 0x00, 0x00, 0x03, 0x00, 0x80};
	uint8_t out[32];
	CKH264FrameInfo info;
	CHECK(ck_h264_annexb_to_avcc(in, sizeof(in), 0, out, sizeof(out), &info) == CK_H264_OK);
	CHECK(info.avcc_size == 4 + 10);
	CHECK(out[3] == 10 && memcmp(out + 4, in + 3, 10) == 0);
}

static void test_empty_nal_units_are_skipped(void)
{
	static const uint8_t in[] = {0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x65, 0x80};
	uint8_t out[16];
	CKH264FrameInfo info;
	CHECK(ck_h264_annexb_to_avcc(in, sizeof(in), 0, out, sizeof(out), &info) == CK_H264_OK);
	CHECK(info.nal_count == 1 && info.avcc_size == 6);
	CHECK(out[3] == 2 && out[4] == 0x65 && out[5] == 0x80);
}

static void test_no_nal_unit(void)
{
	static const uint8_t no_start_code[] = {0x65, 0x88, 0x84, 0x00, 0x01, 0x80};
	static const uint8_t start_code_only[] = {0x00, 0x00, 0x00, 0x01};
	static const uint8_t zeros_only[] = {0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
	const struct
	{
		const uint8_t *buf;
		size_t size;
	} cases[] = {{NULL, 0}, {NULL, 16}, {no_start_code, sizeof(no_start_code)},
		{start_code_only, sizeof(start_code_only)}, {zeros_only, sizeof(zeros_only)}};
	for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
	{
		uint8_t out[16];
		CKH264FrameInfo info;
		memset(&info, 0xaa, sizeof(info));
		CHECK(ck_h264_annexb_to_avcc(cases[i].buf, cases[i].size, 0, out, sizeof(out), &info) == CK_H264_ERR_NO_NAL);
		CHECK(info.avcc_size == 0 && info.nal_count == 0 && info.nal_types == 0);
		CKH264ParamSets sets;
		CHECK(!ck_h264_find_param_sets(cases[i].buf, cases[i].size, &sets));
	}
	// info is optional.
	CHECK(ck_h264_annexb_to_avcc(no_start_code, sizeof(no_start_code), 0, NULL, 0, NULL) == CK_H264_ERR_NO_NAL);
}

static void test_output_capacity(void)
{
	static const uint8_t in[] = {0x00, 0x00, 0x01, 0x65, 0x88, 0x80, 0x00, 0x00, 0x01, 0x65, 0x99, 0x77, 0x80};
	const size_t needed = 4 + 3 + 4 + 4;

	// A sizing pass without a buffer.
	CKH264FrameInfo info;
	CHECK(ck_h264_annexb_to_avcc(in, sizeof(in), 0, NULL, 0, &info) == CK_H264_ERR_OUT_TOO_SMALL);
	CHECK(info.avcc_size == needed && info.nal_written == 2);
	// A NULL out counts as no room, whatever the capacity says.
	CHECK(ck_h264_annexb_to_avcc(in, sizeof(in), 0, NULL, 64, &info) == CK_H264_ERR_OUT_TOO_SMALL);

	// One byte short: an error with the size needed, and nothing written past the capacity.
	uint8_t out[32];
	memset(out, 0xaa, sizeof(out));
	CHECK(ck_h264_annexb_to_avcc(in, sizeof(in), 0, out, needed - 1, &info) == CK_H264_ERR_OUT_TOO_SMALL);
	CHECK(info.avcc_size == needed);
	for(size_t i = needed - 1; i < sizeof(out); i++)
		CHECK(out[i] == 0xaa);

	// Exactly enough.
	CHECK(ck_h264_annexb_to_avcc(in, sizeof(in), 0, out, needed, &info) == CK_H264_OK);
	CHECK(info.avcc_size == needed && out[needed] == 0xaa);

	// With every unit dropped, no buffer is needed.
	CHECK(ck_h264_annexb_to_avcc(in, sizeof(in), TYPE_BIT(CK_H264_NAL_IDR), NULL, 0, &info) == CK_H264_OK);
	CHECK(info.avcc_size == 0 && info.nal_written == 0);
}

static void test_max_size(void)
{
	// The worst case: one-byte NAL units behind 3-byte start codes grow by a quarter.
	enum { UNITS = 64 };
	uint8_t in[UNITS * 4];
	for(size_t i = 0; i < UNITS; i++)
	{
		in[i * 4] = 0x00;
		in[i * 4 + 1] = 0x00;
		in[i * 4 + 2] = 0x01;
		in[i * 4 + 3] = 0x09;
	}
	uint8_t out[UNITS * 5];
	CKH264FrameInfo info;
	CHECK(ck_h264_avcc_max_size(sizeof(in)) == sizeof(out));
	CHECK(ck_h264_annexb_to_avcc(in, sizeof(in), 0, out, sizeof(out), &info) == CK_H264_OK);
	CHECK(info.avcc_size == sizeof(out) && info.nal_count == UNITS);

	CHECK(ck_h264_avcc_max_size(0) == 0);
	CHECK(ck_h264_avcc_max_size(3) == 3);
	CHECK(ck_h264_avcc_max_size(SIZE_MAX) == SIZE_MAX);
	CHECK(ck_h264_avcc_max_size(SIZE_MAX - 1) == SIZE_MAX);
}

static void test_large_nal_length(void)
{
	// Over 16 MiB, so every byte of the big-endian length is used.
	const size_t nal_size = 0x01020304;
	uint8_t *in = malloc(3 + nal_size);
	uint8_t *out = malloc(4 + nal_size);
	CHECK(in && out);
	if(!in || !out)
	{
		free(in);
		free(out);
		return;
	}
	in[0] = 0x00;
	in[1] = 0x00;
	in[2] = 0x01;
	memset(in + 3, 0x5a, nal_size);
	in[3] = 0x65;
	CKH264FrameInfo info;
	CHECK(ck_h264_annexb_to_avcc(in, 3 + nal_size, 0, out, 4 + nal_size, &info) == CK_H264_OK);
	CHECK(info.avcc_size == 4 + nal_size);
	CHECK(out[0] == 0x01 && out[1] == 0x02 && out[2] == 0x03 && out[3] == 0x04);
	CHECK(memcmp(out + 4, in + 3, nal_size) == 0);
	free(in);
	free(out);
}

static void test_input_too_large(void)
{
	// Rejected before anything is read, so a small buffer is enough to test it.
	uint8_t in[4] = {0x00, 0x00, 0x01, 0x65};
	CKH264FrameInfo info;
	CHECK(ck_h264_annexb_to_avcc(in, CK_H264_MAX_INPUT_SIZE + 1, 0, NULL, 0, &info) == CK_H264_ERR_INPUT_TOO_LARGE);
	CHECK(info.avcc_size == 0 && info.nal_count == 0);
}

static void test_frame_for_videotoolbox(void)
{
	// An IDR access unit with everything an encoder may put around the slices.
	static const uint8_t in[] = {
		0x00, 0x00, 0x00, 0x01, 0x09, 0x10,             // AUD
		0x00, 0x00, 0x00, 0x01, 0x67, 0x4d, 0x40, 0x20, // SPS
		0x00, 0x00, 0x00, 0x01, 0x68, 0xee, 0x3c, 0x80, // PPS
		0x00, 0x00, 0x01, 0x06, 0x05, 0x01, 0x80,       // SEI
		0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x21,       // IDR, first slice
		0x00, 0x00, 0x01, 0x65, 0x01, 0x9c, 0x80,       // IDR, second slice
		0x00, 0x00, 0x01, 0x0c, 0xff, 0xff, 0x80,       // filler data
		0x00, 0x00, 0x01, 0x68, 0xce, 0x3c, 0x80,       // a second PPS and SPS: not the ones reported
		0x00, 0x00, 0x01, 0x67, 0x64, 0x00, 0x1f};
	static const uint8_t expected[] = {
		0x00, 0x00, 0x00, 0x04, 0x06, 0x05, 0x01, 0x80,
		0x00, 0x00, 0x00, 0x04, 0x65, 0x88, 0x84, 0x21,
		0x00, 0x00, 0x00, 0x04, 0x65, 0x01, 0x9c, 0x80};
	uint8_t out[sizeof(in) * 2];
	CKH264FrameInfo info;
	CHECK(ck_h264_annexb_to_avcc(in, sizeof(in), CK_H264_DROP_FOR_VIDEOTOOLBOX, out, sizeof(out), &info) == CK_H264_OK);
	CHECK(info.avcc_size == sizeof(expected) && memcmp(out, expected, sizeof(expected)) == 0);
	CHECK(info.nal_count == 9 && info.nal_written == 3);
	CHECK(info.nal_types == (TYPE_BIT(9) | TYPE_BIT(7) | TYPE_BIT(8) | TYPE_BIT(6) | TYPE_BIT(5) | TYPE_BIT(12)));
	CHECK(info.param_sets.sps == in + 10 && info.param_sets.sps_size == 4);
	CHECK(info.param_sets.pps == in + 18 && info.param_sets.pps_size == 4);
}

static void test_param_store(void)
{
	static const uint8_t sps[] = {0x67, 0x4d, 0x40, 0x20};
	static const uint8_t pps_a[] = {0x68, 0xee, 0x3c, 0x80};
	static const uint8_t pps_b[] = {0x68, 0xce, 0x3c, 0x80};
	static const uint8_t pps_c[] = {0x68, 0xce, 0x3c, 0x81, 0x80};
	static uint8_t big[CK_H264_PARAM_SET_MAX_SIZE + 1];
	memset(big, 0x67, sizeof(big));

	CKH264ParamStore store;
	memset(&store, 0, sizeof(store)); // zeroing works like ck_h264_param_store_reset()
	CKH264ParamSets sets = {sps, sizeof(sps), NULL, 0};
	CHECK(ck_h264_param_store_update(&store, &sets) == CK_H264_PARAMS_CHANGED);
	CHECK(!ck_h264_param_store_ready(&store));

	sets = (CKH264ParamSets){NULL, 0, pps_a, sizeof(pps_a)};
	CHECK(ck_h264_param_store_update(&store, &sets) == CK_H264_PARAMS_CHANGED);
	CHECK(ck_h264_param_store_ready(&store));

	// Repeats, and buffers without parameter sets, change nothing.
	sets = (CKH264ParamSets){sps, sizeof(sps), pps_a, sizeof(pps_a)};
	CHECK(ck_h264_param_store_update(&store, &sets) == CK_H264_PARAMS_UNCHANGED);
	sets = (CKH264ParamSets){NULL, 0, NULL, 0};
	CHECK(ck_h264_param_store_update(&store, &sets) == CK_H264_PARAMS_UNCHANGED);

	// A new PPS of the same size, then of another size, keeps the SPS.
	sets = (CKH264ParamSets){NULL, 0, pps_b, sizeof(pps_b)};
	CHECK(ck_h264_param_store_update(&store, &sets) == CK_H264_PARAMS_CHANGED);
	CHECK(memcmp(store.pps, pps_b, sizeof(pps_b)) == 0);
	sets = (CKH264ParamSets){NULL, 0, pps_c, sizeof(pps_c)};
	CHECK(ck_h264_param_store_update(&store, &sets) == CK_H264_PARAMS_CHANGED);
	CHECK(store.pps_size == sizeof(pps_c) && store.sps_size == sizeof(sps) && memcmp(store.sps, sps, sizeof(sps)) == 0);
	// A PPS that is a prefix of the stored one is still a change.
	sets = (CKH264ParamSets){NULL, 0, pps_c, sizeof(pps_c) - 1};
	CHECK(ck_h264_param_store_update(&store, &sets) == CK_H264_PARAMS_CHANGED);
	CHECK(store.pps_size == sizeof(pps_c) - 1);
	sets = (CKH264ParamSets){NULL, 0, pps_c, sizeof(pps_c)};
	CHECK(ck_h264_param_store_update(&store, &sets) == CK_H264_PARAMS_CHANGED);

	// Too large: nothing is stored, not even the set that fits.
	sets = (CKH264ParamSets){big, sizeof(big), pps_a, sizeof(pps_a)};
	CHECK(ck_h264_param_store_update(&store, &sets) == CK_H264_PARAMS_TOO_LARGE);
	CHECK(store.sps_size == sizeof(sps) && store.pps_size == sizeof(pps_c) && memcmp(store.pps, pps_c, sizeof(pps_c)) == 0);
	sets = (CKH264ParamSets){big, CK_H264_PARAM_SET_MAX_SIZE, NULL, 0};
	CHECK(ck_h264_param_store_update(&store, &sets) == CK_H264_PARAMS_CHANGED);
	CHECK(store.sps_size == CK_H264_PARAM_SET_MAX_SIZE);

	ck_h264_param_store_reset(&store);
	CHECK(!ck_h264_param_store_ready(&store) && store.sps_size == 0 && store.pps_size == 0);
}

// --- Randomized cases -------------------------------------------------------

static uint32_t rng_state = 0x2545f491u;

static uint32_t rng(void)
{
	// xorshift32: deterministic, so a failure reproduces.
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return rng_state;
}

// Mostly the bytes that matter to start codes and emulation prevention.
static uint8_t rng_byte(void)
{
	static const uint8_t interesting[] = {0x00, 0x00, 0x00, 0x01, 0x02, 0x03};
	uint32_t r = rng();
	if(r % 3 == 0)
		return interesting[(r >> 8) % sizeof(interesting)];
	return (uint8_t)(r >> 16);
}

// Builds a valid NAL unit the way an encoder does: header byte, payload,
// stop bit, then emulation prevention so no 00 00 0x shows up inside.
static size_t make_nal(uint8_t *out, size_t payload_size)
{
	static const uint8_t types[] = {1, 5, 6, 7, 8, 9, 12};
	uint8_t raw[4096];
	raw[0] = (uint8_t)(((rng() % 4) << 5) | types[rng() % sizeof(types)]);
	for(size_t i = 1; i <= payload_size; i++)
		raw[i] = rng_byte();
	raw[payload_size + 1] = 0x80;
	size_t raw_size = payload_size + 2;

	size_t n = 0;
	unsigned zeros = 0;
	for(size_t i = 0; i < raw_size; i++)
	{
		if(zeros >= 2 && raw[i] <= 3)
		{
			out[n++] = 0x03;
			zeros = 0;
		}
		out[n++] = raw[i];
		zeros = raw[i] ? 0 : zeros + 1;
	}
	return n;
}

static void test_access_units(void)
{
	// SPS, PPS and a two-slice IDR frame; a P frame; an AUD and a P frame of
	// two slices, the second with first_mb_in_slice 1 (0x40: ue(v) "010").
	static const uint8_t stream[] = {
		0x00, 0x00, 0x00, 0x01, 0x67, 0x4d, 0x40, 0x20,
		0x00, 0x00, 0x01, 0x68, 0xee, 0x3c, 0x80,
		0x00, 0x00, 0x01, 0x65, 0x88, 0x11,
		0x00, 0x00, 0x01, 0x65, 0x40, 0x22,
		0x00, 0x00, 0x01, 0x41, 0x9a, 0x33,
		0x00, 0x00, 0x01, 0x09, 0x10,
		0x00, 0x00, 0x01, 0x41, 0x9a, 0x44,
		0x00, 0x00, 0x01, 0x41, 0x40, 0x55};
	static const size_t expected[][2] = { { 0, 27 }, { 27, 6 }, { 33, 17 } };
	size_t pos = 0;
	const uint8_t *au = NULL;
	size_t au_size = 0;
	for(size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++)
	{
		CHECK(ck_h264_next_access_unit(stream, sizeof(stream), &pos, &au, &au_size));
		CHECK(au == stream + expected[i][0] && au_size == expected[i][1]);
	}
	CHECK(!ck_h264_next_access_unit(stream, sizeof(stream), &pos, &au, &au_size));
	CHECK(pos == sizeof(stream));

	// Only the SPS and PPS, as the core sends them first: one access unit.
	pos = 0;
	CHECK(ck_h264_next_access_unit(PS5_HEADER, sizeof(PS5_HEADER), &pos, &au, &au_size));
	CHECK(au == PS5_HEADER && au_size == sizeof(PS5_HEADER));
	CHECK(!ck_h264_next_access_unit(PS5_HEADER, sizeof(PS5_HEADER), &pos, &au, &au_size));

	// No start code, or nothing at all.
	static const uint8_t junk[] = { 0x12, 0x34, 0x00, 0x00, 0x02 };
	pos = 0;
	CHECK(!ck_h264_next_access_unit(junk, sizeof(junk), &pos, &au, &au_size));
	pos = 0;
	CHECK(!ck_h264_next_access_unit(junk, 0, &pos, &au, &au_size));
	CHECK(!ck_h264_next_access_unit(NULL, 4, &pos, &au, &au_size));
}

// Writes bits into a NAL unit's payload, adding emulation prevention bytes
// the way an encoder must, so the reader has to skip them.
typedef struct
{
	uint8_t buf[64];
	size_t size;
	unsigned bits;
	uint8_t cur;
	unsigned zeros;
} BitWriter;

static void put_byte(BitWriter *w, uint8_t byte)
{
	if(w->zeros >= 2 && byte <= 3)
	{
		w->buf[w->size++] = 3;
		w->zeros = 0;
	}
	w->buf[w->size++] = byte;
	w->zeros = byte ? 0 : w->zeros + 1;
}

static void put_bit(BitWriter *w, unsigned bit)
{
	w->cur = (uint8_t)((w->cur << 1) | (bit & 1));
	if(++w->bits == 8)
	{
		put_byte(w, w->cur);
		w->bits = 0;
		w->cur = 0;
	}
}

static void put_ue(BitWriter *w, uint32_t value)
{
	uint64_t v = (uint64_t)value + 1;
	int len = 0;
	while((v >> len) > 1)
		len++;
	for(int i = 0; i < len; i++)
		put_bit(w, 0);
	for(int i = len; i >= 0; i--)
		put_bit(w, (unsigned)(v >> i) & 1);
}

// A slice NAL unit: header byte, first_mb_in_slice, slice_type, then a 1 and
// zeros up to the byte (enough for the reader) and one more payload byte.
static size_t make_slice(BitWriter *w, uint8_t header, uint32_t first_mb, uint32_t slice_type)
{
	memset(w, 0, sizeof(*w));
	w->buf[w->size++] = header;
	put_ue(w, first_mb);
	put_ue(w, slice_type);
	put_bit(w, 1);
	while(w->bits)
		put_bit(w, 0);
	put_byte(w, 0x5a);
	return w->size;
}

static bool has_emulation_prevention(const uint8_t *buf, size_t size)
{
	for(size_t i = 2; i < size; i++)
		if(!buf[i - 2] && !buf[i - 1] && buf[i] == 3)
			return true;
	return false;
}

static void test_slice_type(void)
{
	// The PS5's IDR frame starts with an I slice (slice_type 7), its P frames with P slices (5).
	static const uint8_t idr[] = { 0x65, 0x88, 0x84 };
	static const uint8_t p[] = { 0x41, 0x9a, 0x02 };
	CHECK(ck_h264_slice_type(idr, sizeof(idr)) == CK_H264_SLICE_I);
	CHECK(ck_h264_slice_type(p, sizeof(p)) == CK_H264_SLICE_P);

	// Every slice_type with first_mb_in_slice small and large; the largest
	// make the writer add emulation prevention bytes the reader must skip.
	static const uint32_t first_mbs[] = { 0, 1, 59, 2039, 65535, (1u << 23) - 1, (1u << 24) + 5, UINT32_MAX - 1 };
	bool saw_emulation = false;
	for(size_t i = 0; i < sizeof(first_mbs) / sizeof(first_mbs[0]); i++)
	{
		for(uint32_t type = 0; type <= 9; type++)
		{
			BitWriter w;
			size_t size = make_slice(&w, type % 2 ? 0x41 : 0x65, first_mbs[i], type);
			saw_emulation |= has_emulation_prevention(w.buf, size);
			CHECK(ck_h264_slice_type(w.buf, size) == (int)(type % 5));
			// Cut short, the header cannot be read.
			for(size_t cut = 0; cut + 1 < size; cut++)
			{
				int got = ck_h264_slice_type(w.buf, cut);
				CHECK(got == -1 || got == (int)(type % 5));
			}
		}
	}
	CHECK(saw_emulation);

	// slice_type above 9, and NAL units that are not slices.
	BitWriter w;
	size_t size = make_slice(&w, 0x41, 0, 10);
	CHECK(ck_h264_slice_type(w.buf, size) == -1);
	static const uint8_t sps[] = { 0x67, 0x88, 0x84 };
	CHECK(ck_h264_slice_type(sps, sizeof(sps)) == -1);
	CHECK(ck_h264_slice_type(idr, 1) == -1);
	CHECK(ck_h264_slice_type(NULL, 3) == -1);
	// 32 leading zeros is not a ue(v) of 32 bits.
	static const uint8_t too_long[] = { 0x41, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x01, 0xff };
	CHECK(ck_h264_slice_type(too_long, sizeof(too_long)) == -1);
}

static void test_random_streams_round_trip(void)
{
	enum { ROUNDS = 3000, MAX_UNITS = 12 };
	static uint8_t nal_pool[MAX_UNITS][8192];
	static uint8_t annexb[MAX_UNITS * 8200 + 64];
	static uint8_t avcc[sizeof(annexb) * 2];
	static const uint32_t masks[] = {0, CK_H264_DROP_FOR_VIDEOTOOLBOX, TYPE_BIT(1) | TYPE_BIT(6), UINT32_MAX};
	NalList expected = {0}, kept = {0}, reference = {0}, got = {0};

	for(int round = 0; round < ROUNDS; round++)
	{
		size_t units = 1 + rng() % MAX_UNITS;
		size_t len = 0;
		// Junk without start codes before the first one, sometimes.
		if(rng() % 4 == 0)
		{
			size_t junk = 1 + rng() % 5;
			for(size_t i = 0; i < junk; i++)
				annexb[len++] = (uint8_t)(1 + rng() % 255);
		}
		expected.count = 0;
		for(size_t u = 0; u < units; u++)
		{
			size_t payload = rng() % 8 == 0 ? rng() % 4000 : rng() % 64;
			size_t nal_size = make_nal(nal_pool[u], payload);
			size_t zeros = rng() % 4 == 0 ? rng() % 4 : 0; // trailing_zero_8bits, or a 4-byte start code
			for(size_t i = 0; i < zeros; i++)
				annexb[len++] = 0x00;
			annexb[len++] = 0x00;
			annexb[len++] = 0x00;
			annexb[len++] = 0x01;
			memcpy(annexb + len, nal_pool[u], nal_size);
			len += nal_size;
			list_add(&expected, nal_pool[u], nal_size);
		}
		size_t tail = rng() % 3;
		for(size_t i = 0; i < tail; i++)
			annexb[len++] = 0x00;

		reference_parse(annexb, len, &reference);
		CHECK(lists_equal(&reference, &expected));

		uint32_t mask = masks[rng() % (sizeof(masks) / sizeof(masks[0]))];
		kept.count = 0;
		for(size_t i = 0; i < expected.count; i++)
		{
			if(!(mask & TYPE_BIT(CK_H264_NAL_TYPE(expected.nal[i].data[0]))))
				list_add(&kept, expected.nal[i].data, expected.nal[i].size);
		}

		CKH264FrameInfo info;
		size_t capacity = ck_h264_avcc_max_size(len);
		CHECK(capacity <= sizeof(avcc));
		CHECK(ck_h264_annexb_to_avcc(annexb, len, mask, avcc, capacity, &info) == CK_H264_OK);
		CHECK(info.nal_count == expected.count && info.nal_written == kept.count);
		CHECK(info.nal_types == list_types(&expected));
		CHECK(avcc_parse(avcc, info.avcc_size, &got));
		CHECK(lists_equal(&got, &kept));
		if(failures)
		{
			fprintf(stderr, "random round trip failed in round %d\n", round);
			break;
		}
	}
	list_free(&expected);
	list_free(&kept);
	list_free(&reference);
	list_free(&got);
}

static void test_random_bytes_against_reference(void)
{
	enum { ROUNDS = 50000, MAX_LEN = 96 };
	uint8_t in[MAX_LEN];
	uint8_t out[MAX_LEN + MAX_LEN / 4];
	NalList reference = {0}, scanned = {0}, got = {0};

	for(int round = 0; round < ROUNDS; round++)
	{
		size_t len = rng() % (MAX_LEN + 1);
		for(size_t i = 0; i < len; i++)
			in[i] = rng_byte();

		reference_parse(in, len, &reference);
		scanned.count = 0;
		size_t pos = 0;
		const uint8_t *nal;
		size_t nal_size;
		while(ck_h264_annexb_next_nal(in, len, &pos, &nal, &nal_size))
			list_add(&scanned, nal, nal_size);
		CHECK(lists_equal(&scanned, &reference));

		CKH264FrameInfo info;
		CKH264Result r = ck_h264_annexb_to_avcc(in, len, 0, out, ck_h264_avcc_max_size(len), &info);
		CHECK(r == (reference.count ? CK_H264_OK : CK_H264_ERR_NO_NAL));
		CHECK(info.avcc_size <= ck_h264_avcc_max_size(len));
		CHECK(avcc_parse(out, info.avcc_size, &got));
		CHECK(lists_equal(&got, &reference));
		if(failures)
		{
			fprintf(stderr, "random bytes failed in round %d\n", round);
			break;
		}
	}
	list_free(&reference);
	list_free(&scanned);
	list_free(&got);
}

// --- Captures ---------------------------------------------------------------

static uint8_t *read_file(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	if(!f)
		return NULL;
	size_t capacity = 1 << 20;
	size_t len = 0;
	uint8_t *buf = malloc(capacity);
	while(buf)
	{
		len += fread(buf + len, 1, capacity - len, f);
		if(len < capacity)
			break;
		capacity *= 2;
		uint8_t *bigger = realloc(buf, capacity);
		if(!bigger)
			free(buf);
		buf = bigger;
	}
	bool ok = buf && !ferror(f);
	fclose(f);
	if(!ok)
	{
		free(buf);
		return NULL;
	}
	*size = len;
	return buf;
}

// Starts a new access unit, roughly as H.264 7.4.1.2.3 says: at an AUD, SEI,
// SPS or PPS, or at a slice with first_mb_in_slice 0, once the current one
// already has a slice.
static bool starts_access_unit(const uint8_t *nal, size_t size)
{
	unsigned type = CK_H264_NAL_TYPE(nal[0]);
	if(type >= CK_H264_NAL_SEI && type <= CK_H264_NAL_AUD)
		return true;
	// first_mb_in_slice is the first ue(v) of the slice header; 0 is coded as a single 1 bit.
	return (type == CK_H264_NAL_SLICE || type == CK_H264_NAL_IDR) && size > 1 && (nal[1] & 0x80);
}

static int check_capture(const char *path)
{
	int failures_before = failures;
	size_t size = 0;
	uint8_t *buf = read_file(path, &size);
	if(!buf)
	{
		fprintf(stderr, "%s: cannot read\n", path);
		return 1;
	}

	// 1. The whole file keeping everything: the AVCC must hold exactly the
	//    NAL units a byte-by-byte reading finds.
	size_t capacity = ck_h264_avcc_max_size(size);
	uint8_t *avcc = malloc(capacity ? capacity : 1);
	NalList reference = {0}, got = {0};
	CKH264FrameInfo info;
	CHECK(avcc != NULL);
	CHECK(ck_h264_annexb_to_avcc(buf, size, 0, avcc, capacity, &info) == CK_H264_OK);
	reference_parse(buf, size, &reference);
	CHECK(avcc_parse(avcc, info.avcc_size, &got));
	CHECK(lists_equal(&got, &reference));
	CHECK(info.nal_types & TYPE_BIT(CK_H264_NAL_SPS));
	CHECK(info.nal_types & TYPE_BIT(CK_H264_NAL_PPS));
	CHECK(info.nal_types & TYPE_BIT(CK_H264_NAL_IDR));

	uint32_t per_type[32] = {0};
	for(size_t i = 0; i < reference.count; i++)
		per_type[CK_H264_NAL_TYPE(reference.nal[i].data[0])]++;

	// 2. Access unit by access unit, as CKVideoSink will: one reused output
	//    buffer, the parameter sets in a store, and no access unit with
	//    slices before the store is ready.
	CKH264ParamStore store;
	ck_h264_param_store_reset(&store);
	size_t access_units = 0, idr_units = 0, param_changes = 0, largest = 0, au_start = 0, api_pos = 0;
	size_t slice_types[5] = {0};
	bool au_has_slice = false;
	double seconds = 0;
	for(size_t i = 0; i <= reference.count; i++)
	{
		bool end = i == reference.count;
		if(!end && !(au_has_slice && starts_access_unit(reference.nal[i].data, reference.nal[i].size)))
		{
			au_has_slice |= (TYPE_BIT(CK_H264_NAL_TYPE(reference.nal[i].data[0])) & SLICE_BITS) != 0;
			continue;
		}
		// The access unit runs from the start code of its first NAL unit to
		// the start code of the next access unit's first one.
		size_t au_end = end ? size : (size_t)(reference.nal[i].data - buf) - 3;
		const uint8_t *au = buf + au_start;
		size_t au_size = au_end - au_start;
		au_start = au_end;
		au_has_slice = !end && (TYPE_BIT(CK_H264_NAL_TYPE(reference.nal[i].data[0])) & SLICE_BITS);

		// ck_h264_next_access_unit(), which the CK Video app replays captures
		// with, must split the same way.
		const uint8_t *api_au = NULL;
		size_t api_au_size = 0;
		CHECK(ck_h264_next_access_unit(buf, size, &api_pos, &api_au, &api_au_size));
		CHECK(api_au == au && api_au_size == au_size);

		clock_t t0 = clock();
		CKH264Result r = ck_h264_annexb_to_avcc(au, au_size, CK_H264_DROP_FOR_VIDEOTOOLBOX, avcc, capacity, &info);
		CKH264ParamUpdate update = ck_h264_param_store_update(&store, &info.param_sets);
		seconds += (double)(clock() - t0) / CLOCKS_PER_SEC;

		CHECK(r == CK_H264_OK);
		CHECK(update != CK_H264_PARAMS_TOO_LARGE);
		param_changes += update == CK_H264_PARAMS_CHANGED;
		CHECK(avcc_parse(avcc, info.avcc_size, &got));
		CHECK((list_types(&got) & CK_H264_DROP_FOR_VIDEOTOOLBOX) == 0);
		if(info.nal_types & SLICE_BITS)
		{
			// Every slice header must read, and an IDR frame holds only I slices.
			for(size_t n = 0; n < got.count; n++)
			{
				if(!(TYPE_BIT(CK_H264_NAL_TYPE(got.nal[n].data[0])) & SLICE_BITS))
					continue;
				int slice_type = ck_h264_slice_type(got.nal[n].data, got.nal[n].size);
				CHECK(slice_type >= 0);
				if(slice_type >= 0)
					slice_types[slice_type]++;
				if(info.nal_types & TYPE_BIT(CK_H264_NAL_IDR))
					CHECK(slice_type == CK_H264_SLICE_I);
			}
			CHECK(ck_h264_param_store_ready(&store));
			access_units++;
			idr_units += (info.nal_types & TYPE_BIT(CK_H264_NAL_IDR)) != 0;
			if(au_size > largest)
				largest = au_size;
		}
	}

	const uint8_t *api_au = NULL;
	size_t api_au_size = 0;
	CHECK(!ck_h264_next_access_unit(buf, size, &api_pos, &api_au, &api_au_size));

	printf("%s: %zu bytes, %zu NAL units (", path, size, reference.count);
	const char *sep = "";
	for(unsigned t = 0; t < 32; t++)
	{
		if(per_type[t])
		{
			printf("%stype %u: %u", sep, t, per_type[t]);
			sep = ", ";
		}
	}
	printf(")\n");
	if(store.sps_size >= 4)
		printf("  SPS %zu bytes, profile_idc %u, level_idc %u; PPS %zu bytes; %zu parameter set change(s)\n",
			store.sps_size, (unsigned)store.sps[1], (unsigned)store.sps[3], store.pps_size, param_changes);
	printf("  slices: %zu I, %zu P, %zu B, %zu SP, %zu SI\n", slice_types[CK_H264_SLICE_I], slice_types[CK_H264_SLICE_P],
		slice_types[CK_H264_SLICE_B], slice_types[CK_H264_SLICE_SP], slice_types[CK_H264_SLICE_SI]);
	printf("  %zu frames, %zu IDR, largest %zu bytes, %.1f us per frame here\n",
		access_units, idr_units, largest, access_units ? seconds * 1e6 / (double)access_units : 0.0);

	list_free(&reference);
	list_free(&got);
	free(avcc);
	free(buf);
	int failed = failures - failures_before;
	printf("  %s\n", failed ? "FAILED" : "ok");
	return failed ? 1 : 0;
}

static int write_avcc(const char *in_path, const char *out_path)
{
	size_t size = 0;
	uint8_t *buf = read_file(in_path, &size);
	if(!buf)
	{
		fprintf(stderr, "%s: cannot read\n", in_path);
		return 1;
	}
	size_t capacity = ck_h264_avcc_max_size(size);
	uint8_t *avcc = malloc(capacity ? capacity : 1);
	CKH264FrameInfo info;
	int ret = 1;
	if(avcc && ck_h264_annexb_to_avcc(buf, size, 0, avcc, capacity, &info) == CK_H264_OK)
	{
		FILE *f = fopen(out_path, "wb");
		if(f)
		{
			ret = fwrite(avcc, 1, info.avcc_size, f) == info.avcc_size ? 0 : 1;
			ret |= fclose(f) != 0;
		}
	}
	if(ret)
		fprintf(stderr, "%s: conversion to %s failed\n", in_path, out_path);
	free(avcc);
	free(buf);
	return ret;
}

int main(int argc, char **argv)
{
	if(argc >= 3 && strcmp(argv[1], "check") == 0)
	{
		int ret = 0;
		for(int i = 2; i < argc; i++)
			ret |= check_capture(argv[i]);
		return ret;
	}
	if(argc == 4 && strcmp(argv[1], "avcc") == 0)
		return write_avcc(argv[2], argv[3]);
	if(argc != 1)
	{
		fprintf(stderr, "usage: %s [check FILE.h264... | avcc IN.h264 OUT.avcc]\n", argv[0]);
		return 2;
	}

	test_ps5_header();
	test_start_codes_and_zeros();
	test_emulation_prevention_is_kept();
	test_empty_nal_units_are_skipped();
	test_no_nal_unit();
	test_output_capacity();
	test_max_size();
	test_large_nal_length();
	test_input_too_large();
	test_frame_for_videotoolbox();
	test_param_store();
	test_access_units();
	test_slice_type();
	test_random_streams_round_trip();
	test_random_bytes_against_reference();

	if(failures)
	{
		fprintf(stderr, "ck_h264_tests: %d check(s) failed\n", failures);
		return 1;
	}
	printf("ck_h264_tests: all passed\n");
	return 0;
}
