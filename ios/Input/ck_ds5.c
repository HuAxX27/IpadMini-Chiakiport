// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "ck_ds5.h"

#include <string.h>

// Offsets inside the common part of the full report, which starts right after
// the report ID on USB and after the report ID and a sequence byte on Bluetooth.
#define FULL_STICKS 0
#define FULL_L2 4
#define FULL_R2 5
#define FULL_BUTTONS 7
#define FULL_TOUCH_0 32
#define FULL_TOUCH_1 36
#define FULL_SIZE 40

// Offsets inside the Bluetooth simple report, right after the report ID.
#define SIMPLE_STICKS 0
#define SIMPLE_BUTTONS 4
#define SIMPLE_L2 7
#define SIMPLE_R2 8

// Bluetooth output report: report ID, sequence in the high nibble, a tag
// byte, then the part shared with USB, then reserved bytes and the CRC.
#define OUT_SEQ 1
#define OUT_TAG 2
#define OUT_TAG_VALUE 0x10
#define OUT_COMMON 3
#define OUT_VALID_FLAG1 (OUT_COMMON + 1)
#define OUT_VALID_FLAG1_LIGHTBAR 0x04
#define OUT_VALID_FLAG1_PLAYER_LEDS 0x10
#define OUT_VALID_FLAG2 (OUT_COMMON + 38)
#define OUT_VALID_FLAG2_LIGHTBAR_SETUP 0x02
#define OUT_LIGHTBAR_SETUP (OUT_COMMON + 41)
#define OUT_LIGHTBAR_SETUP_LIGHT_OUT 0x02
#define OUT_PLAYER_LEDS (OUT_COMMON + 43)
#define OUT_LIGHTBAR_RGB (OUT_COMMON + 44)

// Some HID stacks pad the Bluetooth simple report up to the size of the full one.
#define BT_SIMPLE_PADDED_SIZE CK_DS5_BT_INPUT_SIZE

#define HAT_NONE 8

static const uint32_t hat_buttons[HAT_NONE] = {
	CHIAKI_CONTROLLER_BUTTON_DPAD_UP,
	CHIAKI_CONTROLLER_BUTTON_DPAD_UP | CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT,
	CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT,
	CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN | CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT,
	CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN,
	CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN | CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT,
	CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT,
	CHIAKI_CONTROLLER_BUTTON_DPAD_UP | CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT,
};

// 0 -> -32768, 128 -> 0, 255 -> 32766. Up and left are negative, as in Chiaki.
static int16_t stick_axis(uint8_t raw)
{
	int v = (int)raw - 128;
	return (int16_t)(v < 0 ? v * 256 : v * 258);
}

static void parse_sticks(const uint8_t *p, ChiakiControllerState *state)
{
	state->left_x = stick_axis(p[0]);
	state->left_y = stick_axis(p[1]);
	state->right_x = stick_axis(p[2]);
	state->right_y = stick_axis(p[3]);
}

// b[0]: d-pad hat in the low nibble, then square, cross, circle, triangle.
// b[1]: L1, R1, L2, R2, create, options, L3, R3. L2 and R2 come from the triggers.
// b[2]: PS, touchpad click, mute. Chiaki has no mute button.
static uint32_t parse_buttons(const uint8_t *b)
{
	uint32_t buttons = 0;
	uint8_t hat = b[0] & 0x0f;
	if(hat < HAT_NONE)
		buttons |= hat_buttons[hat];
	if(b[0] & 0x10) buttons |= CHIAKI_CONTROLLER_BUTTON_BOX;
	if(b[0] & 0x20) buttons |= CHIAKI_CONTROLLER_BUTTON_CROSS;
	if(b[0] & 0x40) buttons |= CHIAKI_CONTROLLER_BUTTON_MOON;
	if(b[0] & 0x80) buttons |= CHIAKI_CONTROLLER_BUTTON_PYRAMID;
	if(b[1] & 0x01) buttons |= CHIAKI_CONTROLLER_BUTTON_L1;
	if(b[1] & 0x02) buttons |= CHIAKI_CONTROLLER_BUTTON_R1;
	if(b[1] & 0x10) buttons |= CHIAKI_CONTROLLER_BUTTON_SHARE;
	if(b[1] & 0x20) buttons |= CHIAKI_CONTROLLER_BUTTON_OPTIONS;
	if(b[1] & 0x40) buttons |= CHIAKI_CONTROLLER_BUTTON_L3;
	if(b[1] & 0x80) buttons |= CHIAKI_CONTROLLER_BUTTON_R3;
	if(b[2] & 0x01) buttons |= CHIAKI_CONTROLLER_BUTTON_PS;
	if(b[2] & 0x02) buttons |= CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;
	return buttons;
}

// t[0]: bit 7 set when the finger is up, touch ID in the low 7 bits (Chiaki's
// touch IDs are 7 bits too). t[1..3]: X and Y, 12 bits each.
static void parse_touch(const uint8_t *t, ChiakiControllerTouch *touch)
{
	if(t[0] & 0x80)
	{
		touch->id = -1;
		touch->x = 0;
		touch->y = 0;
		return;
	}
	uint32_t x = t[1] | ((uint32_t)(t[2] & 0x0f) << 8);
	uint32_t y = (t[2] >> 4) | ((uint32_t)t[3] << 4);
	if(y > CK_DS5_TOUCHPAD_HEIGHT - 1)
		y = CK_DS5_TOUCHPAD_HEIGHT - 1;
	touch->id = (int8_t)(t[0] & 0x7f);
	touch->x = (uint16_t)x;
	touch->y = (uint16_t)(y * (CK_DS5_CHIAKI_TOUCHPAD_HEIGHT - 1) / (CK_DS5_TOUCHPAD_HEIGHT - 1));
}

static void clear_touches(ChiakiControllerState *state)
{
	for(size_t i = 0; i < CHIAKI_CONTROLLER_TOUCHES_MAX; i++)
	{
		state->touches[i].id = -1;
		state->touches[i].x = 0;
		state->touches[i].y = 0;
	}
}

static void parse_full(const uint8_t *p, ChiakiControllerState *state)
{
	parse_sticks(p + FULL_STICKS, state);
	state->l2_state = p[FULL_L2];
	state->r2_state = p[FULL_R2];
	state->buttons = parse_buttons(p + FULL_BUTTONS);
	parse_touch(p + FULL_TOUCH_0, &state->touches[0]);
	parse_touch(p + FULL_TOUCH_1, &state->touches[1]);
}

static void parse_simple(const uint8_t *p, ChiakiControllerState *state)
{
	parse_sticks(p + SIMPLE_STICKS, state);
	state->l2_state = p[SIMPLE_L2];
	state->r2_state = p[SIMPLE_R2];
	state->buttons = parse_buttons(p + SIMPLE_BUTTONS);
	clear_touches(state);
}

CKDS5ReportKind ck_ds5_parse_input_report(const uint8_t *report, size_t len, ChiakiControllerState *state)
{
	if(!report || len < 1)
		return CK_DS5_REPORT_UNKNOWN;

	switch(report[0])
	{
		case CK_DS5_REPORT_ID_INPUT:
			if(len == CK_DS5_BT_SIMPLE_INPUT_SIZE || len == BT_SIMPLE_PADDED_SIZE)
			{
				parse_simple(report + 1, state);
				return CK_DS5_REPORT_BT_SIMPLE;
			}
			if(len >= 1 + FULL_SIZE)
			{
				parse_full(report + 1, state);
				return CK_DS5_REPORT_USB;
			}
			return CK_DS5_REPORT_UNKNOWN;
		case CK_DS5_REPORT_ID_BT_INPUT:
			if(len != CK_DS5_BT_INPUT_SIZE)
				return CK_DS5_REPORT_UNKNOWN;
			if(!ck_ds5_bt_crc_ok(report, len))
				return CK_DS5_REPORT_BAD_CRC;
			parse_full(report + 2, state);
			return CK_DS5_REPORT_BT_FULL;
		default:
			return CK_DS5_REPORT_UNKNOWN;
	}
}

void ck_ds5_bt_output_report(uint8_t report[CK_DS5_BT_OUTPUT_SIZE], const CKDS5Output *output)
{
	static const uint8_t hidp_output_header = 0xa2;
	memset(report, 0, CK_DS5_BT_OUTPUT_SIZE);
	report[0] = CK_DS5_REPORT_ID_BT_OUTPUT;
	report[OUT_SEQ] = (uint8_t)((output->seq & 0x0f) << 4);
	report[OUT_TAG] = OUT_TAG_VALUE;
	if(output->lightbar_fade_out)
	{
		report[OUT_VALID_FLAG2] |= OUT_VALID_FLAG2_LIGHTBAR_SETUP;
		report[OUT_LIGHTBAR_SETUP] = OUT_LIGHTBAR_SETUP_LIGHT_OUT;
	}
	if(output->set_lightbar)
	{
		report[OUT_VALID_FLAG1] |= OUT_VALID_FLAG1_LIGHTBAR;
		report[OUT_LIGHTBAR_RGB] = output->red;
		report[OUT_LIGHTBAR_RGB + 1] = output->green;
		report[OUT_LIGHTBAR_RGB + 2] = output->blue;
	}
	if(output->set_player_leds)
	{
		report[OUT_VALID_FLAG1] |= OUT_VALID_FLAG1_PLAYER_LEDS;
		report[OUT_PLAYER_LEDS] = output->player_leds & 0x1f;
	}
	uint32_t crc = ck_ds5_crc32(0, &hidp_output_header, 1);
	crc = ck_ds5_crc32(crc, report, CK_DS5_BT_OUTPUT_SIZE - 4);
	for(int i = 0; i < 4; i++)
		report[CK_DS5_BT_OUTPUT_SIZE - 4 + i] = (uint8_t)(crc >> (8 * i));
}

uint32_t ck_ds5_crc32(uint32_t crc, const uint8_t *buf, size_t len)
{
	crc = ~crc;
	for(size_t i = 0; i < len; i++)
	{
		crc ^= buf[i];
		for(int bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
	}
	return ~crc;
}

bool ck_ds5_bt_crc_ok(const uint8_t *report, size_t len)
{
	static const uint8_t hidp_input_header = 0xa1;
	if(!report || len < 5)
		return false;
	uint32_t crc = ck_ds5_crc32(0, &hidp_input_header, 1);
	crc = ck_ds5_crc32(crc, report, len - 4);
	const uint8_t *tail = report + len - 4;
	uint32_t expected = (uint32_t)tail[0]
		| ((uint32_t)tail[1] << 8)
		| ((uint32_t)tail[2] << 16)
		| ((uint32_t)tail[3] << 24);
	return crc == expected;
}
