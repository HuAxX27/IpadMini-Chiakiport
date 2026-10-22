// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A2.4: Bridge/CKConsoles.h and ckctl against the fake PS5 of
// ios/tests/fakes/fake_ps5.h, on 127.0.0.1. The fake binds the ports a PS5
// uses (9302 and 9295), so these tests run one at a time. A2.5: ckctl with
// the saved registrations, each test with its own file in a temporary folder.

// nftw() and mkdtemp(), for the temporary folders.
#define _XOPEN_SOURCE 700

#include <munit.h>

#include "Bridge/CKConsoles.h"
#include "Bridge/ck_registry.h"
#include "cli/ckctl.h"
#include "fake_ps5.h"

#include <chiaki/base64.h>
#include <chiaki/session.h>
#include <chiaki/thread.h>
#include <chiaki/time.h>

#include <ftw.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define HOST "127.0.0.1"
/** Loopback addresses where nothing listens. */
#define NOBODY "127.0.0.2"
#define NOBODY_ELSE "127.0.0.3"

static ChiakiLog test_log;
/** Where the ckctl tests keep their registration files; made in main(). */
static char store_dir[256];

static void *start_fake(FakePs5 *ps5)
{
	munit_assert_int(fake_ps5_start(ps5), ==, 0);
	return ps5;
}

static void *setup_fake(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	FakePs5 *ps5 = malloc(sizeof(*ps5));
	munit_assert_not_null(ps5);
	fake_ps5_init(ps5);
	return ps5;
}

static void teardown_fake(void *fixture)
{
	free(fixture);
}

static CKRegistRequest request_for(const FakePs5 *ps5)
{
	CKRegistRequest request;
	memset(&request, 0, sizeof(request));
	request.host = HOST;
	request.target = CHIAKI_TARGET_PS5_1;
	memcpy(request.psn_account_id, ps5->psn_account_id, sizeof(request.psn_account_id));
	request.pin = ps5->pin;
	return request;
}

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

static MunitResult test_parse_account_id(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	static const uint8_t bytes[CHIAKI_PSN_ACCOUNT_ID_SIZE] = { 0x21, 0x43, 0x65, 0x87, 0x09, 0xba, 0xdc, 0x0e };
	char b64[16];
	munit_assert_int(chiaki_base64_encode(bytes, sizeof(bytes), b64, sizeof(b64)), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_size(strlen(b64), ==, 12);

	uint8_t id[CHIAKI_PSN_ACCOUNT_ID_SIZE];
	munit_assert_int(ck_parse_psn_account_id(b64, id), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_memory_equal(sizeof(id), id, bytes);

	char padded[32];
	snprintf(padded, sizeof(padded), "  %s\n", b64);
	memset(id, 0, sizeof(id));
	munit_assert_int(ck_parse_psn_account_id(padded, id), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_memory_equal(sizeof(id), id, bytes);

	// The decimal form is the same 8 bytes read as a little-endian number.
	munit_assert_int(ck_parse_psn_account_id("1070935361496367905", id), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_memory_equal(sizeof(id), id, bytes);
	munit_assert_int(ck_parse_psn_account_id("18446744073709551615", id), ==, CHIAKI_ERR_SUCCESS);
	static const uint8_t ones[CHIAKI_PSN_ACCOUNT_ID_SIZE] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	munit_assert_memory_equal(sizeof(id), id, ones);

	static const char *const bad[] = {
		"", "   ", "abc", "0", "AAAAAAAAAAA=", "18446744073709551616", "123456789012345678901",
		"ITlFhwm63A4", "ITlFhwm63A==", "ITlFhwm6\xff\xff\xff=", "ITlFhwm63A4=x", "-1",
	};
	for(size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
	{
		if(ck_parse_psn_account_id(bad[i], id) == CHIAKI_ERR_SUCCESS)
			munit_errorf("accepted the account id \"%s\"", bad[i]);
	}
	munit_assert_int(ck_parse_psn_account_id(NULL, id), !=, CHIAKI_ERR_SUCCESS);
	return MUNIT_OK;
}

static MunitResult test_parse_pin(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	uint32_t pin = 0;
	munit_assert_int(ck_parse_regist_pin("12345678", &pin), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(pin, ==, 12345678);
	munit_assert_int(ck_parse_regist_pin("8765 4321", &pin), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(pin, ==, 87654321);
	munit_assert_int(ck_parse_regist_pin("0000-0042", &pin), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(pin, ==, 42);
	static const char *const bad[] = { "", "1234567", "123456789", "1234a678", "1234_5678" };
	for(size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
	{
		if(ck_parse_regist_pin(bad[i], &pin) == CHIAKI_ERR_SUCCESS)
			munit_errorf("accepted the PIN \"%s\"", bad[i]);
	}
	return MUNIT_OK;
}

static MunitResult test_regist_key_credential(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	char key[CHIAKI_SESSION_AUTH_SIZE];
	uint64_t credential = 0;

	memset(key, 0, sizeof(key));
	memcpy(key, FAKE_PS5_REGIST_KEY, strlen(FAKE_PS5_REGIST_KEY));
	munit_assert_int(ck_regist_key_credential(key, &credential), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_uint64(credential, ==, FAKE_PS5_CREDENTIAL);

	// A full key has no NUL after it.
	memcpy(key, "FFFFFFFF00000000", CHIAKI_SESSION_AUTH_SIZE);
	munit_assert_int(ck_regist_key_credential(key, &credential), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_uint64(credential, ==, UINT64_C(0xffffffff00000000));

	memset(key, 0, sizeof(key));
	munit_assert_int(ck_regist_key_credential(key, &credential), !=, CHIAKI_ERR_SUCCESS);
	memcpy(key, "12x4", 4);
	munit_assert_int(ck_regist_key_credential(key, &credential), !=, CHIAKI_ERR_SUCCESS);
	return MUNIT_OK;
}

// ---------------------------------------------------------------------------
// Discovery
// ---------------------------------------------------------------------------

static MunitResult test_discover_host(const MunitParameter params[], void *fixture)
{
	(void)params;
	FakePs5 *ps5 = start_fake(fixture);
	CKConsole consoles[CK_CONSOLES_MAX];
	size_t count = 0;
	uint64_t start = chiaki_time_now_monotonic_ms();
	munit_assert_int(ck_consoles_discover(&test_log, HOST, 3000, consoles, CK_CONSOLES_MAX, &count), ==, CHIAKI_ERR_SUCCESS);
	uint64_t elapsed = chiaki_time_now_monotonic_ms() - start;
	fake_ps5_stop(ps5);

	munit_assert_size(count, ==, 1);
	// With a host, the first answer ends the search.
	munit_assert_uint64(elapsed, <, 2000);
	const CKConsole *c = &consoles[0];
	munit_assert_string_equal(c->addr, HOST);
	munit_assert_string_equal(c->name, FAKE_PS5_NAME);
	munit_assert_string_equal(c->host_id, FAKE_PS5_HOST_ID_UPPER);
	munit_assert_string_equal(c->system_version, FAKE_PS5_SYSTEM_VERSION);
	munit_assert_string_equal(c->running_app_name, "");
	munit_assert_int(c->state, ==, CHIAKI_DISCOVERY_HOST_STATE_STANDBY);
	munit_assert_true(c->ps5);
	munit_assert_int(c->target, ==, CHIAKI_TARGET_PS5_1);
	return MUNIT_OK;
}

static MunitResult test_discover_silent(const MunitParameter params[], void *fixture)
{
	(void)params;
	FakePs5 *ps5 = fixture;
	ps5->discovery_silent = true;
	start_fake(ps5);
	CKConsole consoles[CK_CONSOLES_MAX];
	size_t count = 99;
	uint64_t start = chiaki_time_now_monotonic_ms();
	munit_assert_int(ck_consoles_discover(&test_log, HOST, 700, consoles, CK_CONSOLES_MAX, &count), ==, CHIAKI_ERR_SUCCESS);
	uint64_t elapsed = chiaki_time_now_monotonic_ms() - start;
	fake_ps5_stop(ps5);
	munit_assert_size(count, ==, 0);
	munit_assert_uint64(elapsed, >=, 650);
	return MUNIT_OK;
}

static MunitResult test_discover_bad_host(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	CKConsole consoles[1];
	size_t count = 99;
	munit_assert_int(ck_consoles_discover(&test_log, "", 100, consoles, 1, &count), ==, CHIAKI_ERR_PARSE_ADDR);
	munit_assert_size(count, ==, 0);
	return MUNIT_OK;
}

/** Without a host the search goes to every interface's broadcast address. */
static MunitResult test_discover_broadcast(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	CKConsole consoles[CK_CONSOLES_MAX];
	size_t count = 99;
	munit_assert_int(ck_consoles_discover(&test_log, NULL, 300, consoles, CK_CONSOLES_MAX, &count), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_size(count, <=, CK_CONSOLES_MAX);
	return MUNIT_OK;
}

typedef struct browser_watch_t
{
	ChiakiMutex mutex;
	ChiakiCond cond;
	int calls;
	size_t count;
	CKConsole first;
} BrowserWatch;

static void browser_cb(const CKConsole *consoles, size_t count, void *user)
{
	BrowserWatch *watch = user;
	chiaki_mutex_lock(&watch->mutex);
	watch->calls++;
	watch->count = count;
	if(count)
		watch->first = consoles[0];
	chiaki_cond_broadcast(&watch->cond);
	chiaki_mutex_unlock(&watch->mutex);
}

/** Waits until the browser reports count consoles, the first in state. */
static bool browser_wait(BrowserWatch *watch, size_t count, ChiakiDiscoveryHostState state, uint64_t timeout_ms)
{
	uint64_t deadline = chiaki_time_now_monotonic_ms() + timeout_ms;
	chiaki_mutex_lock(&watch->mutex);
	bool ok;
	while(!(ok = watch->count == count && (!count || watch->first.state == state)))
	{
		uint64_t now = chiaki_time_now_monotonic_ms();
		if(now >= deadline)
			break;
		chiaki_cond_timedwait(&watch->cond, &watch->mutex, deadline - now);
	}
	chiaki_mutex_unlock(&watch->mutex);
	return ok;
}

static MunitResult test_browser(const MunitParameter params[], void *fixture)
{
	(void)params;
	FakePs5 *ps5 = start_fake(fixture);
	BrowserWatch watch;
	memset(&watch, 0, sizeof(watch));
	chiaki_mutex_init(&watch.mutex, false);
	chiaki_cond_init(&watch.cond, &watch.mutex);

	CKConsoleBrowser *browser = NULL;
	munit_assert_int(ck_console_browser_start(&browser, &test_log, HOST, browser_cb, &watch), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_true(browser_wait(&watch, 1, CHIAKI_DISCOVERY_HOST_STATE_STANDBY, 3000));
	munit_assert_string_equal(watch.first.name, FAKE_PS5_NAME);

	// The console turns on: the next search reports it with its game.
	pthread_mutex_lock(&ps5->mutex);
	ps5->state = CHIAKI_DISCOVERY_HOST_STATE_READY;
	pthread_mutex_unlock(&ps5->mutex);
	munit_assert_true(browser_wait(&watch, 1, CHIAKI_DISCOVERY_HOST_STATE_READY, 3000));
	munit_assert_string_equal(watch.first.running_app_name, "Astro Bot");

	// It stops answering: dropped after 3 searches without an answer.
	pthread_mutex_lock(&ps5->mutex);
	ps5->discovery_silent = true;
	pthread_mutex_unlock(&ps5->mutex);
	munit_assert_true(browser_wait(&watch, 0, CHIAKI_DISCOVERY_HOST_STATE_UNKNOWN, 6000));

	ck_console_browser_stop(browser);
	chiaki_mutex_lock(&watch.mutex);
	int calls = watch.calls;
	chiaki_mutex_unlock(&watch.mutex);
	// Found, turned on, dropped: one call each, none for unchanged answers.
	munit_assert_int(calls, ==, 3);
	fake_ps5_stop(ps5);
	chiaki_cond_fini(&watch.cond);
	chiaki_mutex_fini(&watch.mutex);
	ck_console_browser_stop(NULL);
	return MUNIT_OK;
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

static MunitResult test_regist_success(const MunitParameter params[], void *fixture)
{
	(void)params;
	FakePs5 *ps5 = start_fake(fixture);
	CKRegistRequest request = request_for(ps5);
	CKRegistOutcome outcome;
	CKRegistResult result = ck_regist_run(&test_log, &request, &outcome);
	char account[32];
	memcpy(account, ps5->last_account_b64, sizeof(account));
	fake_ps5_stop(ps5);

	munit_assert_int(result, ==, CK_REGIST_SUCCESS);
	munit_assert_int(outcome.result, ==, CK_REGIST_SUCCESS);
	munit_assert_int(outcome.http_code, ==, 0);
	char expected[32];
	chiaki_base64_encode(ps5->psn_account_id, sizeof(ps5->psn_account_id), expected, sizeof(expected));
	munit_assert_string_equal(account, expected);

	const ChiakiRegisteredHost *host = &outcome.registered_host;
	munit_assert_int(host->target, ==, CHIAKI_TARGET_PS5_1);
	static const uint8_t mac[6] = { 0x0a, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f };
	munit_assert_memory_equal(sizeof(mac), host->server_mac, mac);
	munit_assert_string_equal(host->server_nickname, FAKE_PS5_NAME);
	munit_assert_string_equal(host->rp_regist_key, FAKE_PS5_REGIST_KEY);
	munit_assert_uint32(host->rp_key_type, ==, 2);
	uint8_t key[sizeof(host->rp_key)];
	memset(key, FAKE_PS5_RP_KEY_BYTE, sizeof(key));
	munit_assert_memory_equal(sizeof(key), host->rp_key, key);

	// What regist saves is what wakeup needs.
	uint64_t credential = 0;
	munit_assert_int(ck_regist_key_credential(host->rp_regist_key, &credential), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_uint64(credential, ==, FAKE_PS5_CREDENTIAL);
	return MUNIT_OK;
}

/** How the app matches a discovered console with its saved registration. */
static MunitResult test_host_id_matches_server_mac(const MunitParameter params[], void *fixture)
{
	(void)params;
	FakePs5 *ps5 = start_fake(fixture);
	CKConsole console;
	size_t count = 0;
	munit_assert_int(ck_consoles_discover(&test_log, HOST, 3000, &console, 1, &count), ==, CHIAKI_ERR_SUCCESS);
	CKRegistRequest request = request_for(ps5);
	CKRegistOutcome outcome;
	CKRegistResult result = ck_regist_run(&test_log, &request, &outcome);
	fake_ps5_stop(ps5);
	munit_assert_size(count, ==, 1);
	munit_assert_int(result, ==, CK_REGIST_SUCCESS);
	char mac[13];
	for(size_t i = 0; i < 6; i++)
		snprintf(mac + 2 * i, 3, "%02X", outcome.registered_host.server_mac[i]);
	munit_assert_string_equal(console.host_id, mac);
	return MUNIT_OK;
}

/** Registers against the fake set up by the caller and checks how it failed. */
static void expect_regist_failure(FakePs5 *ps5, const CKRegistRequest *request, CKRegistResult expected,
	int http_code, uint32_t reason)
{
	start_fake(ps5);
	CKRegistOutcome outcome;
	CKRegistResult result = ck_regist_run(&test_log, request, &outcome);
	fake_ps5_stop(ps5);
	if(result != expected)
		munit_errorf("regist ended with %d (%s), expected %d (%s)", (int)result, ck_regist_result_string(result),
			(int)expected, ck_regist_result_string(expected));
	munit_assert_int(outcome.http_code, ==, http_code);
	munit_assert_uint32(outcome.reason, ==, reason);
}

static MunitResult test_regist_wrong_pin(const MunitParameter params[], void *fixture)
{
	(void)params;
	FakePs5 *ps5 = fixture;
	CKRegistRequest request = request_for(ps5);
	request.pin = 87654321;
	expect_regist_failure(ps5, &request, CK_REGIST_WRONG_PIN, 403, CHIAKI_RP_APPLICATION_REASON_REGIST_FAILED);
	return MUNIT_OK;
}

static MunitResult test_regist_wrong_account(const MunitParameter params[], void *fixture)
{
	(void)params;
	FakePs5 *ps5 = fixture;
	CKRegistRequest request = request_for(ps5);
	request.psn_account_id[0] ^= 0xff;
	expect_regist_failure(ps5, &request, CK_REGIST_WRONG_ACCOUNT, 403, CHIAKI_RP_APPLICATION_REASON_INVALID_PSN_ID);
	return MUNIT_OK;
}

static MunitResult test_regist_server_error(const MunitParameter params[], void *fixture)
{
	(void)params;
	FakePs5 *ps5 = fixture;
	ps5->regist_mode = FAKE_PS5_REGIST_SERVER_ERROR;
	CKRegistRequest request = request_for(ps5);
	expect_regist_failure(ps5, &request, CK_REGIST_REJECTED, 500, 0);
	return MUNIT_OK;
}

static MunitResult test_regist_hang_up(const MunitParameter params[], void *fixture)
{
	(void)params;
	FakePs5 *ps5 = fixture;
	ps5->regist_mode = FAKE_PS5_REGIST_HANG_UP;
	CKRegistRequest request = request_for(ps5);
	expect_regist_failure(ps5, &request, CK_REGIST_NO_REPLY, 0, 0);
	return MUNIT_OK;
}

static MunitResult test_regist_no_key(const MunitParameter params[], void *fixture)
{
	(void)params;
	FakePs5 *ps5 = fixture;
	ps5->regist_mode = FAKE_PS5_REGIST_NO_KEY;
	CKRegistRequest request = request_for(ps5);
	expect_regist_failure(ps5, &request, CK_REGIST_BAD_REPLY, 0, 0);
	return MUNIT_OK;
}

static MunitResult test_regist_no_tcp(const MunitParameter params[], void *fixture)
{
	(void)params;
	FakePs5 *ps5 = fixture;
	ps5->regist_mode = FAKE_PS5_REGIST_NO_TCP;
	CKRegistRequest request = request_for(ps5);
	expect_regist_failure(ps5, &request, CK_REGIST_UNREACHABLE, 0, 0);
	return MUNIT_OK;
}

static MunitResult test_regist_not_found(const MunitParameter params[], void *fixture)
{
	(void)params;
	FakePs5 *ps5 = fixture;
	ps5->regist_mode = FAKE_PS5_REGIST_SILENT;
	CKRegistRequest request = request_for(ps5);
	// The core gives the search 3 seconds.
	expect_regist_failure(ps5, &request, CK_REGIST_NOT_FOUND, 0, 0);
	return MUNIT_OK;
}

/** No fake at all: the search gets "port unreachable" right away. */
static MunitResult test_regist_nobody(const MunitParameter params[], void *fixture)
{
	(void)params;
	CKRegistRequest request = request_for(fixture);
	CKRegistOutcome outcome;
	uint64_t start = chiaki_time_now_monotonic_ms();
	munit_assert_int(ck_regist_run(&test_log, &request, &outcome), ==, CK_REGIST_NOT_FOUND);
	munit_assert_uint64(chiaki_time_now_monotonic_ms() - start, <, 2500);
	return MUNIT_OK;
}

static MunitResult test_regist_bad_host(const MunitParameter params[], void *fixture)
{
	(void)params;
	CKRegistRequest request = request_for(fixture);
	request.host = "";
	CKRegistOutcome outcome;
	munit_assert_int(ck_regist_run(&test_log, &request, &outcome), ==, CK_REGIST_BAD_HOST);
	return MUNIT_OK;
}

typedef struct regist_cancel_t
{
	ChiakiBoolPredCond done;
	CKRegistOutcome outcome;
	int calls;
} RegistCancel;

static void regist_cancel_cb(const CKRegistOutcome *outcome, void *user)
{
	RegistCancel *c = user;
	c->outcome = *outcome;
	c->calls++;
	chiaki_bool_pred_cond_signal(&c->done);
}

static MunitResult test_regist_cancel(const MunitParameter params[], void *fixture)
{
	(void)params;
	FakePs5 *ps5 = fixture;
	ps5->regist_mode = FAKE_PS5_REGIST_SILENT;
	start_fake(ps5);
	RegistCancel c;
	memset(&c, 0, sizeof(c));
	chiaki_bool_pred_cond_init(&c.done);
	CKRegistRequest request = request_for(ps5);
	CKRegist *regist = NULL;
	uint64_t start = chiaki_time_now_monotonic_ms();
	munit_assert_int(ck_regist_start(&regist, &test_log, &request, regist_cancel_cb, &c), ==, CHIAKI_ERR_SUCCESS);
	chiaki_sleep_ms(200);
	ck_regist_cancel(regist);
	ck_regist_free(regist);
	uint64_t elapsed = chiaki_time_now_monotonic_ms() - start;
	fake_ps5_stop(ps5);
	munit_assert_int(c.calls, ==, 1);
	munit_assert_int(c.outcome.result, ==, CK_REGIST_CANCELED);
	munit_assert_uint64(elapsed, <, 2500);
	chiaki_bool_pred_cond_fini(&c.done);
	ck_regist_free(NULL);
	return MUNIT_OK;
}

// ---------------------------------------------------------------------------
// Wake-up
// ---------------------------------------------------------------------------

static bool wait_for_wakeups(FakePs5 *ps5, int wakeups, uint64_t timeout_ms)
{
	uint64_t deadline = chiaki_time_now_monotonic_ms() + timeout_ms;
	while(fake_ps5_wakeups(ps5) < wakeups)
	{
		if(chiaki_time_now_monotonic_ms() >= deadline)
			return false;
		chiaki_sleep_ms(10);
	}
	return true;
}

static MunitResult test_wakeup(const MunitParameter params[], void *fixture)
{
	(void)params;
	FakePs5 *ps5 = start_fake(fixture);
	ChiakiRegisteredHost registered;
	memset(&registered, 0, sizeof(registered));
	registered.target = CHIAKI_TARGET_PS5_1;
	memcpy(registered.rp_regist_key, FAKE_PS5_REGIST_KEY, strlen(FAKE_PS5_REGIST_KEY));
	munit_assert_int(ck_console_wakeup(&test_log, HOST, &registered), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_true(wait_for_wakeups(ps5, 1, 2000));
	uint64_t credential = ps5->last_credential;
	ChiakiDiscoveryHostState state = fake_ps5_state(ps5);
	fake_ps5_stop(ps5);
	munit_assert_uint64(credential, ==, FAKE_PS5_CREDENTIAL);
	munit_assert_int(state, ==, CHIAKI_DISCOVERY_HOST_STATE_READY);

	memset(registered.rp_regist_key, 0, sizeof(registered.rp_regist_key));
	munit_assert_int(ck_console_wakeup(&test_log, HOST, &registered), ==, CHIAKI_ERR_INVALID_DATA);
	return MUNIT_OK;
}

// ---------------------------------------------------------------------------
// ckctl
// ---------------------------------------------------------------------------

typedef struct ckctl_run_t
{
	int code;
	char out[4096];
	char err[4096];
} CkctlRun;

static void read_stream(FILE *f, char *buf, size_t size)
{
	rewind(f);
	size_t n = fread(buf, 1, size - 1, f);
	buf[n] = '\0';
	fclose(f);
}

/** Runs ckctl with the NULL-terminated arguments after "ckctl". */
static void ckctl(CkctlRun *run, ...)
{
	char *argv[16];
	int argc = 0;
	argv[argc++] = "ckctl";
	va_list args;
	va_start(args, run);
	char *arg;
	while((arg = va_arg(args, char *)) && argc < 15)
		argv[argc++] = arg;
	va_end(args);
	argv[argc] = NULL;

	FILE *out = tmpfile();
	FILE *err = tmpfile();
	munit_assert_not_null(out);
	munit_assert_not_null(err);
	run->code = ckctl_main(argc, argv, out, err);
	read_stream(out, run->out, sizeof(run->out));
	read_stream(err, run->err, sizeof(run->err));
}

static void assert_contains(const char *haystack, const char *needle)
{
	if(!strstr(haystack, needle))
		munit_errorf("\"%s\" not in:\n%s", needle, haystack);
}

static void assert_not_contains(const char *haystack, const char *needle)
{
	if(strstr(haystack, needle))
		munit_errorf("\"%s\" in:\n%s", needle, haystack);
}

/** A registration file for one test, which does not exist yet. */
static void fresh_store(char *path, size_t size, const char *name)
{
	munit_assert_int(snprintf(path, size, "%s/%s.plist", store_dir, name), <, (int)size);
	unlink(path);
}

/** What ckctl regist saves for the fake PS5. */
static ChiakiRegisteredHost fake_registration(void)
{
	ChiakiRegisteredHost host;
	memset(&host, 0, sizeof(host));
	host.target = CHIAKI_TARGET_PS5_1;
	static const uint8_t mac[6] = { 0x0a, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f };
	memcpy(host.server_mac, mac, sizeof(mac));
	snprintf(host.server_nickname, sizeof(host.server_nickname), "%s", FAKE_PS5_NAME);
	memcpy(host.rp_regist_key, FAKE_PS5_REGIST_KEY, strlen(FAKE_PS5_REGIST_KEY));
	host.rp_key_type = 2;
	memset(host.rp_key, FAKE_PS5_RP_KEY_BYTE, sizeof(host.rp_key));
	return host;
}

/** Another console, registered but not on the network. */
static ChiakiRegisteredHost other_registration(void)
{
	ChiakiRegisteredHost host = fake_registration();
	host.server_mac[5] = 0x60;
	snprintf(host.server_nickname, sizeof(host.server_nickname), "%s", "Cuarto");
	memcpy(host.rp_regist_key, "00ff00ff", 8);
	return host;
}

/** Saves the fake PS5's registration, last seen at last_host, and the other console when other. */
static void write_store(const char *path, const char *last_host, bool other)
{
	CKRegistry registry;
	memset(&registry, 0, sizeof(registry));
	ChiakiRegisteredHost host = fake_registration();
	munit_assert_int(ck_registry_put(&registry, &host, last_host), ==, CHIAKI_ERR_SUCCESS);
	if(other)
	{
		host = other_registration();
		munit_assert_int(ck_registry_put(&registry, &host, NOBODY_ELSE), ==, CHIAKI_ERR_SUCCESS);
	}
	munit_assert_int(ck_registry_save(&registry, path), ==, CHIAKI_ERR_SUCCESS);
}

/** The fake PS5's entry in the file at path. */
static CKRegistryEntry read_fake_entry(const char *path, size_t *count)
{
	CKRegistry registry;
	munit_assert_int(ck_registry_load(&registry, path), ==, CHIAKI_ERR_SUCCESS);
	CKRegistryEntry *entry = ck_registry_find_host_id(&registry, FAKE_PS5_HOST_ID);
	munit_assert_not_null(entry);
	if(count)
		*count = registry.count;
	return *entry;
}

static MunitResult test_ckctl_discover(const MunitParameter params[], void *fixture)
{
	(void)params;
	FakePs5 *ps5 = start_fake(fixture);
	char store[512];
	fresh_store(store, sizeof(store), "discover");
	CkctlRun run;
	ckctl(&run, "discover", "-H", HOST, "-s", store, NULL);
	fake_ps5_stop(ps5);
	munit_assert_int(run.code, ==, 0);
	assert_contains(run.out, HOST);
	assert_contains(run.out, "PS5");
	assert_contains(run.out, "en reposo");
	assert_contains(run.out, FAKE_PS5_NAME);
	assert_contains(run.out, "id " FAKE_PS5_HOST_ID_UPPER);
	assert_not_contains(run.out, "registrada");
	// Nothing registered, nothing to remember.
	munit_assert_int(access(store, F_OK), !=, 0);
	return MUNIT_OK;
}

/** discover marks a registered console and remembers where it answered. */
static MunitResult test_ckctl_discover_registered(const MunitParameter params[], void *fixture)
{
	(void)params;
	FakePs5 *ps5 = start_fake(fixture);
	char store[512];
	fresh_store(store, sizeof(store), "discover_registered");
	write_store(store, NOBODY, true);
	CkctlRun run;
	ckctl(&run, "discover", "-H", HOST, "-s", store, NULL);
	fake_ps5_stop(ps5);
	munit_assert_int(run.code, ==, 0);
	assert_contains(run.out, "id " FAKE_PS5_HOST_ID_UPPER "  fw " FAKE_PS5_SYSTEM_VERSION "  registrada");
	size_t count;
	CKRegistryEntry entry = read_fake_entry(store, &count);
	munit_assert_string_equal(entry.last_host, HOST);
	munit_assert_size(count, ==, 2);

	// A file it cannot read is left alone, and discover still works.
	static const char garbage[] = "no soy un plist";
	FILE *f = fopen(store, "wb");
	munit_assert_not_null(f);
	fputs(garbage, f);
	fclose(f);
	start_fake(ps5);
	ckctl(&run, "discover", "-H", HOST, "-s", store, NULL);
	fake_ps5_stop(ps5);
	munit_assert_int(run.code, ==, 0);
	assert_contains(run.out, FAKE_PS5_NAME);
	assert_contains(run.err, "aviso: no pude leer los registros");
	f = fopen(store, "rb");
	munit_assert_not_null(f);
	char back[64] = { 0 };
	munit_assert_size(fread(back, 1, sizeof(back) - 1, f), ==, strlen(garbage));
	fclose(f);
	munit_assert_string_equal(back, garbage);
	return MUNIT_OK;
}

static MunitResult test_ckctl_discover_none(const MunitParameter params[], void *fixture)
{
	(void)params;
	FakePs5 *ps5 = fixture;
	ps5->discovery_silent = true;
	start_fake(ps5);
	CkctlRun run;
	ckctl(&run, "discover", "-H", HOST, "-t", "1", NULL);
	fake_ps5_stop(ps5);
	munit_assert_int(run.code, ==, 1);
	assert_contains(run.err, "ninguna consola");
	return MUNIT_OK;
}

static MunitResult test_ckctl_regist(const MunitParameter params[], void *fixture)
{
	(void)params;
	FakePs5 *ps5 = start_fake(fixture);
	char account[16];
	chiaki_base64_encode(ps5->psn_account_id, sizeof(ps5->psn_account_id), account, sizeof(account));
	char store[512];
	fresh_store(store, sizeof(store), "regist");
	CkctlRun run;
	ckctl(&run, "regist", "-H", HOST, "-a", account, "-p", "1234 5678", "-s", store, NULL);
	munit_assert_int(run.code, ==, 0);
	assert_contains(run.out, "Registrando con " FAKE_PS5_NAME " (PS5, en reposo)");
	assert_contains(run.out, "Registro listo.");
	char saved[700];
	snprintf(saved, sizeof(saved), "Guardé el registro de " FAKE_PS5_NAME " (id " FAKE_PS5_HOST_ID_UPPER ") en %s.", store);
	assert_contains(run.out, saved);
	assert_contains(run.out, "ckctl wakeup -c " FAKE_PS5_HOST_ID_UPPER);
	// The keys are in the file, not on the screen.
	assert_not_contains(run.out, FAKE_PS5_REGIST_KEY);
	assert_not_contains(run.out, "rp_key");

	size_t count;
	CKRegistryEntry entry = read_fake_entry(store, &count);
	munit_assert_size(count, ==, 1);
	ChiakiRegisteredHost expected = fake_registration();
	munit_assert_memory_equal(sizeof(expected), &entry.host, &expected);
	munit_assert_string_equal(entry.last_host, HOST);

	// Registering again replaces it.
	ckctl(&run, "regist", "-H", HOST, "-a", account, "-p", "12345678", "-s", store, NULL);
	fake_ps5_stop(ps5);
	munit_assert_int(run.code, ==, 0);
	assert_contains(run.out, "Reemplacé el registro de " FAKE_PS5_NAME);
	read_fake_entry(store, &count);
	munit_assert_size(count, ==, 1);
	return MUNIT_OK;
}

/** With a store it cannot read, regist shows the keys instead and leaves the file alone. */
static MunitResult test_ckctl_regist_store_broken(const MunitParameter params[], void *fixture)
{
	(void)params;
	FakePs5 *ps5 = start_fake(fixture);
	char account[16];
	chiaki_base64_encode(ps5->psn_account_id, sizeof(ps5->psn_account_id), account, sizeof(account));
	char store[512];
	fresh_store(store, sizeof(store), "regist_store_broken");
	static const char garbage[] = "<plist><dict><key>roto";
	FILE *f = fopen(store, "wb");
	munit_assert_not_null(f);
	fputs(garbage, f);
	fclose(f);
	CkctlRun run;
	ckctl(&run, "regist", "-H", HOST, "-a", account, "-p", "12345678", "-s", store, NULL);
	fake_ps5_stop(ps5);
	munit_assert_int(run.code, ==, 1);
	assert_contains(run.err, "no pude leer los registros de");
	assert_contains(run.err, "no lo toco");
	assert_contains(run.out, "Registro listo.");
	assert_contains(run.out, "No quedó guardado.");
	assert_contains(run.out, "target=1000100");
	assert_contains(run.out, "server_mac=0a1b2c3d4e5f");
	assert_contains(run.out, "rp_regist_key=" FAKE_PS5_REGIST_KEY);
	assert_contains(run.out, "rp_key=" FAKE_PS5_RP_KEY_HEX);
	f = fopen(store, "rb");
	munit_assert_not_null(f);
	char back[64] = { 0 };
	munit_assert_size(fread(back, 1, sizeof(back) - 1, f), ==, strlen(garbage));
	fclose(f);
	munit_assert_string_equal(back, garbage);
	return MUNIT_OK;
}

static MunitResult test_ckctl_regist_wrong_pin(const MunitParameter params[], void *fixture)
{
	(void)params;
	FakePs5 *ps5 = start_fake(fixture);
	char store[512];
	fresh_store(store, sizeof(store), "regist_wrong_pin");
	CkctlRun run;
	ckctl(&run, "regist", "-H", HOST, "-a", "1070935361496367905", "-p", "11112222", "-s", store, NULL);
	fake_ps5_stop(ps5);
	munit_assert_int(run.code, ==, 1);
	assert_contains(run.err, "rechazó el PIN");
	assert_contains(run.err, "(HTTP 403, razón 0x80108b09)");
	munit_assert_int(access(store, F_OK), !=, 0);
	return MUNIT_OK;
}

static MunitResult test_ckctl_wakeup(const MunitParameter params[], void *fixture)
{
	(void)params;
	FakePs5 *ps5 = start_fake(fixture);
	CkctlRun run;
	ckctl(&run, "wakeup", "-H", HOST, "-k", FAKE_PS5_REGIST_KEY, "-w", "10", NULL);
	int wakeups = fake_ps5_wakeups(ps5);
	fake_ps5_stop(ps5);
	munit_assert_int(run.code, ==, 0);
	assert_contains(run.out, "Mandé la señal para despertar a " HOST);
	assert_contains(run.out, FAKE_PS5_NAME " se encendió en");
	munit_assert_int(wakeups, ==, 1);
	return MUNIT_OK;
}

static MunitResult test_ckctl_wakeup_already_on(const MunitParameter params[], void *fixture)
{
	(void)params;
	FakePs5 *ps5 = fixture;
	ps5->state = CHIAKI_DISCOVERY_HOST_STATE_READY;
	start_fake(ps5);
	CkctlRun run;
	ckctl(&run, "wakeup", "-H", HOST, "-k", FAKE_PS5_REGIST_KEY, NULL);
	int wakeups = fake_ps5_wakeups(ps5);
	fake_ps5_stop(ps5);
	munit_assert_int(run.code, ==, 0);
	assert_contains(run.out, "ya está encendida");
	munit_assert_int(wakeups, ==, 0);
	return MUNIT_OK;
}

/** A wrong key sends a packet the console ignores: ckctl gives up after -w. */
static MunitResult test_ckctl_wakeup_wrong_key(const MunitParameter params[], void *fixture)
{
	(void)params;
	FakePs5 *ps5 = start_fake(fixture);
	CkctlRun run;
	ckctl(&run, "wakeup", "-H", HOST, "-k", "ffff0000", "-w", "2", NULL);
	int wakeups = fake_ps5_wakeups(ps5);
	ChiakiDiscoveryHostState state = fake_ps5_state(ps5);
	fake_ps5_stop(ps5);
	munit_assert_int(run.code, ==, 1);
	assert_contains(run.err, "no se encendió en 2 s");
	munit_assert_int(wakeups, >=, 1);
	munit_assert_int(state, ==, CHIAKI_DISCOVERY_HOST_STATE_STANDBY);
	return MUNIT_OK;
}

/** wakeup with the saved registration: no key to type, the address it was last seen at. */
static MunitResult test_ckctl_wakeup_store(const MunitParameter params[], void *fixture)
{
	(void)params;
	FakePs5 *ps5 = start_fake(fixture);
	char store[512];
	fresh_store(store, sizeof(store), "wakeup_store");
	write_store(store, HOST, false);
	CkctlRun run;
	ckctl(&run, "wakeup", "-s", store, "-w", "10", NULL);
	int wakeups = fake_ps5_wakeups(ps5);
	pthread_mutex_lock(&ps5->mutex);
	uint64_t credential = ps5->last_credential;
	pthread_mutex_unlock(&ps5->mutex);
	munit_assert_int(run.code, ==, 0);
	assert_contains(run.out, "Mandé la señal para despertar a " HOST ".");
	assert_contains(run.out, FAKE_PS5_NAME " se encendió en");
	munit_assert_int(wakeups, ==, 1);
	munit_assert_uint64(credential, ==, FAKE_PS5_CREDENTIAL);

	// On now: nothing to send.
	ckctl(&run, "wakeup", "-s", store, NULL);
	wakeups = fake_ps5_wakeups(ps5);
	fake_ps5_stop(ps5);
	munit_assert_int(run.code, ==, 0);
	assert_contains(run.out, FAKE_PS5_NAME " ya está encendida.");
	munit_assert_int(wakeups, ==, 1);
	return MUNIT_OK;
}

/** The console is not at its last address any more: wakeup searches for it and remembers the new one. */
static MunitResult test_ckctl_wakeup_store_moved(const MunitParameter params[], void *fixture)
{
	(void)params;
	FakePs5 *ps5 = start_fake(fixture);
	char store[512];
	fresh_store(store, sizeof(store), "wakeup_store_moved");
	write_store(store, NOBODY, false);
	CkctlRun run;
	// -b stands for the network's broadcast address, which never reaches 127.0.0.1.
	ckctl(&run, "wakeup", "-s", store, "-b", HOST, NULL);
	bool woken = wait_for_wakeups(ps5, 1, 2000);
	fake_ps5_stop(ps5);
	munit_assert_int(run.code, ==, 0);
	assert_contains(run.out, "Mandé la señal para despertar a " HOST ".");
	munit_assert_true(woken);
	CKRegistryEntry entry = read_fake_entry(store, NULL);
	munit_assert_string_equal(entry.last_host, HOST);
	return MUNIT_OK;
}

/** Nowhere to be found: with a last address the signal still goes there; without one, ckctl asks for -H. */
static MunitResult test_ckctl_wakeup_store_not_found(const MunitParameter params[], void *fixture)
{
	(void)params;
	FakePs5 *ps5 = fixture;
	ps5->discovery_silent = true;
	start_fake(ps5);
	char store[512];
	fresh_store(store, sizeof(store), "wakeup_store_not_found");
	write_store(store, HOST, false);
	CkctlRun run;
	ckctl(&run, "wakeup", "-s", store, "-b", NOBODY, NULL);
	munit_assert_int(run.code, ==, 0);
	assert_contains(run.out, FAKE_PS5_NAME " no contestó la búsqueda; mando la señal a " HOST " de todos modos.");

	write_store(store, NULL, false);
	ckctl(&run, "wakeup", "-s", store, "-b", NOBODY, NULL);
	fake_ps5_stop(ps5);
	munit_assert_int(run.code, ==, 1);
	assert_contains(run.err, "no encuentro a " FAKE_PS5_NAME " en la red y no sé su IP; pásala con -H");
	return MUNIT_OK;
}

/** With two consoles registered, wakeup needs to be told which. */
static MunitResult test_ckctl_wakeup_store_choose(const MunitParameter params[], void *fixture)
{
	(void)params;
	FakePs5 *ps5 = start_fake(fixture);
	char store[512];
	fresh_store(store, sizeof(store), "wakeup_store_choose");
	CkctlRun run;
	ckctl(&run, "wakeup", "-s", store, NULL);
	munit_assert_int(run.code, ==, 1);
	assert_contains(run.err, "no hay consolas registradas en");

	write_store(store, HOST, true);
	ckctl(&run, "wakeup", "-s", store, NULL);
	munit_assert_int(run.code, ==, 1);
	assert_contains(run.err, "hay 2 consolas registradas: elige una con -c");
	ckctl(&run, "wakeup", "-s", store, "-c", "Nadie", NULL);
	munit_assert_int(run.code, ==, 1);
	assert_contains(run.err, "ninguna consola registrada se llama, tiene el id o la IP \"Nadie\"");
	munit_assert_int(fake_ps5_wakeups(ps5), ==, 0);

	// By name, by id in any case, and by the address it was last seen at.
	ckctl(&run, "wakeup", "-s", store, "-c", FAKE_PS5_NAME, NULL);
	munit_assert_int(run.code, ==, 0);
	assert_contains(run.out, "Mandé la señal para despertar a " HOST ".");
	munit_assert_true(wait_for_wakeups(ps5, 1, 2000));
	pthread_mutex_lock(&ps5->mutex);
	ps5->state = CHIAKI_DISCOVERY_HOST_STATE_STANDBY;
	pthread_mutex_unlock(&ps5->mutex);
	ckctl(&run, "wakeup", "-s", store, "-c", FAKE_PS5_HOST_ID, NULL);
	munit_assert_int(run.code, ==, 0);
	assert_contains(run.out, "Mandé la señal para despertar a " HOST ".");
	munit_assert_true(wait_for_wakeups(ps5, 2, 2000));
	ckctl(&run, "wakeup", "-s", store, "-H", HOST, NULL);
	fake_ps5_stop(ps5);
	munit_assert_int(run.code, ==, 0);
	assert_contains(run.out, FAKE_PS5_NAME " ya está encendida.");
	return MUNIT_OK;
}

static MunitResult test_ckctl_list_forget(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	char store[512];
	fresh_store(store, sizeof(store), "list_forget");
	CkctlRun run;
	ckctl(&run, "list", "-s", store, NULL);
	munit_assert_int(run.code, ==, 0);
	assert_contains(run.out, "No hay consolas registradas en");

	write_store(store, NULL, true);
	ckctl(&run, "list", "-s", store, NULL);
	munit_assert_int(run.code, ==, 0);
	assert_contains(run.out, "(sin IP)         PS5  " FAKE_PS5_NAME "  id " FAKE_PS5_HOST_ID_UPPER "\n");
	assert_contains(run.out, NOBODY_ELSE "        PS5  Cuarto  id 0A1B2C3D4E60\n");
	assert_not_contains(run.out, FAKE_PS5_REGIST_KEY);

	ckctl(&run, "forget", "-s", store, NULL);
	munit_assert_int(run.code, ==, 2);
	ckctl(&run, "forget", "-s", store, "-c", "Nadie", NULL);
	munit_assert_int(run.code, ==, 1);
	ckctl(&run, "forget", "-s", store, "-c", FAKE_PS5_HOST_ID, NULL);
	munit_assert_int(run.code, ==, 0);
	assert_contains(run.out, "Borré el registro de " FAKE_PS5_NAME " (id " FAKE_PS5_HOST_ID_UPPER ").");
	ckctl(&run, "list", "-s", store, NULL);
	munit_assert_int(run.code, ==, 0);
	assert_not_contains(run.out, FAKE_PS5_NAME);
	assert_contains(run.out, "Cuarto");
	ckctl(&run, "forget", "-s", store, "-c", "Cuarto", NULL);
	munit_assert_int(run.code, ==, 0);
	ckctl(&run, "list", "-s", store, NULL);
	assert_contains(run.out, "No hay consolas registradas en");

	// A file that is not a registry is not listed, and not saved over.
	FILE *f = fopen(store, "wb");
	munit_assert_not_null(f);
	fputs("no soy un plist", f);
	fclose(f);
	ckctl(&run, "list", "-s", store, NULL);
	munit_assert_int(run.code, ==, 1);
	assert_contains(run.err, "no es un archivo de registros o está dañado");
	ckctl(&run, "forget", "-s", store, "-c", "Cuarto", NULL);
	munit_assert_int(run.code, ==, 1);
	return MUNIT_OK;
}

static MunitResult test_ckctl_usage(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	CkctlRun run;
	ckctl(&run, NULL);
	munit_assert_int(run.code, ==, 2);
	assert_contains(run.err, "Uso:");
	ckctl(&run, "--help", NULL);
	munit_assert_int(run.code, ==, 0);
	assert_contains(run.out, "ckctl [-v] regist");
	ckctl(&run, "bailar", NULL);
	munit_assert_int(run.code, ==, 2);
	ckctl(&run, "regist", "-H", HOST, NULL);
	munit_assert_int(run.code, ==, 2);
	ckctl(&run, "regist", "-H", HOST, "-a", "nope", "-p", "12345678", NULL);
	munit_assert_int(run.code, ==, 2);
	assert_contains(run.err, "Account ID");
	ckctl(&run, "regist", "-H", HOST, "-a", "1070935361496367905", "-p", "123", NULL);
	munit_assert_int(run.code, ==, 2);
	assert_contains(run.err, "PIN");
	ckctl(&run, "wakeup", "-H", HOST, "-k", "zz", NULL);
	munit_assert_int(run.code, ==, 2);
	ckctl(&run, "wakeup", "-H", HOST, "-k", "11111111111111111", NULL);
	munit_assert_int(run.code, ==, 2);
	ckctl(&run, "discover", "-t", "mucho", NULL);
	munit_assert_int(run.code, ==, 2);
	ckctl(&run, "discover", "-t", NULL);
	munit_assert_int(run.code, ==, 2);
	ckctl(&run, "discover", "--lento", NULL);
	munit_assert_int(run.code, ==, 2);
	ckctl(&run, "wakeup", "-k", FAKE_PS5_REGIST_KEY, NULL);
	munit_assert_int(run.code, ==, 2);
	assert_contains(run.err, "wakeup con -k pide -H");
	ckctl(&run, "forget", NULL);
	munit_assert_int(run.code, ==, 2);
	ckctl(&run, "list", "-s", NULL);
	munit_assert_int(run.code, ==, 2);
	ckctl(&run, "help", NULL);
	munit_assert_int(run.code, ==, 0);
	assert_contains(run.out, "ckctl forget -c CONSOLA");
	return MUNIT_OK;
}

static int remove_one(const char *path, const struct stat *st, int type, struct FTW *ftw)
{
	(void)st;
	(void)type;
	(void)ftw;
	return remove(path);
}

#define TEST(name, fn) { name, fn, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
#define FAKE_TEST(name, fn) { name, fn, setup_fake, teardown_fake, MUNIT_TEST_OPTION_NONE, NULL }

static MunitTest tests[] = {
	TEST("/parse_account_id", test_parse_account_id),
	TEST("/parse_pin", test_parse_pin),
	TEST("/regist_key_credential", test_regist_key_credential),
	FAKE_TEST("/discover_host", test_discover_host),
	FAKE_TEST("/discover_silent", test_discover_silent),
	TEST("/discover_bad_host", test_discover_bad_host),
	TEST("/discover_broadcast", test_discover_broadcast),
	FAKE_TEST("/browser", test_browser),
	FAKE_TEST("/regist_success", test_regist_success),
	FAKE_TEST("/host_id_matches_server_mac", test_host_id_matches_server_mac),
	FAKE_TEST("/regist_wrong_pin", test_regist_wrong_pin),
	FAKE_TEST("/regist_wrong_account", test_regist_wrong_account),
	FAKE_TEST("/regist_server_error", test_regist_server_error),
	FAKE_TEST("/regist_hang_up", test_regist_hang_up),
	FAKE_TEST("/regist_no_key", test_regist_no_key),
	FAKE_TEST("/regist_no_tcp", test_regist_no_tcp),
	FAKE_TEST("/regist_not_found", test_regist_not_found),
	FAKE_TEST("/regist_nobody", test_regist_nobody),
	FAKE_TEST("/regist_bad_host", test_regist_bad_host),
	FAKE_TEST("/regist_cancel", test_regist_cancel),
	FAKE_TEST("/wakeup", test_wakeup),
	FAKE_TEST("/ckctl_discover", test_ckctl_discover),
	FAKE_TEST("/ckctl_discover_none", test_ckctl_discover_none),
	FAKE_TEST("/ckctl_discover_registered", test_ckctl_discover_registered),
	FAKE_TEST("/ckctl_regist", test_ckctl_regist),
	FAKE_TEST("/ckctl_regist_store_broken", test_ckctl_regist_store_broken),
	FAKE_TEST("/ckctl_regist_wrong_pin", test_ckctl_regist_wrong_pin),
	FAKE_TEST("/ckctl_wakeup", test_ckctl_wakeup),
	FAKE_TEST("/ckctl_wakeup_already_on", test_ckctl_wakeup_already_on),
	FAKE_TEST("/ckctl_wakeup_wrong_key", test_ckctl_wakeup_wrong_key),
	FAKE_TEST("/ckctl_wakeup_store", test_ckctl_wakeup_store),
	FAKE_TEST("/ckctl_wakeup_store_moved", test_ckctl_wakeup_store_moved),
	FAKE_TEST("/ckctl_wakeup_store_not_found", test_ckctl_wakeup_store_not_found),
	FAKE_TEST("/ckctl_wakeup_store_choose", test_ckctl_wakeup_store_choose),
	TEST("/ckctl_list_forget", test_ckctl_list_forget),
	TEST("/ckctl_usage", test_ckctl_usage),
	{ NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
};

static const MunitSuite suite = { "/consoles", tests, NULL, 1, MUNIT_SUITE_OPTION_NONE };

int main(int argc, char *argv[])
{
	if(chiaki_lib_init() != CHIAKI_ERR_SUCCESS)
		return 1;
	// Silent: the failing registrations are expected to log errors. -v in
	// ckctl's tests is not used for the same reason.
	chiaki_log_init(&test_log, 0, NULL, NULL);
	const char *tmp = getenv("TMPDIR");
	snprintf(store_dir, sizeof(store_dir), "%s/ck-consoles-XXXXXX", tmp && *tmp ? tmp : "/tmp");
	if(!mkdtemp(store_dir))
		return 1;
	int r = munit_suite_main(&suite, NULL, argc, argv);
	nftw(store_dir, remove_one, 16, FTW_DEPTH | FTW_PHYS);
	return r;
}
