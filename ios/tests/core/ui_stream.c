// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A6.5: what the game screen says and decides (UI/ck_ui_stream.h).
// CKStreamViewController.m asks ck_ui_session_should_retry() after every end
// before any video, and shows ck_ui_quit_reason_string() and
// ck_ui_metrics_line() as they come.

#include <munit.h>

#include "UI/ck_ui_stream.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static MunitResult test_retry(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	// "Remote is already in use" right after the previous session: ask again, up to the limit.
	for(unsigned retries = 0; retries < CK_UI_SESSION_RETRY_MAX; retries++)
		munit_assert_true(ck_ui_session_should_retry(CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_IN_USE, false, retries));
	munit_assert_false(ck_ui_session_should_retry(CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_IN_USE, false,
		CK_UI_SESSION_RETRY_MAX));
	// After video came, in use means something else took the console: no retry.
	munit_assert_false(ck_ui_session_should_retry(CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_IN_USE, true, 0));
	// Every other end is shown, not retried.
	for(int reason = CHIAKI_QUIT_REASON_NONE; reason <= CHIAKI_QUIT_REASON_STREAM_CONNECTION_TRANSPORT_FAILED; reason++)
		if(reason != CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_IN_USE)
			munit_assert_false(ck_ui_session_should_retry((ChiakiQuitReason)reason, false, 0));
	// The whole wait covers the PS5's cooldown, 4 to 8 s, with room to spare.
	munit_assert_int(CK_UI_SESSION_RETRY_SECONDS * CK_UI_SESSION_RETRY_MAX, >=, 16);
	return MUNIT_OK;
}

static MunitResult test_quit_reasons(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	// Every reason has a sentence of its own words, ending in a period.
	for(int reason = CHIAKI_QUIT_REASON_NONE; reason <= CHIAKI_QUIT_REASON_STREAM_CONNECTION_TRANSPORT_FAILED + 1;
		reason++)
	{
		const char *text = ck_ui_quit_reason_string((ChiakiQuitReason)reason);
		munit_assert_not_null(text);
		size_t len = strlen(text);
		munit_assert_size(len, >, 10);
		munit_assert_char(text[len - 1], ==, '.');
	}
	// The ones that need the user to do something say what.
	munit_assert_not_null(strstr(ck_ui_quit_reason_string(CHIAKI_QUIT_REASON_PSN_REGIST_FAILED), "registrarlo"));
	munit_assert_not_null(strstr(ck_ui_quit_reason_string(CHIAKI_QUIT_REASON_STREAM_CONNECTION_TRANSPORT_FAILED),
		"Ajustes"));
	munit_assert_string_not_equal(ck_ui_quit_reason_string(CHIAKI_QUIT_REASON_STREAM_CONNECTION_REMOTE_SHUTDOWN),
		ck_ui_quit_reason_string(CHIAKI_QUIT_REASON_STREAM_CONNECTION_REMOTE_DISCONNECTED));
	return MUNIT_OK;
}

static MunitResult test_metrics_line(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	char line[CK_UI_METRICS_LINE_SIZE];
	CKMetrics metrics = { 29.84f, 4.16f, 2, 1, 80, 1900, 47000 };
	ck_ui_metrics_line(&metrics, line, sizeof(line));
	munit_assert_string_equal(line, "29,8 fps · decodifica 4,2 ms · 1,9 Mbps · 2 cuadros perdidos · audio 1 ocultados, "
		"80 ms · 46 MB de RAM");

	memset(&metrics, 0, sizeof(metrics));
	ck_ui_metrics_line(&metrics, line, sizeof(line));
	munit_assert_string_equal(line, "0,0 fps · decodifica 0,0 ms · 0,0 Mbps · 0 cuadros perdidos · audio 0 ocultados, "
		"0 ms · 0 MB de RAM");

	// Rounding carries into the units.
	metrics.fps_decoded = 59.96f;
	metrics.bitrate_kbps = 14999;
	ck_ui_metrics_line(&metrics, line, sizeof(line));
	munit_assert_not_null(strstr(line, "60,0 fps"));
	munit_assert_not_null(strstr(line, "15,0 Mbps"));

	// Values no session gives still fit and stay readable.
	CKMetrics worst = { 1e30f, -5.0f, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX };
	ck_ui_metrics_line(&worst, line, sizeof(line));
	munit_assert_size(strlen(line), <, sizeof(line) - 1);
	munit_assert_not_null(strstr(line, "100000,0 fps · decodifica 0,0 ms"));
	munit_assert_not_null(strstr(line, "de RAM"));
	CKMetrics nan = { NAN, 1.0f, 0, 0, 0, 0, 0 };
	ck_ui_metrics_line(&nan, line, sizeof(line));
	munit_assert_not_null(strstr(line, "0,0 fps · decodifica 1,0 ms"));

	// A short buffer is cut, never overrun.
	char small[12];
	memset(small, 'x', sizeof(small));
	ck_ui_metrics_line(&metrics, small, 8);
	munit_assert_size(strlen(small), ==, 7);
	munit_assert_char(small[8], ==, 'x');
	small[0] = 'x';
	ck_ui_metrics_line(&metrics, small, 0);
	munit_assert_char(small[0], ==, 'x');
	return MUNIT_OK;
}

#define TEST(name, fn) { name, fn, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }

static MunitTest tests[] = {
	TEST("/retry", test_retry),
	TEST("/quit_reasons", test_quit_reasons),
	TEST("/metrics_line", test_metrics_line),
	{ NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
};

static const MunitSuite suite = { "/ui_stream", tests, NULL, 1, MUNIT_SUITE_OPTION_NONE };

int main(int argc, char *argv[])
{
	return munit_suite_main(&suite, NULL, argc, argv);
}
