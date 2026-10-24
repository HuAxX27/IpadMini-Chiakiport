// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Tests for the DualSense input source (A5.8): ck_ds5_source polled the way
// CKSession polls its sources, reading a shared file that this test writes the
// way ckbtfilter does. Run with ./ios/tests/input_tests.sh, which passes
// SOURCE_PATH.

#if !defined(__APPLE__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "ck_ds5_source.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef SOURCE_PATH
#define SOURCE_PATH "build-ios-tests/ds5_source_test.report"
#endif

static int failures = 0;

#define CHECK(cond) do { \
	if(!(cond)) { \
		fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
		failures++; \
	} \
} while(0)

#define CHECK_EQ(a, b) do { \
	long long a_ = (long long)(a), b_ = (long long)(b); \
	if(a_ != b_) { \
		fprintf(stderr, "%s:%d: %s == %lld, expected %s == %lld\n", __FILE__, __LINE__, #a, a_, #b, b_); \
		failures++; \
	} \
} while(0)

// Opens the file as ckbtfilter does when BTServer starts, which says "no pad".
static CKDS5Shared *writer_open(void)
{
	int fd = open(SOURCE_PATH, O_RDWR | O_CREAT, 0644);
	if(fd < 0)
		return NULL;
	void *map = MAP_FAILED;
	if(ftruncate(fd, (off_t)sizeof(CKDS5Shared)) == 0)
		map = mmap(NULL, sizeof(CKDS5Shared), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if(map == MAP_FAILED)
		return NULL;
	ck_ds5_shared_attach(map);
	return map;
}

// The simple report with nothing pressed but face, the full one with nothing
// pressed but face and one finger on the touchpad.
static void write_simple(CKDS5Shared *w, uint8_t face)
{
	uint8_t r[CK_DS5_BT_SIMPLE_INPUT_SIZE] = { 0x01, 0x80, 0x80, 0x80, 0x80 };
	r[5] = (uint8_t)(0x08 | face); // d-pad hat at rest
	ck_ds5_shared_write(w, r, sizeof(r));
}

static void write_full(CKDS5Shared *w, uint8_t face, int8_t touch_id, uint16_t x, uint16_t y, bool bad_crc)
{
	uint8_t r[CK_DS5_BT_INPUT_SIZE] = { 0x31, 0x00, 0x80, 0x80, 0x80, 0x80 };
	r[2 + 7] = (uint8_t)(0x08 | face);
	uint8_t *t = r + 2 + 32;
	t[0] = touch_id < 0 ? 0x80 : (uint8_t)touch_id;
	t[1] = (uint8_t)(x & 0xff);
	t[2] = (uint8_t)(((x >> 8) & 0x0f) | ((y & 0x0f) << 4));
	t[3] = (uint8_t)(y >> 4);
	r[2 + 36] = 0x80; // second finger up
	static const uint8_t hidp_input_header = 0xa1;
	uint32_t crc = ck_ds5_crc32(ck_ds5_crc32(0, &hidp_input_header, 1), r, sizeof(r) - 4);
	if(bad_crc)
		crc ^= 1;
	for(int i = 0; i < 4; i++)
		r[sizeof(r) - 4 + i] = (uint8_t)(crc >> (8 * i));
	ck_ds5_shared_write(w, r, sizeof(r));
}

#define CROSS 0x20
#define SQUARE 0x10

// Polls until the source looks at the file again; returns what the last poll said.
static bool poll_until_check(CKDS5Source *source, ChiakiControllerState *state)
{
	bool r = false;
	for(int i = 0; i < CK_DS5_SOURCE_CHECK_POLLS; i++)
		r = ck_ds5_source_poll(source, state);
	return r;
}

int main(void)
{
	unlink(SOURCE_PATH);
	static CKDS5Source source;
	ck_ds5_source_init(&source, SOURCE_PATH);
	CKInputSource input = ck_ds5_source_input(&source);
	CHECK(strcmp(input.name, CK_DS5_SOURCE_NAME) == 0);
	CHECK(input.user == &source);
	CHECK(input.poll == ck_ds5_source_poll);

	// No file yet: nothing to report, and no file is made.
	ChiakiControllerState state;
	CHECK(!input.poll(input.user, &state));
	CHECK(source.shared == NULL);
	CHECK(access(SOURCE_PATH, F_OK) != 0);

	// BTServer starts: the source finds the file within a second, with no pad.
	CKDS5Shared *w = writer_open();
	CHECK(w != NULL);
	if(!w)
		return 1;
	CHECK(!input.poll(input.user, &state)); // not looked yet
	CHECK(!poll_until_check(&source, &state));
	CHECK(source.shared != NULL);
	CHECK_EQ(source.maps, 1);

	// The pad sends a simple report with cross: it stays held until the next report.
	write_simple(w, CROSS);
	CHECK(input.poll(input.user, &state));
	CHECK_EQ(state.buttons, CHIAKI_CONTROLLER_BUTTON_CROSS);
	CHECK_EQ(state.touches[0].id, -1);
	CHECK_EQ(source.kind, CK_DS5_REPORT_BT_SIMPLE);
	for(int i = 0; i < 3 * CK_DS5_SOURCE_CHECK_POLLS; i++)
		if(!input.poll(input.user, &state) || state.buttons != CHIAKI_CONTROLLER_BUTTON_CROSS)
		{
			fprintf(stderr, "ds5_source_test: cross let go after %d polls with no new report\n", i);
			failures++;
			break;
		}

	// The full report: square and a finger in the middle of the touchpad.
	write_full(w, SQUARE, 5, 960, 540, false);
	CHECK(input.poll(input.user, &state));
	CHECK_EQ(state.buttons, CHIAKI_CONTROLLER_BUTTON_BOX);
	CHECK_EQ(state.touches[0].id, 5);
	CHECK_EQ(state.touches[0].x, 960);
	CHECK_EQ(state.touches[0].y, 540 * (CK_DS5_CHIAKI_TOUCHPAD_HEIGHT - 1) / (CK_DS5_TOUCHPAD_HEIGHT - 1));
	CHECK_EQ(state.touches[1].id, -1);
	CHECK_EQ(source.kind, CK_DS5_REPORT_BT_FULL);

	// A report with a bad CRC changes nothing.
	write_full(w, CROSS, -1, 0, 0, true);
	CHECK(input.poll(input.user, &state));
	CHECK_EQ(state.buttons, CHIAKI_CONTROLLER_BUTTON_BOX);
	CHECK_EQ(state.touches[0].id, 5);
	CHECK_EQ(source.kinds[CK_DS5_REPORT_BAD_CRC], 1);
	CHECK_EQ(source.kinds[CK_DS5_REPORT_BT_FULL], 1);
	CHECK_EQ(source.kinds[CK_DS5_REPORT_BT_SIMPLE], 1);

	// The pad leaves: nothing to report, and the state goes back to idle.
	ck_ds5_shared_write(w, NULL, 0);
	CHECK(!input.poll(input.user, &state));
	CHECK(!input.poll(input.user, &state));
	CHECK_EQ(source.state.buttons, 0);
	CHECK_EQ(source.state.touches[0].id, -1);

	// It comes back, and BTServer then dies with cross held. Its successor
	// makes a new file: within a second the source moves to it and lets go.
	write_simple(w, CROSS);
	CHECK(input.poll(input.user, &state));
	CHECK(unlink(SOURCE_PATH) == 0);
	CKDS5Shared *w2 = writer_open();
	CHECK(w2 != NULL);
	if(!w2)
		return 1;
	CHECK(!poll_until_check(&source, &state));
	CHECK_EQ(source.maps, 2);
	CHECK_EQ(source.state.buttons, 0);
	write_simple(w2, SQUARE);
	CHECK(input.poll(input.user, &state));
	CHECK_EQ(state.buttons, CHIAKI_CONTROLLER_BUTTON_BOX);

	// A BTServer that restarts with the same file says "no pad" when it attaches.
	ck_ds5_shared_attach(w2);
	CHECK(!input.poll(input.user, &state));
	CHECK_EQ(source.maps, 2);

	ck_ds5_source_fini(&source);
	CHECK(source.shared == NULL);
	munmap(w, sizeof(CKDS5Shared));
	munmap(w2, sizeof(CKDS5Shared));
	unlink(SOURCE_PATH);

	if(failures)
	{
		fprintf(stderr, "ds5_source_test: %d failure(s)\n", failures);
		return 1;
	}
	printf("ds5_source_test: ok\n");
	return 0;
}
