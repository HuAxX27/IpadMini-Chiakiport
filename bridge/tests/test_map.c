// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "check.h"
#include "ck_bridge_map.h"
#include "ck_pad_codec.h"

#include <chiaki/controller.h>

#include <string.h>

_Static_assert(CK_PAD_BUTTON_CROSS == CHIAKI_CONTROLLER_BUTTON_CROSS, "cross");
_Static_assert(CK_PAD_BUTTON_MOON == CHIAKI_CONTROLLER_BUTTON_MOON, "moon");
_Static_assert(CK_PAD_BUTTON_BOX == CHIAKI_CONTROLLER_BUTTON_BOX, "box");
_Static_assert(CK_PAD_BUTTON_PYRAMID == CHIAKI_CONTROLLER_BUTTON_PYRAMID, "pyramid");
_Static_assert(CK_PAD_BUTTON_DPAD_LEFT == CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT, "dpad left");
_Static_assert(CK_PAD_BUTTON_DPAD_RIGHT == CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT, "dpad right");
_Static_assert(CK_PAD_BUTTON_DPAD_UP == CHIAKI_CONTROLLER_BUTTON_DPAD_UP, "dpad up");
_Static_assert(CK_PAD_BUTTON_DPAD_DOWN == CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN, "dpad down");
_Static_assert(CK_PAD_BUTTON_L1 == CHIAKI_CONTROLLER_BUTTON_L1, "l1");
_Static_assert(CK_PAD_BUTTON_R1 == CHIAKI_CONTROLLER_BUTTON_R1, "r1");
_Static_assert(CK_PAD_BUTTON_L3 == CHIAKI_CONTROLLER_BUTTON_L3, "l3");
_Static_assert(CK_PAD_BUTTON_R3 == CHIAKI_CONTROLLER_BUTTON_R3, "r3");
_Static_assert(CK_PAD_BUTTON_OPTIONS == CHIAKI_CONTROLLER_BUTTON_OPTIONS, "options");
_Static_assert(CK_PAD_BUTTON_SHARE == CHIAKI_CONTROLLER_BUTTON_SHARE, "share");
_Static_assert(CK_PAD_BUTTON_TOUCHPAD == CHIAKI_CONTROLLER_BUTTON_TOUCHPAD, "touchpad");
_Static_assert(CK_PAD_BUTTON_PS == CHIAKI_CONTROLLER_BUTTON_PS, "ps");

static CKPadPacket map(const CKBridgeGamepad *pad)
{
	CKPadPacket p;
	ck_pad_packet_init(&p);
	p.seq = 77;
	ck_bridge_map(pad, &p);
	return p;
}

static void test_neutral(void)
{
	CKBridgeGamepad pad;
	memset(&pad, 0, sizeof(pad));
	CKPadPacket p = map(&pad);
	CKPadPacket idle;
	ck_pad_packet_init(&idle);
	CHECK(ck_pad_packet_same_state(&p, &idle));
	// map() fills the state only.
	CHECK_EQ(p.seq, 77);
	CHECK_EQ(p.magic[0], 'C');
	CHECK_EQ(p.version, CK_PAD_VERSION);
}

static void test_buttons(void)
{
	static const struct
	{
		uint16_t buttons;
		uint8_t misc;
		uint8_t dpad;
		uint32_t expected;
	} cases[] = {
		{ CK_BP32_BUTTON_A, 0, 0, CK_PAD_BUTTON_CROSS },
		{ CK_BP32_BUTTON_B, 0, 0, CK_PAD_BUTTON_MOON },
		{ CK_BP32_BUTTON_X, 0, 0, CK_PAD_BUTTON_BOX },
		{ CK_BP32_BUTTON_Y, 0, 0, CK_PAD_BUTTON_PYRAMID },
		{ CK_BP32_BUTTON_SHOULDER_L, 0, 0, CK_PAD_BUTTON_L1 },
		{ CK_BP32_BUTTON_SHOULDER_R, 0, 0, CK_PAD_BUTTON_R1 },
		{ CK_BP32_BUTTON_THUMB_L, 0, 0, CK_PAD_BUTTON_L3 },
		{ CK_BP32_BUTTON_THUMB_R, 0, 0, CK_PAD_BUTTON_R3 },
		{ 0, CK_BP32_MISC_SYSTEM, 0, CK_PAD_BUTTON_PS },
		{ 0, CK_BP32_MISC_SELECT, 0, CK_PAD_BUTTON_SHARE },
		{ 0, CK_BP32_MISC_START, 0, CK_PAD_BUTTON_OPTIONS },
		{ 0, CK_BP32_MISC_CAPTURE, 0, 0 },
		{ 0, 0, CK_BP32_DPAD_UP, CK_PAD_BUTTON_DPAD_UP },
		{ 0, 0, CK_BP32_DPAD_DOWN, CK_PAD_BUTTON_DPAD_DOWN },
		{ 0, 0, CK_BP32_DPAD_LEFT, CK_PAD_BUTTON_DPAD_LEFT },
		{ 0, 0, CK_BP32_DPAD_RIGHT, CK_PAD_BUTTON_DPAD_RIGHT },
		{ 0, 0, CK_BP32_DPAD_UP | CK_BP32_DPAD_LEFT, CK_PAD_BUTTON_DPAD_UP | CK_PAD_BUTTON_DPAD_LEFT },
		{ CK_BP32_BUTTON_A | CK_BP32_BUTTON_Y, CK_BP32_MISC_START, CK_BP32_DPAD_DOWN,
			CK_PAD_BUTTON_CROSS | CK_PAD_BUTTON_PYRAMID | CK_PAD_BUTTON_OPTIONS | CK_PAD_BUTTON_DPAD_DOWN },
	};
	for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
	{
		CKBridgeGamepad pad;
		memset(&pad, 0, sizeof(pad));
		pad.buttons = cases[i].buttons;
		pad.misc_buttons = cases[i].misc;
		pad.dpad = cases[i].dpad;
		CKPadPacket p = map(&pad);
		CHECK_EQ(p.buttons, cases[i].expected);
		CHECK_EQ(p.flags, 0);
	}
}

static void test_touchpad(void)
{
	CKBridgeGamepad pad;
	memset(&pad, 0, sizeof(pad));
	pad.touchpad_click = true;
	CKPadPacket p = map(&pad);
	CHECK_EQ(p.buttons, CK_PAD_BUTTON_TOUCHPAD);
	CHECK_EQ(p.flags, CK_PAD_FLAG_TOUCHPAD_PRESSED);
}

static void test_sticks(void)
{
	CKBridgeGamepad pad;
	memset(&pad, 0, sizeof(pad));
	pad.axis_x = -512;
	pad.axis_y = 511;
	pad.axis_rx = 512; // the DualSense parser reaches 512: (255 - 127) * 4
	pad.axis_ry = -508;
	CKPadPacket p = map(&pad);
	CHECK_EQ(p.lx, -32768);
	CHECK_EQ(p.ly, 32704);
	CHECK_EQ(p.rx, 32767);
	CHECK_EQ(p.ry, -32512);

	// Same sign as ChiakiControllerState: negative is left and up.
	pad.axis_x = -4;
	pad.axis_y = -100;
	pad.axis_rx = 4;
	pad.axis_ry = 100;
	p = map(&pad);
	CHECK_EQ(p.lx, -256);
	CHECK_EQ(p.ly, -6400);
	CHECK_EQ(p.rx, 256);
	CHECK_EQ(p.ry, 6400);

	pad.axis_x = 100000;
	pad.axis_y = -100000;
	p = map(&pad);
	CHECK_EQ(p.lx, 32767);
	CHECK_EQ(p.ly, -32768);
}

static void test_triggers(void)
{
	CKBridgeGamepad pad;
	memset(&pad, 0, sizeof(pad));
	pad.brake = 1020; // the DualSense parser gives raw * 4
	pad.throttle = 512;
	CKPadPacket p = map(&pad);
	CHECK_EQ(p.l2, 255);
	CHECK_EQ(p.r2, 128);
	CHECK_EQ(p.buttons, 0);

	pad.brake = 4;
	pad.throttle = 5000;
	p = map(&pad);
	CHECK_EQ(p.l2, 1);
	CHECK_EQ(p.r2, 255);

	// Digital trigger bits alone (pads without analog triggers) mean fully pressed;
	// with an analog value the analog value wins.
	pad.brake = 0;
	pad.throttle = 40;
	pad.buttons = CK_BP32_BUTTON_TRIGGER_L | CK_BP32_BUTTON_TRIGGER_R;
	p = map(&pad);
	CHECK_EQ(p.l2, 255);
	CHECK_EQ(p.r2, 10);
	CHECK_EQ(p.buttons, 0);

	pad.buttons = 0;
	pad.brake = -3;
	p = map(&pad);
	CHECK_EQ(p.l2, 0);
}

int main(void)
{
	test_neutral();
	test_buttons();
	test_touchpad();
	test_sticks();
	test_triggers();
	return check_report("test_map");
}
