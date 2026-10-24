// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A2.7: Bridge/ck_video_info.h, which reads the profile header the console
// sends before the frames. The x264 headers were made with ffmpeg (libx264)
// from one frame of its test pattern, ps5_540 came from a real PS5, and the
// others were written bit by bit for what x264 does not make, and checked with
// ffmpeg's trace_headers.

#include <munit.h>

#include "Bridge/ck_video_info.h"

#include <stdlib.h>
#include <string.h>

#define START 0x00, 0x00, 0x00, 0x01

/** The x264 PPS that follows every SPS below: a header is SPS then PPS. */
#define PPS START, 0x68, 0xeb, 0xe3, 0xcb, 0x22, 0xc0

// 960x540 High 3.1: coded 960x544, cropped by 4 rows.
static const uint8_t high_540[] = { START, 0x67, 0x64, 0x00, 0x1f, 0xac, 0xd9, 0x40, 0xf0, 0x11, 0x7e, 0xf0, 0x11,
	0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00, 0x3c, 0x0f, 0x18, 0x31, 0x96, PPS };
static const uint8_t main_720[] = { START, 0x67, 0x4d, 0x40, 0x1f, 0xec, 0xa0, 0x28, 0x02, 0xdd, 0x80, 0x88, 0x00,
	0x00, 0x03, 0x00, 0x08, 0x00, 0x00, 0x03, 0x01, 0xe0, 0x78, 0xc1, 0x8c, 0xb0, PPS };
static const uint8_t baseline_360[] = { START, 0x67, 0x42, 0xc0, 0x1e, 0xd9, 0x00, 0xa0, 0x2f, 0xf9, 0x70, 0x11,
	0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00, 0x3c, 0x0f, 0x16, 0x2e, 0x48, PPS };
static const uint8_t high_1080[] = { START, 0x67, 0x64, 0x00, 0x2a, 0xac, 0xd9, 0x40, 0x78, 0x02, 0x27, 0xe5, 0xc0,
	0x44, 0x00, 0x00, 0x03, 0x00, 0x04, 0x00, 0x00, 0x03, 0x00, 0xf0, 0x3c, 0x60, 0xc6, 0x58, PPS };
// What a PS5 sent when asked for 540p30 (the first session from the iPad, in
// October 2026), with its own PPS: Main 3.2, coded 960x544 and cropped by 4
// rows, pic_order_cnt_type 2, 9 reference frames, and a VUI.
static const uint8_t ps5_540[] = { START, 0x67, 0x4d, 0x40, 0x20, 0x91, 0x8a, 0x03, 0xc0, 0x45, 0xfb, 0xc0, 0x5a,
	0x80, 0x80, 0x80, 0xa0, 0x00, 0x01, 0xf4, 0x80, 0x00, 0x75, 0x30, 0x11, 0x34, 0x38, 0x74, 0x54, START, 0x68,
	0xee, 0x3c, 0x80 };
// Interlaced: 17 map units of 32 rows, cropped by 2 field rows (4 rows).
static const uint8_t interlaced_540[] = { START, 0x67, 0x64, 0x00, 0x1f, 0xac, 0xd9, 0x40, 0xf0, 0x22, 0xfd, 0x60,
	0x22, 0x00, 0x00, 0x03, 0x00, 0x02, 0x00, 0x00, 0x03, 0x00, 0x78, 0x3e, 0x2c, 0x5b, 0x2c, PPS };
static const uint8_t high10_540[] = { START, 0x67, 0x6e, 0x00, 0x1f, 0xa6, 0xcd, 0x94, 0x0f, 0x01, 0x17, 0xef, 0x01,
	0x10, 0x00, 0x00, 0x03, 0x00, 0x10, 0x00, 0x00, 0x03, 0x03, 0xc0, 0xf1, 0x83, 0x19, 0x60, PPS };
// 4:2:2 crops in whole rows: 544 - 4.
static const uint8_t high422_540[] = { START, 0x67, 0x7a, 0x00, 0x1f, 0xbc, 0xd9, 0x40, 0xf0, 0x11, 0x7e, 0x5c,
	0x04, 0x40, 0x00, 0x00, 0x03, 0x00, 0x40, 0x00, 0x00, 0x0f, 0x03, 0xc6, 0x0c, 0x65, 0x80, PPS };
static const uint8_t high444_540[] = { START, 0x67, 0xf4, 0x00, 0x1f, 0x91, 0x9b, 0x28, 0x1e, 0x02, 0x2f, 0xcb, 0x80,
	0x88, 0x00, 0x00, 0x03, 0x00, 0x08, 0x00, 0x00, 0x03, 0x01, 0xe0, 0x78, 0xc1, 0x8c, 0xb0, PPS };
// Written by hand: scaling lists 0, 1 and 6, pic_order_cnt_type 1 with a long
// offset that needs two emulation prevention bytes, and 2 + 2 columns cropped.
static const uint8_t scaling_poc1[] = { START, 0x67, 0x64, 0x00, 0x1f, 0xad, 0xa4, 0x92, 0x49, 0x24, 0x92, 0x49,
	0x42, 0x21, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23,
	0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x50, 0x00,
	0x00, 0x03, 0x02, 0x00, 0x00, 0x03, 0x03, 0x22, 0x28, 0xa4, 0x07, 0x80, 0x8b, 0xb7, 0x68, PPS };
// Written by hand, all wrong: a scaling delta of 200, a crop of the whole
// height, and 2000 macroblocks across.
static const uint8_t bad_delta[] = { START, 0x67, 0x64, 0x00, 0x1f, 0xad, 0x80, 0x64, 0x3f, 0xff, 0x80, 0xda, 0x03,
	0xc0, 0x45, 0xfb, 0x40, PPS };
static const uint8_t crop_all[] = { START, 0x67, 0x64, 0x00, 0x1f, 0xac, 0xda, 0x03, 0xc0, 0x45, 0xf8, 0x04, 0x45,
	PPS };
static const uint8_t too_wide[] = { START, 0x67, 0x64, 0x00, 0x1f, 0xac, 0xda, 0x00, 0x1f, 0x40, 0x11, 0x7e, 0xd0,
	PPS };
// x265's 960x540 header, cut after the VPS.
static const uint8_t hevc_540[] = { START, 0x40, 0x01, 0x0c, 0x01, 0xff, 0xff, 0x04, 0x08, 0x00, 0x00, 0x03, 0x00,
	0x9e, 0x08, 0x00, 0x00, 0x03, 0x00, 0x00, 0x5a, 0x95, 0x98, 0x09 };

typedef struct expected_t
{
	const char *name;
	const uint8_t *buf;
	size_t size;
	uint8_t profile_idc;
	uint8_t level_idc;
	uint8_t chroma_format_idc;
	uint8_t bit_depth;
	bool interlaced;
	uint32_t width;
	uint32_t height;
	const char *profile;
	bool a5_decodes;
} Expected;

#define VECTOR(buf) #buf, buf, sizeof(buf)

static const Expected expected[] = {
	{ VECTOR(high_540), 100, 31, 1, 8, false, 960, 540, "High", true },
	{ VECTOR(ps5_540), 77, 32, 1, 8, false, 960, 540, "Main", true },
	{ VECTOR(main_720), 77, 31, 1, 8, false, 1280, 720, "Main", true },
	{ VECTOR(baseline_360), 66, 30, 1, 8, false, 640, 360, "Constrained Baseline", true },
	{ VECTOR(high_1080), 100, 42, 1, 8, false, 1920, 1080, "High", false },
	{ VECTOR(interlaced_540), 100, 31, 1, 8, true, 960, 540, "High", false },
	{ VECTOR(high10_540), 110, 31, 1, 10, false, 960, 540, "High 10", false },
	{ VECTOR(high422_540), 122, 31, 2, 8, false, 960, 540, "High 4:2:2", false },
	{ VECTOR(high444_540), 244, 31, 3, 8, false, 960, 540, "High 4:4:4 Predictive", false },
	{ VECTOR(scaling_poc1), 100, 31, 1, 8, false, 952, 540, "High", true },
};

static MunitResult test_x264_headers(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	for(size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++)
	{
		const Expected *e = &expected[i];
		CKVideoInfo info;
		if(!ck_video_info_parse(e->buf, e->size, &info))
			munit_errorf("%s did not parse", e->name);
		munit_assert_int(info.codec, ==, CK_VIDEO_CODEC_H264);
		munit_assert_uint8(info.profile_idc, ==, e->profile_idc);
		munit_assert_uint8(info.level_idc, ==, e->level_idc);
		munit_assert_uint8(info.chroma_format_idc, ==, e->chroma_format_idc);
		munit_assert_uint8(info.bit_depth_luma, ==, e->bit_depth);
		munit_assert_uint8(info.bit_depth_chroma, ==, e->bit_depth);
		munit_assert(info.interlaced == e->interlaced);
		if(info.width != e->width || info.height != e->height)
			munit_errorf("%s: %ux%u, expected %ux%u", e->name, (unsigned)info.width, (unsigned)info.height,
				(unsigned)e->width, (unsigned)e->height);
		munit_assert_string_equal(ck_video_info_profile_name(&info), e->profile);
		munit_assert(ck_video_info_a5_decodes(&info) == e->a5_decodes);
	}
	return MUNIT_OK;
}

static MunitResult test_hevc(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	CKVideoInfo info;
	munit_assert_true(ck_video_info_parse(hevc_540, sizeof(hevc_540), &info));
	munit_assert_int(info.codec, ==, CK_VIDEO_CODEC_H265);
	munit_assert_uint32(info.width, ==, 0);
	munit_assert_false(ck_video_info_a5_decodes(&info));
	munit_assert_string_equal(ck_video_info_profile_name(&info), "unknown");
	return MUNIT_OK;
}

/** Copies the parts into buf, one after the other, and returns the size. */
static size_t join(uint8_t *buf, const uint8_t *a, size_t a_size, const uint8_t *b, size_t b_size)
{
	memcpy(buf, a, a_size);
	memcpy(buf + a_size, b, b_size);
	return a_size + b_size;
}

static MunitResult test_what_comes_first(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	uint8_t buf[256];
	CKVideoInfo info;

	// An access unit delimiter, an SEI and three-byte start codes before the SPS.
	static const uint8_t aud_sei[] = { START, 0x09, 0xf0, 0x00, 0x00, 0x01, 0x06, 0x05, 0x01, 0x80 };
	size_t size = join(buf, aud_sei, sizeof(aud_sei), high_540, sizeof(high_540));
	munit_assert_true(ck_video_info_parse(buf, size, &info));
	munit_assert_uint32(info.width, ==, 960);

	// Bytes before the first start code are skipped, as the core's buffers never have them.
	static const uint8_t junk[] = { 0x12, 0x34 };
	size = join(buf, junk, sizeof(junk), high_540, sizeof(high_540));
	munit_assert_true(ck_video_info_parse(buf, size, &info));

	// A frame: a slice comes first, with or without a delimiter, and the SPS
	// after it does not count. The delimiter rule stops after four units.
	static const uint8_t idr[] = { START, 0x65, 0x88, 0x84, 0x00 };
	size = join(buf, idr, sizeof(idr), high_540, sizeof(high_540));
	munit_assert_false(ck_video_info_parse(buf, size, &info));
	munit_assert_int(info.codec, ==, CK_VIDEO_CODEC_UNKNOWN);
	static const uint8_t aud_slice[] = { START, 0x09, 0xf0, START, 0x41, 0x9a, 0x02 };
	munit_assert_false(ck_video_info_parse(aud_slice, sizeof(aud_slice), &info));
	static const uint8_t four_auds[] = { START, 0x09, 0xf0, START, 0x09, 0xf0, START, 0x09, 0xf0, START, 0x09, 0xf0 };
	size = join(buf, four_auds, sizeof(four_auds), high_540, sizeof(high_540));
	munit_assert_false(ck_video_info_parse(buf, size, &info));

	// forbidden_zero_bit, nothing at all, and a start code with nothing after it.
	memcpy(buf, high_540, sizeof(high_540));
	buf[4] |= 0x80;
	munit_assert_false(ck_video_info_parse(buf, sizeof(high_540), &info));
	munit_assert_false(ck_video_info_parse(buf, 0, &info));
	static const uint8_t start_only[] = { START };
	munit_assert_false(ck_video_info_parse(start_only, sizeof(start_only), &info));
	return MUNIT_OK;
}

static MunitResult test_broken(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	CKVideoInfo info;
	munit_assert_false(ck_video_info_parse(bad_delta, sizeof(bad_delta), &info));
	munit_assert_false(ck_video_info_parse(crop_all, sizeof(crop_all), &info));
	munit_assert_false(ck_video_info_parse(too_wide, sizeof(too_wide), &info));
	munit_assert_int(info.codec, ==, CK_VIDEO_CODEC_UNKNOWN);

	// Every cut of every header, in a buffer of exactly that size so the
	// sanitizers see a read past it. Short cuts fail; none may crash.
	for(size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++)
	{
		const Expected *e = &expected[i];
		for(size_t cut = 0; cut < e->size; cut++)
		{
			uint8_t *copy = malloc(cut ? cut : 1);
			munit_assert_not_null(copy);
			memcpy(copy, e->buf, cut);
			bool parsed = ck_video_info_parse(copy, cut, &info);
			if(cut < 4 + 8)
				munit_assert_false(parsed);
			free(copy);
		}
	}

	// Long runs of zero bits: an Exp-Golomb code over 32 bits is an error.
	static const uint8_t zeros[] = { START, 0x67, 0x64, 0x00, 0x1f, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00,
		0x03, 0x00, 0x00, 0x03, 0x00, 0x01 };
	munit_assert_false(ck_video_info_parse(zeros, sizeof(zeros), &info));
	return MUNIT_OK;
}

#define TEST(name, fn) { name, fn, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }

static MunitTest tests[] = {
	TEST("/x264_headers", test_x264_headers),
	TEST("/hevc", test_hevc),
	TEST("/what_comes_first", test_what_comes_first),
	TEST("/broken", test_broken),
	{ NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
};

static const MunitSuite suite = { "/video_info", tests, NULL, 1, MUNIT_SUITE_OPTION_NONE };

int main(int argc, char *argv[])
{
	return munit_suite_main(&suite, NULL, argc, argv);
}
