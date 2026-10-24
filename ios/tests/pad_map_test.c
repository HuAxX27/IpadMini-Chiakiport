// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Tests for the pad map (A5.4): button table, stick and trigger dead zones,
// the touchpad switch and the wrapping input source. Run with
// ./ios/tests/input_tests.sh.

#include "ck_ds5.h"
#include "ck_pad_map.h"

#include <stdio.h>
#include <stdlib.h>
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

static ChiakiControllerState idle(void)
{
	ChiakiControllerState s;
	chiaki_controller_state_set_idle(&s);
	return s;
}

static void test_button_index(void)
{
	for(int i = 0; i < CHIAKI_CONTROLLER_BUTTONS_COUNT; i++)
		CHECK_EQ(ck_pad_map_button_index(1u << i), i);
	CHECK_EQ(ck_pad_map_button_index(CHIAKI_CONTROLLER_BUTTON_CROSS), 0);
	CHECK_EQ(ck_pad_map_button_index(CHIAKI_CONTROLLER_BUTTON_PS), 15);
	CHECK_EQ(ck_pad_map_button_index(0), -1);
	CHECK_EQ(ck_pad_map_button_index(CHIAKI_CONTROLLER_BUTTON_CROSS | CHIAKI_CONTROLLER_BUTTON_MOON), -1);
	CHECK_EQ(ck_pad_map_button_index(CHIAKI_CONTROLLER_ANALOG_BUTTON_L2), -1);
	CHECK_EQ(ck_pad_map_button_index(1u << 31), -1);
}

static void test_default_buttons(void)
{
	CKPadMap map;
	ck_pad_map_set_default(&map);
	CHECK(map.touchpad);
	// Every button, PS and the touchpad click included, reaches the console as it is.
	for(int i = 0; i < CHIAKI_CONTROLLER_BUTTONS_COUNT; i++)
	{
		ChiakiControllerState s = idle();
		s.buttons = 1u << i;
		ck_pad_map_apply(&map, &s);
		CHECK_EQ(s.buttons, 1u << i);
		CHECK_EQ(s.l2_state, 0);
		CHECK_EQ(s.r2_state, 0);
	}
	ChiakiControllerState s = idle();
	s.buttons = (1u << CHIAKI_CONTROLLER_BUTTONS_COUNT) - 1u;
	ck_pad_map_apply(&map, &s);
	CHECK_EQ(s.buttons, (1u << CHIAKI_CONTROLLER_BUTTONS_COUNT) - 1u);
}

static void test_remap(void)
{
	CKPadMap map;
	ck_pad_map_set_default(&map);
	// Swap cross and circle, as on Japanese consoles; PS off; R1 as a full R2; L1 presses L1 and L2 both.
	CHECK(ck_pad_map_set_button(&map, CHIAKI_CONTROLLER_BUTTON_CROSS, CHIAKI_CONTROLLER_BUTTON_MOON));
	CHECK(ck_pad_map_set_button(&map, CHIAKI_CONTROLLER_BUTTON_MOON, CHIAKI_CONTROLLER_BUTTON_CROSS));
	CHECK(ck_pad_map_set_button(&map, CHIAKI_CONTROLLER_BUTTON_PS, 0));
	CHECK(ck_pad_map_set_button(&map, CHIAKI_CONTROLLER_BUTTON_R1, CHIAKI_CONTROLLER_ANALOG_BUTTON_R2));
	CHECK(ck_pad_map_set_button(&map, CHIAKI_CONTROLLER_BUTTON_L1,
		CHIAKI_CONTROLLER_BUTTON_L1 | CHIAKI_CONTROLLER_ANALOG_BUTTON_L2));
	// Not one button: refused, nothing changes.
	CHECK(!ck_pad_map_set_button(&map, CHIAKI_CONTROLLER_BUTTON_BOX | CHIAKI_CONTROLLER_BUTTON_PYRAMID, 0));
	CHECK(!ck_pad_map_set_button(&map, CHIAKI_CONTROLLER_ANALOG_BUTTON_L2, CHIAKI_CONTROLLER_BUTTON_BOX));
	// Bits that are neither buttons nor triggers are dropped from the entry.
	CHECK(ck_pad_map_set_button(&map, CHIAKI_CONTROLLER_BUTTON_SHARE, CHIAKI_CONTROLLER_BUTTON_SHARE | (1u << 30)));
	CHECK_EQ(map.buttons[13], CHIAKI_CONTROLLER_BUTTON_SHARE);

	ChiakiControllerState s = idle();
	s.buttons = CHIAKI_CONTROLLER_BUTTON_CROSS;
	ck_pad_map_apply(&map, &s);
	CHECK_EQ(s.buttons, CHIAKI_CONTROLLER_BUTTON_MOON);

	s = idle();
	s.buttons = CHIAKI_CONTROLLER_BUTTON_CROSS | CHIAKI_CONTROLLER_BUTTON_MOON;
	ck_pad_map_apply(&map, &s);
	CHECK_EQ(s.buttons, CHIAKI_CONTROLLER_BUTTON_CROSS | CHIAKI_CONTROLLER_BUTTON_MOON);

	s = idle();
	s.buttons = CHIAKI_CONTROLLER_BUTTON_PS | CHIAKI_CONTROLLER_BUTTON_BOX;
	ck_pad_map_apply(&map, &s);
	CHECK_EQ(s.buttons, CHIAKI_CONTROLLER_BUTTON_BOX);

	// A table entry that presses a trigger: full trigger, and no stray bit in buttons.
	s = idle();
	s.buttons = CHIAKI_CONTROLLER_BUTTON_R1;
	s.r2_state = 40;
	ck_pad_map_apply(&map, &s);
	CHECK_EQ(s.buttons, 0);
	CHECK_EQ(s.r2_state, 255);
	CHECK_EQ(s.l2_state, 0);

	s = idle();
	s.buttons = CHIAKI_CONTROLLER_BUTTON_L1;
	ck_pad_map_apply(&map, &s);
	CHECK_EQ(s.buttons, CHIAKI_CONTROLLER_BUTTON_L1);
	CHECK_EQ(s.l2_state, 255);

	// Released, the trigger goes back to what the pad says.
	s = idle();
	s.r2_state = 40;
	ck_pad_map_apply(&map, &s);
	CHECK_EQ(s.r2_state, 40);
}

static int stick_length_sq(int x, int y)
{
	return x * x + y * y;
}

static void test_stick_deadzone(void)
{
	CKPadMap map;
	ck_pad_map_set_default(&map);
	const int dz = CK_PAD_MAP_STICK_DEADZONE_DEFAULT;

	// Inside the dead zone, on an axis and on a diagonal: 0.
	ChiakiControllerState s = idle();
	s.left_x = (int16_t)dz;
	s.left_y = 0;
	s.right_x = -1100;
	s.right_y = 1100; // length 1556 < 1600
	ck_pad_map_apply(&map, &s);
	CHECK_EQ(s.left_x, 0);
	CHECK_EQ(s.left_y, 0);
	CHECK_EQ(s.right_x, 0);
	CHECK_EQ(s.right_y, 0);

	// Full push on each axis still reads full.
	s = idle();
	s.left_x = 32767;
	s.left_y = -32768;
	s.right_x = -32768;
	s.right_y = 32767;
	ck_pad_map_apply(&map, &s);
	CHECK_EQ(s.left_x, 32767);
	CHECK_EQ(s.left_y, -32768);
	CHECK_EQ(s.right_x, -32768);
	CHECK_EQ(s.right_y, 32767);

	// Just past the edge: small, with no jump.
	s = idle();
	s.left_x = (int16_t)(dz + 10);
	ck_pad_map_apply(&map, &s);
	CHECK(s.left_x > 0 && s.left_x <= 12);
	CHECK_EQ(s.left_y, 0);

	// Halfway between the edge and full reads about half.
	s = idle();
	s.left_y = (int16_t)-(dz + (32767 - dz) / 2);
	ck_pad_map_apply(&map, &s);
	CHECK(s.left_y <= -16380 && s.left_y >= -16388);

	// A diagonal keeps its direction and is never longer than it was stretched to.
	s = idle();
	s.right_x = 12000;
	s.right_y = -9000;
	ck_pad_map_apply(&map, &s);
	CHECK(s.right_x > 0 && s.right_y < 0);
	CHECK(abs(s.right_x * 3 - s.right_y * -4) < 8);
	CHECK(stick_length_sq(s.right_x, s.right_y) < stick_length_sq(12000, 9000));

	// The DualSense's corners, past 32767 in length, clamp per axis without wrapping.
	s = idle();
	s.left_x = 32766;
	s.left_y = 32766;
	s.right_x = -32768;
	s.right_y = -32768;
	ck_pad_map_apply(&map, &s);
	CHECK(s.left_x >= 32000 && s.left_y >= 32000);
	CHECK(s.right_x <= -32000 && s.right_y <= -32000);

	// Along an axis, every raw value maps in order and inside the range.
	int last = -32769;
	for(int v = -32768; v <= 32767; v += 7)
	{
		s = idle();
		s.left_x = (int16_t)v;
		ck_pad_map_apply(&map, &s);
		CHECK(s.left_x >= last);
		last = s.left_x;
	}

	// Dead zone 0 changes nothing; the maximum leaves the stick at 0.
	map.left_deadzone = 0;
	map.right_deadzone = 32767;
	s = idle();
	s.left_x = 37;
	s.left_y = -5;
	s.right_x = 32767;
	s.right_y = -32768;
	ck_pad_map_apply(&map, &s);
	CHECK_EQ(s.left_x, 37);
	CHECK_EQ(s.left_y, -5);
	CHECK_EQ(s.right_x, 0);
	CHECK_EQ(s.right_y, 0);
}

static void test_trigger_deadzone(void)
{
	CKPadMap map;
	ck_pad_map_set_default(&map);
	// The default leaves triggers alone.
	for(int v = 0; v <= 255; v++)
	{
		ChiakiControllerState s = idle();
		s.l2_state = (uint8_t)v;
		s.r2_state = (uint8_t)(255 - v);
		ck_pad_map_apply(&map, &s);
		CHECK_EQ(s.l2_state, v);
		CHECK_EQ(s.r2_state, 255 - v);
	}

	map.l2_deadzone = 10;
	map.r2_deadzone = 255;
	int last = 0;
	for(int v = 0; v <= 255; v++)
	{
		ChiakiControllerState s = idle();
		s.l2_state = (uint8_t)v;
		s.r2_state = (uint8_t)v;
		ck_pad_map_apply(&map, &s);
		if(v <= 10)
			CHECK_EQ(s.l2_state, 0);
		CHECK(s.l2_state >= last);
		last = s.l2_state;
		CHECK_EQ(s.r2_state, 0);
	}
	ChiakiControllerState s = idle();
	s.l2_state = 255;
	ck_pad_map_apply(&map, &s);
	CHECK_EQ(s.l2_state, 255);
	s.l2_state = 11;
	ck_pad_map_apply(&map, &s);
	CHECK_EQ(s.l2_state, 1);
}

static void test_touchpad_and_motion(void)
{
	CKPadMap map;
	ck_pad_map_set_default(&map);
	ChiakiControllerState s = idle();
	s.buttons = CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;
	s.touches[0].id = 5;
	s.touches[0].x = 960;
	s.touches[0].y = 471;
	s.gyro_x = 0.5f;
	s.accel_z = -1.0f;
	ck_pad_map_apply(&map, &s);
	CHECK_EQ(s.buttons, CHIAKI_CONTROLLER_BUTTON_TOUCHPAD);
	CHECK_EQ(s.touches[0].id, 5);
	CHECK_EQ(s.touches[0].x, 960);
	CHECK_EQ(s.touches[0].y, 471);
	CHECK_EQ(s.touches[1].id, -1);
	CHECK(s.gyro_x == 0.5f && s.accel_z == -1.0f);

	// Fingers off: the touches go, the click stays (it is in the table).
	map.touchpad = false;
	ck_pad_map_apply(&map, &s);
	CHECK_EQ(s.buttons, CHIAKI_CONTROLLER_BUTTON_TOUCHPAD);
	CHECK_EQ(s.touches[0].id, -1);
	CHECK_EQ(s.touches[0].x, 0);
	CHECK(s.gyro_x == 0.5f);
}

// A DualSense report at rest, sticks a couple of steps off centre as T8 saw
// them, reads as an idle state once mapped: nothing reaches the console.
static void test_ds5_at_rest(void)
{
	uint8_t r[CK_DS5_BT_SIMPLE_INPUT_SIZE] = { 0x01, 0x7e, 0x82, 0x81, 0x7d, 0x08, 0x00, 0x00, 0x00, 0x00 };
	ChiakiControllerState s = idle();
	CHECK_EQ(ck_ds5_parse_input_report(r, sizeof(r), &s), CK_DS5_REPORT_BT_SIMPLE);
	CHECK(s.left_x != 0 && s.left_y != 0 && s.right_x != 0 && s.right_y != 0);
	CKPadMap map;
	ck_pad_map_set_default(&map);
	ck_pad_map_apply(&map, &s);
	ChiakiControllerState rest = idle();
	CHECK(chiaki_controller_state_equals(&s, &rest));

	// Pushed fully left and down, it still reads full.
	r[1] = 0x00;
	r[2] = 0xff;
	CHECK_EQ(ck_ds5_parse_input_report(r, sizeof(r), &s), CK_DS5_REPORT_BT_SIMPLE);
	ck_pad_map_apply(&map, &s);
	CHECK_EQ(s.left_x, -32768);
	CHECK(s.left_y >= 32700);
}

typedef struct fake_source_t
{
	bool has;
	ChiakiControllerState state;
	int polls;
} FakeSource;

static bool fake_poll(void *user, ChiakiControllerState *state)
{
	FakeSource *fake = user;
	fake->polls++;
	if(!fake->has)
		return false;
	*state = fake->state;
	return true;
}

static void test_source(void)
{
	FakeSource fake = { false, idle(), 0 };
	CKInputSource inner = { "fake", &fake, fake_poll };
	CKPadMap map;
	ck_pad_map_set_default(&map);
	ck_pad_map_set_button(&map, CHIAKI_CONTROLLER_BUTTON_CROSS, CHIAKI_CONTROLLER_BUTTON_MOON);
	CKPadMapSource mapped;
	CKInputSource input = ck_pad_map_source_input(&mapped, inner, &map);
	CHECK(!strcmp(input.name, "fake"));
	CHECK(input.user == &mapped);
	// The map was copied: changing the caller's afterwards does nothing.
	map.left_deadzone = 0;

	ChiakiControllerState s = idle();
	CHECK(!input.poll(input.user, &s));
	CHECK_EQ(fake.polls, 1);

	fake.has = true;
	fake.state.buttons = CHIAKI_CONTROLLER_BUTTON_CROSS;
	fake.state.left_x = 1000;
	CHECK(input.poll(input.user, &s));
	CHECK_EQ(fake.polls, 2);
	CHECK_EQ(s.buttons, CHIAKI_CONTROLLER_BUTTON_MOON);
	CHECK_EQ(s.left_x, 0);
	// The inner source's own state is not touched.
	CHECK_EQ(fake.state.buttons, CHIAKI_CONTROLLER_BUTTON_CROSS);

	// A source with no poll is never connected.
	CKInputSource empty = { "empty", NULL, NULL };
	CKPadMapSource mapped_empty;
	input = ck_pad_map_source_input(&mapped_empty, empty, &map);
	CHECK(!input.poll(input.user, &s));
}

int main(void)
{
	test_button_index();
	test_default_buttons();
	test_remap();
	test_stick_deadzone();
	test_trigger_deadzone();
	test_touchpad_and_motion();
	test_ds5_at_rest();
	test_source();
	if(failures)
	{
		fprintf(stderr, "pad_map_test: %d failures\n", failures);
		return 1;
	}
	printf("pad_map_test: ok\n");
	return 0;
}
