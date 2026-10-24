// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A6.4: what the settings screen offers and what the settings file may hold
// (UI/ck_ui_settings.h). CKSettingsStore.m stores the numbers that
// ck_ui_settings_to_values() gives and reads them back through
// ck_ui_settings_from_values(), so a round trip here is a round trip there.

#include <munit.h>

#include "UI/ck_ui_settings.h"

#include <stdint.h>
#include <string.h>

static void assert_default(const CKSettings *settings)
{
	munit_assert_int(settings->resolution, ==, CHIAKI_VIDEO_RESOLUTION_PRESET_540p);
	munit_assert_int(settings->fps, ==, CHIAKI_VIDEO_FPS_PRESET_30);
	munit_assert_uint32(settings->bitrate_kbps, ==, 0);
}

static MunitResult test_round_trip(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	// Every combination the screen offers survives the file.
	for(int r = 0; r < CK_UI_RESOLUTION_COUNT; r++)
		for(int f = 0; f < CK_UI_FPS_COUNT; f++)
			for(int b = 0; b < CK_UI_BITRATE_COUNT; b++)
			{
				CKSettings settings = { ck_ui_resolutions[r], ck_ui_fps[f], ck_ui_bitrates_kbps[b] };
				munit_assert_true(ck_ui_settings_valid(&settings));
				int64_t height, fps, bitrate;
				ck_ui_settings_to_values(&settings, &height, &fps, &bitrate);
				CKSettings back;
				munit_assert_true(ck_ui_settings_from_values(height, fps, bitrate, &back));
				munit_assert_int(back.resolution, ==, settings.resolution);
				munit_assert_int(back.fps, ==, settings.fps);
				munit_assert_uint32(back.bitrate_kbps, ==, settings.bitrate_kbps);
			}
	return MUNIT_OK;
}

static MunitResult test_file_values(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	CKSettings settings;
	munit_assert_true(ck_ui_settings_from_values(720, 60, 10000, &settings));
	munit_assert_int(settings.resolution, ==, CHIAKI_VIDEO_RESOLUTION_PRESET_720p);
	munit_assert_int(settings.fps, ==, CHIAKI_VIDEO_FPS_PRESET_60);
	munit_assert_uint32(settings.bitrate_kbps, ==, 10000);

	int64_t height, fps, bitrate;
	ck_ui_settings_to_values(&settings, &height, &fps, &bitrate);
	munit_assert_int64(height, ==, 720);
	munit_assert_int64(fps, ==, 60);
	munit_assert_int64(bitrate, ==, 10000);

	// A bitrate set by hand in range stays, though the screen has no segment for it.
	munit_assert_true(ck_ui_settings_from_values(360, 30, 7500, &settings));
	munit_assert_uint32(settings.bitrate_kbps, ==, 7500);
	munit_assert_int(ck_ui_bitrate_index(7500), ==, -1);
	munit_assert_true(ck_ui_settings_from_values(540, 30, CK_UI_BITRATE_MIN_KBPS, &settings));
	munit_assert_true(ck_ui_settings_from_values(540, 30, CK_UI_BITRATE_MAX_KBPS, &settings));
	return MUNIT_OK;
}

static MunitResult test_missing_or_bad(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	CKSettings settings;
	// A file with nothing, or nothing usable: the defaults, and the app opens.
	munit_assert_false(ck_ui_settings_from_values(-1, -1, -1, &settings));
	assert_default(&settings);
	munit_assert_false(ck_ui_settings_from_values(1080, 120, 999999, &settings));
	assert_default(&settings);
	munit_assert_false(ck_ui_settings_from_values(0, 0, CK_UI_BITRATE_MIN_KBPS - 1, &settings));
	assert_default(&settings);
	munit_assert_false(ck_ui_settings_from_values(INT64_MAX, INT64_MIN, CK_UI_BITRATE_MAX_KBPS + 1, &settings));
	assert_default(&settings);
	// The preset numbers are not heights: 2 is not 540p.
	munit_assert_false(ck_ui_settings_from_values(CHIAKI_VIDEO_RESOLUTION_PRESET_540p, 30, 0, &settings));

	// Each bad value falls back on its own; the good ones stay.
	munit_assert_false(ck_ui_settings_from_values(720, 45, 4000, &settings));
	munit_assert_int(settings.resolution, ==, CHIAKI_VIDEO_RESOLUTION_PRESET_720p);
	munit_assert_int(settings.fps, ==, CHIAKI_VIDEO_FPS_PRESET_30);
	munit_assert_uint32(settings.bitrate_kbps, ==, 4000);
	munit_assert_false(ck_ui_settings_from_values(-1, 60, -5, &settings));
	munit_assert_int(settings.resolution, ==, CHIAKI_VIDEO_RESOLUTION_PRESET_540p);
	munit_assert_int(settings.fps, ==, CHIAKI_VIDEO_FPS_PRESET_60);
	munit_assert_uint32(settings.bitrate_kbps, ==, 0);
	return MUNIT_OK;
}

static MunitResult test_valid(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	CKSettings settings;
	ck_settings_set_default(&settings);
	munit_assert_true(ck_ui_settings_valid(&settings));
	settings.resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_1080p;
	munit_assert_false(ck_ui_settings_valid(&settings));
	ck_settings_set_default(&settings);
	settings.fps = (ChiakiVideoFPSPreset)45;
	munit_assert_false(ck_ui_settings_valid(&settings));
	ck_settings_set_default(&settings);
	settings.bitrate_kbps = 500;
	munit_assert_false(ck_ui_settings_valid(&settings));
	return MUNIT_OK;
}

static MunitResult test_options(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	munit_assert_uint(ck_ui_resolution_height(CHIAKI_VIDEO_RESOLUTION_PRESET_360p), ==, 360);
	munit_assert_uint(ck_ui_resolution_height(CHIAKI_VIDEO_RESOLUTION_PRESET_540p), ==, 540);
	munit_assert_uint(ck_ui_resolution_height(CHIAKI_VIDEO_RESOLUTION_PRESET_720p), ==, 720);
	munit_assert_uint(ck_ui_resolution_height(CHIAKI_VIDEO_RESOLUTION_PRESET_1080p), ==, 0);

	// The core's own bitrate for each preset.
	for(int i = 0; i < CK_UI_RESOLUTION_COUNT; i++)
	{
		ChiakiConnectVideoProfile profile;
		chiaki_connect_video_profile_preset(&profile, ck_ui_resolutions[i], CHIAKI_VIDEO_FPS_PRESET_30);
		munit_assert_uint32(ck_ui_auto_bitrate_kbps(ck_ui_resolutions[i]), ==, profile.bitrate);
		munit_assert_uint(ck_ui_resolution_height(ck_ui_resolutions[i]), ==, profile.height);
		munit_assert_int(ck_ui_resolution_index(ck_ui_resolutions[i]), ==, i);
	}
	munit_assert_int(ck_ui_resolution_index(CHIAKI_VIDEO_RESOLUTION_PRESET_1080p), ==, -1);
	munit_assert_int(ck_ui_fps_index(CHIAKI_VIDEO_FPS_PRESET_60), ==, 1);
	munit_assert_int(ck_ui_bitrate_index(0), ==, 0);
	munit_assert_int(ck_ui_bitrate_index(15000), ==, CK_UI_BITRATE_COUNT - 1);

	munit_assert_string_equal(ck_ui_resolution_label(CHIAKI_VIDEO_RESOLUTION_PRESET_540p), "540p");
	munit_assert_string_equal(ck_ui_resolution_label(CHIAKI_VIDEO_RESOLUTION_PRESET_1080p), "?");
	munit_assert_string_equal(ck_ui_fps_label(CHIAKI_VIDEO_FPS_PRESET_60), "60");
	munit_assert_string_equal(ck_ui_bitrate_label(0), "Auto");
	munit_assert_string_equal(ck_ui_bitrate_label(8000), "8");
	munit_assert_string_equal(ck_ui_bitrate_label(7500), "?");
	return MUNIT_OK;
}

static MunitResult test_summary(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	char out[96];
	CKSettings settings;
	ck_settings_set_default(&settings);
	ck_ui_settings_summary(&settings, out, sizeof(out));
	munit_assert_string_equal(out, "540p · 30 fps · 6 Mbps (automático)");

	settings = (CKSettings){ CHIAKI_VIDEO_RESOLUTION_PRESET_720p, CHIAKI_VIDEO_FPS_PRESET_60, 15000 };
	ck_ui_settings_summary(&settings, out, sizeof(out));
	munit_assert_string_equal(out, "720p · 60 fps · 15 Mbps");

	settings.bitrate_kbps = 7500;
	ck_ui_settings_summary(&settings, out, sizeof(out));
	munit_assert_string_equal(out, "720p · 60 fps · 7,5 Mbps");
	settings.bitrate_kbps = 2960;
	ck_ui_settings_summary(&settings, out, sizeof(out));
	munit_assert_string_equal(out, "720p · 60 fps · 3 Mbps");

	// Cut to fit, always terminated, never past the end.
	memset(out, 'x', sizeof(out));
	ck_ui_settings_summary(&settings, out, 8);
	munit_assert_size(strlen(out), ==, 7);
	munit_assert_char(out[8], ==, 'x');
	ck_ui_settings_summary(&settings, out, 0);
	munit_assert_char(out[0], ==, '7');
	return MUNIT_OK;
}

static MunitTest tests[] = {
	{ "/round_trip", test_round_trip, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/file_values", test_file_values, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/missing_or_bad", test_missing_or_bad, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/valid", test_valid, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/options", test_options, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/summary", test_summary, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
};

static const MunitSuite suite = { "/ui_settings", tests, NULL, 1, MUNIT_SUITE_OPTION_NONE };

int main(int argc, char *argv[])
{
	return munit_suite_main(&suite, NULL, argc, argv);
}
