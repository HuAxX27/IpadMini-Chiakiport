// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Tests for the DualSense report parser (A5.7). Runs on any host:
//
//   cc -std=c11 -Wall -Wextra -Ilib/include -Iios/Input -o /tmp/ds5_test ios/tests/ds5_test.c ios/Input/ck_ds5.c lib/src/controller.c
//   /tmp/ds5_test

#include "ck_ds5.h"

#include <stdio.h>
#include <string.h>

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

// Bluetooth report 0x31 with its CRC-32 computed independently, by Python's
// zlib.crc32(b'\xa1' + report[:74]): hat left, square, circle, R1, L2 digital,
// create, L3; left stick full up, right stick full right; L2 fully pressed;
// one finger (ID 0x11) at the touchpad's centre, 960 x 540.
static const uint8_t bt_full_report[CK_DS5_BT_INPUT_SIZE] = {
	0x31, 0x51, 0x80, 0x00, 0xff, 0x80, 0xff, 0x00, 0x2a, 0x56, 0x56, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0xc0,
	0xc3, 0x21, 0x90, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0xa2, 0x56, 0x2f, 0x2c,
};

static void test_crc32(void)
{
	const uint8_t check[] = "123456789";
	CHECK_EQ(ck_ds5_crc32(0, check, 9), 0xcbf43926u);
	// Chaining gives the same result as one pass.
	CHECK_EQ(ck_ds5_crc32(ck_ds5_crc32(0, check, 4), check + 4, 5), 0xcbf43926u);
	CHECK(ck_ds5_bt_crc_ok(bt_full_report, sizeof(bt_full_report)));
}

static void test_usb_full(void)
{
	uint8_t r[CK_DS5_USB_INPUT_SIZE] = { 0 };
	r[0] = 0x01;
	r[1] = 0x00; // LX full left
	r[2] = 0xff; // LY full down
	r[3] = 0x80; // RX centre
	r[4] = 0x40; // RY half up
	r[5] = 200;  // L2
	r[6] = 10;   // R2
	r[8] = 0x02 | 0x20 | 0x80; // hat right, cross, triangle
	r[9] = 0x01 | 0x20 | 0x80; // L1, options, R3
	r[10] = 0x01 | 0x02 | 0x04; // PS, touchpad click, mute
	// Touch 0: finger down, ID 5, bottom-right corner (1919, 1079).
	r[33] = 0x05;
	r[34] = 0x7f;
	r[35] = 0x07 | (0x07 << 4);
	r[36] = 0x43;
	// Touch 1: finger up.
	r[37] = 0x80 | 0x03;

	ChiakiControllerState s;
	chiaki_controller_state_set_idle(&s);
	CHECK_EQ(ck_ds5_parse_input_report(r, sizeof(r), &s), CK_DS5_REPORT_USB);
	CHECK_EQ(s.buttons, CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT | CHIAKI_CONTROLLER_BUTTON_CROSS
		| CHIAKI_CONTROLLER_BUTTON_PYRAMID | CHIAKI_CONTROLLER_BUTTON_L1 | CHIAKI_CONTROLLER_BUTTON_OPTIONS
		| CHIAKI_CONTROLLER_BUTTON_R3 | CHIAKI_CONTROLLER_BUTTON_PS | CHIAKI_CONTROLLER_BUTTON_TOUCHPAD);
	CHECK_EQ(s.left_x, -32768);
	CHECK_EQ(s.left_y, 32766);
	CHECK_EQ(s.right_x, 0);
	CHECK_EQ(s.right_y, -16384);
	CHECK_EQ(s.l2_state, 200);
	CHECK_EQ(s.r2_state, 10);
	CHECK_EQ(s.touches[0].id, 5);
	CHECK_EQ(s.touches[0].x, 1919);
	CHECK_EQ(s.touches[0].y, CK_DS5_CHIAKI_TOUCHPAD_HEIGHT - 1);
	CHECK_EQ(s.touches[1].id, -1);
	// Motion stays as set_idle left it.
	CHECK(s.accel_y == 1.0f);
	CHECK(s.orient_w == 1.0f);
}

static void test_bt_full(void)
{
	ChiakiControllerState s;
	chiaki_controller_state_set_idle(&s);
	CHECK_EQ(ck_ds5_parse_input_report(bt_full_report, sizeof(bt_full_report), &s), CK_DS5_REPORT_BT_FULL);
	CHECK_EQ(s.buttons, CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT | CHIAKI_CONTROLLER_BUTTON_BOX
		| CHIAKI_CONTROLLER_BUTTON_MOON | CHIAKI_CONTROLLER_BUTTON_R1 | CHIAKI_CONTROLLER_BUTTON_SHARE
		| CHIAKI_CONTROLLER_BUTTON_L3);
	CHECK_EQ(s.left_x, 0);
	CHECK_EQ(s.left_y, -32768);
	CHECK_EQ(s.right_x, 32766);
	CHECK_EQ(s.right_y, 0);
	CHECK_EQ(s.l2_state, 255);
	CHECK_EQ(s.r2_state, 0);
	CHECK_EQ(s.touches[0].id, 0x11);
	CHECK_EQ(s.touches[0].x, 960);
	CHECK_EQ(s.touches[0].y, 540 * (CK_DS5_CHIAKI_TOUCHPAD_HEIGHT - 1) / (CK_DS5_TOUCHPAD_HEIGHT - 1));
	CHECK_EQ(s.touches[1].id, -1);
}

static void test_bt_bad_crc_keeps_state(void)
{
	uint8_t r[CK_DS5_BT_INPUT_SIZE];
	memcpy(r, bt_full_report, sizeof(r));
	r[9] ^= 0x20; // press cross without fixing the CRC

	ChiakiControllerState s, before;
	chiaki_controller_state_set_idle(&s);
	s.buttons = CHIAKI_CONTROLLER_BUTTON_PS;
	before = s;
	CHECK_EQ(ck_ds5_parse_input_report(r, sizeof(r), &s), CK_DS5_REPORT_BAD_CRC);
	CHECK(memcmp(&s, &before, sizeof(s)) == 0);
}

static void test_bt_simple(void)
{
	// Report ID, LX, LY, RX, RY, three button bytes, L2, R2. Here: no hat, cross,
	// R2 digital (ignored, the trigger byte counts), PS, and a counter in the top
	// six bits of the third button byte.
	uint8_t r[CK_DS5_BT_SIMPLE_INPUT_SIZE] = { 0x01, 0xff, 0x80, 0x00, 0x80, 0x08 | 0x20, 0x08, 0xfc | 0x01, 0x00, 0x7f };

	ChiakiControllerState s;
	chiaki_controller_state_set_idle(&s);
	s.touches[0].id = 3; // left over from an earlier full report
	CHECK_EQ(ck_ds5_parse_input_report(r, sizeof(r), &s), CK_DS5_REPORT_BT_SIMPLE);
	CHECK_EQ(s.buttons, CHIAKI_CONTROLLER_BUTTON_CROSS | CHIAKI_CONTROLLER_BUTTON_PS);
	CHECK_EQ(s.left_x, 32766);
	CHECK_EQ(s.right_x, -32768);
	CHECK_EQ(s.l2_state, 0);
	CHECK_EQ(s.r2_state, 0x7f);
	CHECK_EQ(s.touches[0].id, -1);

	// The same report padded to 78 bytes, as some HID stacks deliver it.
	uint8_t padded[CK_DS5_BT_INPUT_SIZE] = { 0 };
	memcpy(padded, r, sizeof(r));
	chiaki_controller_state_set_idle(&s);
	CHECK_EQ(ck_ds5_parse_input_report(padded, sizeof(padded), &s), CK_DS5_REPORT_BT_SIMPLE);
	CHECK_EQ(s.r2_state, 0x7f);
}

static void test_hat(void)
{
	static const uint32_t expected[16] = {
		CHIAKI_CONTROLLER_BUTTON_DPAD_UP,
		CHIAKI_CONTROLLER_BUTTON_DPAD_UP | CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT,
		CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT,
		CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN | CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT,
		CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN,
		CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN | CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT,
		CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT,
		CHIAKI_CONTROLLER_BUTTON_DPAD_UP | CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT,
		// 8 is "not pressed"; 9 to 15 never come from a working pad and also mean nothing.
	};
	for(uint8_t hat = 0; hat < 16; hat++)
	{
		uint8_t r[CK_DS5_USB_INPUT_SIZE] = { 0x01, 0x80, 0x80, 0x80, 0x80 };
		r[8] = hat;
		ChiakiControllerState s;
		chiaki_controller_state_set_idle(&s);
		CHECK_EQ(ck_ds5_parse_input_report(r, sizeof(r), &s), CK_DS5_REPORT_USB);
		CHECK_EQ(s.buttons, expected[hat]);
	}
}

static void test_sticks(void)
{
	static const struct { uint8_t raw; int16_t axis; } cases[] = {
		{ 0x00, -32768 }, { 0x01, -32512 }, { 0x7f, -256 }, { 0x80, 0 }, { 0x81, 258 }, { 0xff, 32766 },
	};
	for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
	{
		uint8_t r[CK_DS5_USB_INPUT_SIZE] = { 0x01, cases[i].raw, 0x80, 0x80, cases[i].raw };
		r[8] = 0x08;
		ChiakiControllerState s;
		chiaki_controller_state_set_idle(&s);
		ck_ds5_parse_input_report(r, sizeof(r), &s);
		CHECK_EQ(s.left_x, cases[i].axis);
		CHECK_EQ(s.right_y, cases[i].axis);
		CHECK_EQ(s.left_y, 0);
	}
}

static void test_rejects(void)
{
	ChiakiControllerState s, before;
	chiaki_controller_state_set_idle(&s);
	s.buttons = CHIAKI_CONTROLLER_BUTTON_CROSS;
	before = s;

	uint8_t other_id[CK_DS5_USB_INPUT_SIZE] = { 0x02 };
	uint8_t short_usb[20] = { 0x01 };
	uint8_t short_bt[CK_DS5_BT_INPUT_SIZE - 1] = { 0x31 };

	CHECK_EQ(ck_ds5_parse_input_report(NULL, 64, &s), CK_DS5_REPORT_UNKNOWN);
	CHECK_EQ(ck_ds5_parse_input_report(other_id, 0, &s), CK_DS5_REPORT_UNKNOWN);
	CHECK_EQ(ck_ds5_parse_input_report(other_id, sizeof(other_id), &s), CK_DS5_REPORT_UNKNOWN);
	CHECK_EQ(ck_ds5_parse_input_report(short_usb, sizeof(short_usb), &s), CK_DS5_REPORT_UNKNOWN);
	CHECK_EQ(ck_ds5_parse_input_report(short_bt, sizeof(short_bt), &s), CK_DS5_REPORT_UNKNOWN);
	CHECK(!ck_ds5_bt_crc_ok(short_bt, 4));
	CHECK(memcmp(&s, &before, sizeof(s)) == 0);
}

// The two output reports ckbtfilter sends, with CRCs computed independently by
// Python's zlib.crc32(b'\xa2' + report[:74]).
static void test_bt_output(void)
{
	uint8_t r[CK_DS5_BT_OUTPUT_SIZE];
	CKDS5Output o;

	memset(&o, 0, sizeof(o));
	o.lightbar_fade_out = true;
	ck_ds5_bt_output_report(r, &o);
	static const uint8_t fade_out_crc[4] = { 0x43, 0xe6, 0x31, 0x36 };
	CHECK_EQ(r[0], CK_DS5_REPORT_ID_BT_OUTPUT);
	CHECK_EQ(r[1], 0x00); // sequence 0
	CHECK_EQ(r[2], 0x10); // tag
	CHECK_EQ(r[4], 0x00); // valid_flag1: nothing
	CHECK_EQ(r[41], 0x02); // valid_flag2: lightbar setup
	CHECK_EQ(r[44], 0x02); // lightbar setup: fade out
	CHECK(memcmp(r + 74, fade_out_crc, 4) == 0);

	memset(&o, 0, sizeof(o));
	o.seq = 1;
	o.set_lightbar = true;
	o.blue = 0x80;
	o.set_player_leds = true;
	o.player_leds = CK_DS5_PLAYER_LEDS_1;
	ck_ds5_bt_output_report(r, &o);
	static const uint8_t leds_crc[4] = { 0x41, 0x42, 0x09, 0xdc };
	CHECK_EQ(r[1], 0x10); // sequence 1, high nibble
	CHECK_EQ(r[4], 0x14); // valid_flag1: lightbar and player LEDs
	CHECK_EQ(r[41], 0x00);
	CHECK_EQ(r[46], 0x04);
	CHECK_EQ(r[47], 0x00);
	CHECK_EQ(r[48], 0x00);
	CHECK_EQ(r[49], 0x80);
	CHECK(memcmp(r + 74, leds_crc, 4) == 0);

	// The sequence wraps at 16 and the LEDs keep to their five bits.
	o.seq = 17;
	o.player_leds = 0xff;
	ck_ds5_bt_output_report(r, &o);
	CHECK_EQ(r[1], 0x10);
	CHECK_EQ(r[46], 0x1f);
}

int main(void)
{
	test_crc32();
	test_usb_full();
	test_bt_full();
	test_bt_bad_crc_keeps_state();
	test_bt_simple();
	test_hat();
	test_sticks();
	test_rejects();
	test_bt_output();
	if(failures)
	{
		fprintf(stderr, "ds5_test: %d failure(s)\n", failures);
		return 1;
	}
	printf("ds5_test: ok\n");
	return 0;
}
