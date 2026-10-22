// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A2.6: Bridge/ck_session.h, the C half of CKSession, against the fake PS5 of
// ios/tests/fakes/fake_ps5.h on 127.0.0.1. The fake answers the session
// request in each of the ways a PS5 does, but has no stream, so these tests
// cover the session's life (start, quit, stop, start again) and its input
// thread, not the video.

#include <munit.h>

#include "Bridge/ck_session.h"
#include "fake_ps5.h"

#include <chiaki/thread.h>
#include <chiaki/time.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HOST "127.0.0.1"

// Under ASan and TSan resident memory says nothing (freed memory waits in a
// quarantine); LeakSanitizer checks the leaks instead, with --no-fork. Under
// qemu it is qemu's, which grows with the code it translates.
#if defined(CK_TEST_EMULATED) || defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define CHECK_RESIDENT 0
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define CHECK_RESIDENT 0
#endif
#endif
#ifndef CHECK_RESIDENT
#define CHECK_RESIDENT 1
#endif

static ChiakiLog test_log;

/** What the callbacks saw. */
typedef struct watch_t
{
	ChiakiMutex mutex;
	ChiakiCond cond;
	CKSessionCoreState states[16];
	size_t states_count;
	int quits;
	ChiakiQuitReason reason;
} Watch;

static void watch_init(Watch *watch)
{
	memset(watch, 0, sizeof(*watch));
	chiaki_mutex_init(&watch->mutex, false);
	chiaki_cond_init(&watch->cond, &watch->mutex);
}

static void watch_fini(Watch *watch)
{
	chiaki_cond_fini(&watch->cond);
	chiaki_mutex_fini(&watch->mutex);
}

static void state_cb(CKSessionCoreState state, void *user)
{
	Watch *watch = user;
	chiaki_mutex_lock(&watch->mutex);
	if(watch->states_count < sizeof(watch->states) / sizeof(watch->states[0]))
		watch->states[watch->states_count++] = state;
	chiaki_cond_broadcast(&watch->cond);
	chiaki_mutex_unlock(&watch->mutex);
}

static void quit_cb(ChiakiQuitReason reason, const char *reason_str, void *user)
{
	(void)reason_str;
	Watch *watch = user;
	chiaki_mutex_lock(&watch->mutex);
	watch->quits++;
	watch->reason = reason;
	chiaki_mutex_unlock(&watch->mutex);
}

static bool stopped_pred(void *user)
{
	Watch *watch = user;
	return watch->states_count && watch->states[watch->states_count - 1] == CK_SESSION_CORE_STOPPED;
}

/** Waits for the state STOPPED. */
static bool watch_wait_stopped(Watch *watch, uint64_t timeout_ms)
{
	chiaki_mutex_lock(&watch->mutex);
	ChiakiErrorCode err = chiaki_cond_timedwait_pred(&watch->cond, &watch->mutex, timeout_ms, stopped_pred, watch);
	chiaki_mutex_unlock(&watch->mutex);
	return err == CHIAKI_ERR_SUCCESS;
}

static ChiakiRegisteredHost fake_registration(void)
{
	ChiakiRegisteredHost host;
	memset(&host, 0, sizeof(host));
	host.target = CHIAKI_TARGET_PS5_1;
	static const uint8_t mac[6] = { 0x0a, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f };
	memcpy(host.server_mac, mac, sizeof(mac));
	memcpy(host.rp_regist_key, FAKE_PS5_REGIST_KEY, strlen(FAKE_PS5_REGIST_KEY));
	host.rp_key_type = 2;
	memset(host.rp_key, FAKE_PS5_RP_KEY_BYTE, sizeof(host.rp_key));
	return host;
}

static CKSessionCore *new_session(Watch *watch)
{
	ChiakiRegisteredHost host = fake_registration();
	CKSettings settings;
	ck_settings_set_default(&settings);
	CKSessionCallbacks callbacks = { state_cb, quit_cb, watch };
	CKSessionCore *session = ck_session_new(&test_log, HOST, &host, &settings, &callbacks);
	munit_assert_not_null(session);
	return session;
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

static size_t count_dir(const char *path)
{
	DIR *dir = opendir(path);
	munit_assert_not_null(dir);
	size_t n = 0;
	struct dirent *entry;
	while((entry = readdir(dir)))
		if(entry->d_name[0] != '.')
			n++;
	closedir(dir);
	return n;
}

// ---------------------------------------------------------------------------
// Without a console
// ---------------------------------------------------------------------------

static MunitResult test_video_profile(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	CKSettings settings;
	ck_settings_set_default(&settings);
	ChiakiConnectVideoProfile profile;
	ck_session_video_profile(&settings, &profile);
	munit_assert_uint(profile.width, ==, 960);
	munit_assert_uint(profile.height, ==, 540);
	munit_assert_uint(profile.max_fps, ==, 30);
	munit_assert_uint(profile.bitrate, ==, 6000);
	munit_assert_int(profile.codec, ==, CHIAKI_CODEC_H264);

	settings.resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_720p;
	settings.fps = CHIAKI_VIDEO_FPS_PRESET_60;
	settings.bitrate_kbps = 8000;
	ck_session_video_profile(&settings, &profile);
	munit_assert_uint(profile.width, ==, 1280);
	munit_assert_uint(profile.height, ==, 720);
	munit_assert_uint(profile.max_fps, ==, 60);
	munit_assert_uint(profile.bitrate, ==, 8000);
	munit_assert_int(profile.codec, ==, CHIAKI_CODEC_H264);

	settings.resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_360p;
	settings.fps = CHIAKI_VIDEO_FPS_PRESET_30;
	settings.bitrate_kbps = 0;
	ck_session_video_profile(&settings, &profile);
	munit_assert_uint(profile.width, ==, 640);
	munit_assert_uint(profile.bitrate, ==, 2000);
	return MUNIT_OK;
}

typedef struct fake_source_t
{
	bool report;
	ChiakiControllerState state;
	int polls;
} FakeSource;

static bool fake_poll(void *user, ChiakiControllerState *state)
{
	FakeSource *source = user;
	__atomic_add_fetch(&source->polls, 1, __ATOMIC_SEQ_CST);
	if(!source->report)
		return false;
	*state = source->state;
	return true;
}

static MunitResult test_input_merge(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	ChiakiControllerState merged;
	munit_assert_false(ck_input_merge(NULL, 0, &merged));
	ChiakiControllerState idle;
	chiaki_controller_state_set_idle(&idle);
	munit_assert_true(chiaki_controller_state_equals(&merged, &idle));

	FakeSource pad = { .report = true };
	chiaki_controller_state_set_idle(&pad.state);
	pad.state.buttons = CHIAKI_CONTROLLER_BUTTON_CROSS;
	pad.state.left_x = 1000;
	pad.state.l2_state = 10;
	FakeSource touch = { .report = true };
	chiaki_controller_state_set_idle(&touch.state);
	touch.state.buttons = CHIAKI_CONTROLLER_BUTTON_MOON;
	touch.state.left_x = -2000;
	touch.state.l2_state = 200;
	FakeSource quiet = { .report = false };
	chiaki_controller_state_set_idle(&quiet.state);
	quiet.state.buttons = CHIAKI_CONTROLLER_BUTTON_BOX;

	CKInputSource sources[] = {
		{ "quiet", &quiet, fake_poll },
		{ "pad", &pad, fake_poll },
		{ "none", NULL, NULL },
		{ "touch", &touch, fake_poll },
	};
	munit_assert_true(ck_input_merge(sources, 4, &merged));
	munit_assert_uint32(merged.buttons, ==, CHIAKI_CONTROLLER_BUTTON_CROSS | CHIAKI_CONTROLLER_BUTTON_MOON);
	munit_assert_int(merged.left_x, ==, -2000);
	munit_assert_uint8(merged.l2_state, ==, 200);
	munit_assert_int(quiet.polls, ==, 1);

	// One source alone comes out as it is.
	munit_assert_true(ck_input_merge(&sources[1], 1, &merged));
	munit_assert_true(chiaki_controller_state_equals(&merged, &pad.state));
	munit_assert_false(ck_input_merge(sources, 1, &merged));
	munit_assert_true(chiaki_controller_state_equals(&merged, &idle));
	return MUNIT_OK;
}

static MunitResult test_bad_start(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	ChiakiRegisteredHost host = fake_registration();
	CKSettings settings;
	ck_settings_set_default(&settings);

	CKSessionCore *session = ck_session_new(&test_log, "", &host, &settings, NULL);
	munit_assert_not_null(session);
	munit_assert_int(ck_session_start(session), ==, CHIAKI_ERR_INVALID_DATA);
	munit_assert_int(ck_session_state(session), ==, CK_SESSION_CORE_IDLE);
	ck_session_free(session);

	memset(host.rp_regist_key, 0, sizeof(host.rp_regist_key));
	session = ck_session_new(&test_log, HOST, &host, &settings, NULL);
	munit_assert_int(ck_session_start(session), ==, CHIAKI_ERR_INVALID_DATA);
	// Stopping or freeing a session that never started is fine.
	ck_session_stop(session);
	ck_session_stop(session);
	munit_assert_int(ck_session_state(session), ==, CK_SESSION_CORE_IDLE);
	ck_session_free(session);
	ck_session_free(NULL);
	return MUNIT_OK;
}

static MunitResult test_resident_kb(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	munit_assert_uint32(ck_resident_kb(), >, 0);
	return MUNIT_OK;
}

// ---------------------------------------------------------------------------
// Against the fake
// ---------------------------------------------------------------------------

/** Starts a session against the fake and waits for it to end on its own. */
static ChiakiQuitReason run_until_quit(Watch *watch)
{
	CKSessionCore *session = new_session(watch);
	munit_assert_int(ck_session_start(session), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_true(watch_wait_stopped(watch, 15000));
	munit_assert_int(ck_session_state(session), ==, CK_SESSION_CORE_STOPPED);
	ck_session_stop(session);
	ck_session_free(session);
	chiaki_mutex_lock(&watch->mutex);
	munit_assert_int(watch->quits, ==, 1);
	munit_assert_size(watch->states_count, ==, 2);
	munit_assert_int(watch->states[0], ==, CK_SESSION_CORE_CONNECTING);
	munit_assert_int(watch->states[1], ==, CK_SESSION_CORE_STOPPED);
	ChiakiQuitReason reason = watch->reason;
	chiaki_mutex_unlock(&watch->mutex);
	return reason;
}

static MunitResult test_refused(const MunitParameter params[], void *fixture)
{
	(void)params;
	FakePs5 *ps5 = fixture;
	ps5->regist_mode = FAKE_PS5_REGIST_NO_TCP;
	munit_assert_int(fake_ps5_start(ps5), ==, 0);
	Watch watch;
	watch_init(&watch);
	ChiakiQuitReason reason = run_until_quit(&watch);
	fake_ps5_stop(ps5);
	watch_fini(&watch);
	munit_assert_int(reason, ==, CHIAKI_QUIT_REASON_SESSION_REQUEST_CONNECTION_REFUSED);
	return MUNIT_OK;
}

static MunitResult test_in_use(const MunitParameter params[], void *fixture)
{
	(void)params;
	FakePs5 *ps5 = fixture;
	ps5->session_mode = FAKE_PS5_SESSION_IN_USE;
	munit_assert_int(fake_ps5_start(ps5), ==, 0);
	Watch watch;
	watch_init(&watch);
	ChiakiQuitReason reason = run_until_quit(&watch);
	int requests = fake_ps5_session_requests(ps5);
	fake_ps5_stop(ps5);
	watch_fini(&watch);
	munit_assert_int(reason, ==, CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_IN_USE);
	munit_assert_int(requests, ==, 1);
	// The session asks with the saved registration key, as hex of its text.
	munit_assert_string_equal(ps5->last_session_regist_key, "3161326233633464");
	return MUNIT_OK;
}

/** The console takes the session and the ctrl connection is refused: the ctrl thread starts and joins too. */
static MunitResult test_ctrl_refused(const MunitParameter params[], void *fixture)
{
	(void)params;
	FakePs5 *ps5 = fixture;
	munit_assert_int(fake_ps5_start(ps5), ==, 0);
	Watch watch;
	watch_init(&watch);
	ChiakiQuitReason reason = run_until_quit(&watch);
	int ctrl = fake_ps5_ctrl_requests(ps5);
	fake_ps5_stop(ps5);
	watch_fini(&watch);
	munit_assert_int(ctrl, ==, 1);
	munit_assert_true(chiaki_quit_reason_is_error(reason));
	munit_assert_int(reason, !=, CHIAKI_QUIT_REASON_STOPPED);
	return MUNIT_OK;
}

static bool session_requested_pred(void *user)
{
	return fake_ps5_session_requests(user) > 0;
}

/** Stop while the console has not answered: returns right away, as STOPPED. */
static MunitResult test_stop_while_connecting(const MunitParameter params[], void *fixture)
{
	(void)params;
	FakePs5 *ps5 = fixture;
	ps5->session_mode = FAKE_PS5_SESSION_HANG;
	munit_assert_int(fake_ps5_start(ps5), ==, 0);
	Watch watch;
	watch_init(&watch);
	CKSessionCore *session = new_session(&watch);
	munit_assert_int(ck_session_start(session), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_int(ck_session_start(session), ==, CHIAKI_ERR_INVALID_DATA);
	uint64_t deadline = chiaki_time_now_monotonic_ms() + 3000;
	while(!session_requested_pred(ps5) && chiaki_time_now_monotonic_ms() < deadline)
		chiaki_sleep_ms(5);
	munit_assert_int(fake_ps5_session_requests(ps5), ==, 1);
	munit_assert_int(ck_session_state(session), ==, CK_SESSION_CORE_CONNECTING);

	uint64_t start = chiaki_time_now_monotonic_ms();
	ck_session_stop(session);
	uint64_t took = chiaki_time_now_monotonic_ms() - start;
	munit_assert_uint64(took, <, 1000);
	munit_assert_int(ck_session_state(session), ==, CK_SESSION_CORE_STOPPED);
	ck_session_stop(session);
	ck_session_free(session);
	fake_ps5_stop(ps5);
	munit_assert_int(watch.quits, ==, 1);
	munit_assert_int(watch.reason, ==, CHIAKI_QUIT_REASON_STOPPED);
	watch_fini(&watch);
	return MUNIT_OK;
}

/** The input thread polls every source at 125 Hz while the session runs, and not after. */
static MunitResult test_input_thread(const MunitParameter params[], void *fixture)
{
	(void)params;
	FakePs5 *ps5 = fixture;
	ps5->session_mode = FAKE_PS5_SESSION_HANG;
	munit_assert_int(fake_ps5_start(ps5), ==, 0);
	Watch watch;
	watch_init(&watch);
	CKSessionCore *session = new_session(&watch);
	FakeSource pad = { .report = true };
	chiaki_controller_state_set_idle(&pad.state);
	pad.state.buttons = CHIAKI_CONTROLLER_BUTTON_CROSS;
	FakeSource touch = { .report = false };
	CKInputSource sources[] = { { "pad", &pad, fake_poll }, { "touch", &touch, fake_poll } };
	munit_assert_int(ck_session_add_input_source(session, &sources[0]), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_int(ck_session_add_input_source(session, &sources[1]), ==, CHIAKI_ERR_SUCCESS);
	for(size_t i = 2; i < CK_SESSION_INPUT_SOURCES_MAX; i++)
		munit_assert_int(ck_session_add_input_source(session, &sources[1]), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_int(ck_session_add_input_source(session, &sources[1]), ==, CHIAKI_ERR_OVERFLOW);
	ck_session_remove_all_input_sources(session);
	munit_assert_int(ck_session_add_input_source(session, &sources[0]), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_int(ck_session_add_input_source(session, &sources[1]), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_int(__atomic_load_n(&pad.polls, __ATOMIC_SEQ_CST), ==, 0);

	munit_assert_int(ck_session_start(session), ==, CHIAKI_ERR_SUCCESS);
	chiaki_sleep_ms(1000);
	int pad_polls = __atomic_load_n(&pad.polls, __ATOMIC_SEQ_CST);
	int touch_polls = __atomic_load_n(&touch.polls, __ATOMIC_SEQ_CST);
	ck_session_stop(session);
	int after = __atomic_load_n(&pad.polls, __ATOMIC_SEQ_CST);
	chiaki_sleep_ms(100);
	munit_assert_int(__atomic_load_n(&pad.polls, __ATOMIC_SEQ_CST), ==, after);
	ck_session_free(session);
	fake_ps5_stop(ps5);
	watch_fini(&watch);
	// 125 per second, with room for a slow machine or qemu.
	if(pad_polls < 80 || pad_polls > 130)
		munit_errorf("%d polls in 1 s", pad_polls);
	munit_assert_int(touch_polls, >=, pad_polls - 1);
	munit_assert_int(touch_polls, <=, pad_polls + 1);
	return MUNIT_OK;
}

/** One session object, started again after each stop. */
static MunitResult test_restart(const MunitParameter params[], void *fixture)
{
	(void)params;
	FakePs5 *ps5 = fixture;
	ps5->session_mode = FAKE_PS5_SESSION_IN_USE;
	munit_assert_int(fake_ps5_start(ps5), ==, 0);
	Watch watch;
	watch_init(&watch);
	CKSessionCore *session = new_session(&watch);
	for(int i = 0; i < 3; i++)
	{
		munit_assert_int(ck_session_start(session), ==, CHIAKI_ERR_SUCCESS);
		munit_assert_true(watch_wait_stopped(&watch, 15000));
		// After the first round there is no stop: the next start, or free, joins the ended stream.
		if(i < 1)
			ck_session_stop(session);
		chiaki_mutex_lock(&watch.mutex);
		munit_assert_int(watch.quits, ==, i + 1);
		munit_assert_size(watch.states_count, ==, 2);
		munit_assert_int(watch.states[0], ==, CK_SESSION_CORE_CONNECTING);
		munit_assert_int(watch.states[1], ==, CK_SESSION_CORE_STOPPED);
		watch.states_count = 0;
		chiaki_mutex_unlock(&watch.mutex);
		CKSessionStats stats;
		ck_session_take_stats(session, &stats);
		munit_assert_uint32(stats.video_frames, ==, 0);
		munit_assert_uint32(stats.audio_frames, ==, 0);
	}
	ck_session_free(session);
	int requests = fake_ps5_session_requests(ps5);
	fake_ps5_stop(ps5);
	watch_fini(&watch);
	munit_assert_int(requests, ==, 3);
	return MUNIT_OK;
}

/** A2.6's bar: open and close 20 times without leaking threads, sockets or memory. */
static MunitResult test_open_close_20(const MunitParameter params[], void *fixture)
{
	(void)params;
	FakePs5 *ps5 = fixture;
	munit_assert_int(fake_ps5_start(ps5), ==, 0);

	// One round first, so what the core sets up once (its log, OpenSSL's tables) is not counted.
	Watch watch;
	watch_init(&watch);
	run_until_quit(&watch);
	watch_fini(&watch);
	size_t fds = count_dir("/proc/self/fd");
	size_t threads = count_dir("/proc/self/task");
	uint32_t resident = ck_resident_kb();

	for(int i = 0; i < 20; i++)
	{
		watch_init(&watch);
		CKSessionCore *session = new_session(&watch);
		munit_assert_int(ck_session_start(session), ==, CHIAKI_ERR_SUCCESS);
		// Half end on their own, half are stopped while connecting.
		if(i % 2)
			munit_assert_true(watch_wait_stopped(&watch, 15000));
		ck_session_stop(session);
		munit_assert_int(ck_session_state(session), ==, CK_SESSION_CORE_STOPPED);
		ck_session_free(session);
		munit_assert_int(watch.quits, ==, 1);
		watch_fini(&watch);
	}
	// Counted with the fake still running: its own sockets are in the baseline.
	size_t fds_after = count_dir("/proc/self/fd");
	size_t threads_after = count_dir("/proc/self/task");
	uint32_t grown = ck_resident_kb() - resident;
	int ctrl = fake_ps5_ctrl_requests(ps5);
	fake_ps5_stop(ps5);

	munit_assert_size(fds_after, ==, fds);
	munit_assert_size(threads_after, ==, threads);
	// A leak of even one session per round (several KB each) would show; allocator noise does not.
	if(CHECK_RESIDENT && grown > 1024)
		munit_errorf("resident memory grew %u KB in 20 rounds", grown);
	munit_assert_int(ctrl, >=, 10);
	return MUNIT_OK;
}

#define TEST(name, fn) { name, fn, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
#define FAKE_TEST(name, fn) { name, fn, setup_fake, teardown_fake, MUNIT_TEST_OPTION_NONE, NULL }

static MunitTest tests[] = {
	TEST("/video_profile", test_video_profile),
	TEST("/input_merge", test_input_merge),
	TEST("/bad_start", test_bad_start),
	TEST("/resident_kb", test_resident_kb),
	FAKE_TEST("/refused", test_refused),
	FAKE_TEST("/in_use", test_in_use),
	FAKE_TEST("/ctrl_refused", test_ctrl_refused),
	FAKE_TEST("/stop_while_connecting", test_stop_while_connecting),
	FAKE_TEST("/input_thread", test_input_thread),
	FAKE_TEST("/restart", test_restart),
	FAKE_TEST("/open_close_20", test_open_close_20),
	{ NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
};

static const MunitSuite suite = { "/session", tests, NULL, 1, MUNIT_SUITE_OPTION_NONE };

int main(int argc, char *argv[])
{
	if(chiaki_lib_init() != CHIAKI_ERR_SUCCESS)
		return 1;
	// Silent: every session here is expected to fail and log it.
	chiaki_log_init(&test_log, 0, NULL, NULL);
	return munit_suite_main(&suite, NULL, argc, argv);
}
