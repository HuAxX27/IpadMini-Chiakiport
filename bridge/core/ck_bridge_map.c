// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "ck_bridge_map.h"

#include <stddef.h>

typedef struct
{
	uint16_t from;
	uint32_t to;
} BitMap;

static const BitMap button_map[] = {
	{ CK_BP32_BUTTON_A, CK_PAD_BUTTON_CROSS },
	{ CK_BP32_BUTTON_B, CK_PAD_BUTTON_MOON },
	{ CK_BP32_BUTTON_X, CK_PAD_BUTTON_BOX },
	{ CK_BP32_BUTTON_Y, CK_PAD_BUTTON_PYRAMID },
	{ CK_BP32_BUTTON_SHOULDER_L, CK_PAD_BUTTON_L1 },
	{ CK_BP32_BUTTON_SHOULDER_R, CK_PAD_BUTTON_R1 },
	{ CK_BP32_BUTTON_THUMB_L, CK_PAD_BUTTON_L3 },
	{ CK_BP32_BUTTON_THUMB_R, CK_PAD_BUTTON_R3 },
};

static const BitMap misc_map[] = {
	{ CK_BP32_MISC_SYSTEM, CK_PAD_BUTTON_PS },
	{ CK_BP32_MISC_SELECT, CK_PAD_BUTTON_SHARE },
	{ CK_BP32_MISC_START, CK_PAD_BUTTON_OPTIONS },
};

static const BitMap dpad_map[] = {
	{ CK_BP32_DPAD_UP, CK_PAD_BUTTON_DPAD_UP },
	{ CK_BP32_DPAD_DOWN, CK_PAD_BUTTON_DPAD_DOWN },
	{ CK_BP32_DPAD_LEFT, CK_PAD_BUTTON_DPAD_LEFT },
	{ CK_BP32_DPAD_RIGHT, CK_PAD_BUTTON_DPAD_RIGHT },
};

static uint32_t map_bits(uint16_t bits, const BitMap *map, size_t count)
{
	uint32_t out = 0;
	for(size_t i = 0; i < count; i++)
	{
		if(bits & map[i].from)
			out |= map[i].to;
	}
	return out;
}

static int16_t map_stick(int32_t axis)
{
	int32_t v = axis * 64;
	if(v > INT16_MAX)
		return INT16_MAX;
	if(v < INT16_MIN)
		return INT16_MIN;
	return (int16_t)v;
}

// Pads without analog triggers only set the digital bit, so it counts as fully pressed.
static uint8_t map_trigger(int32_t pedal, bool digital)
{
	if(pedal <= 0)
		return digital ? 255 : 0;
	if(pedal >= CK_BP32_PEDAL_MAX)
		return 255;
	return (uint8_t)(pedal >> 2);
}

void ck_bridge_map(const CKBridgeGamepad *pad, CKPadPacket *packet)
{
	uint32_t buttons = map_bits(pad->buttons, button_map, sizeof(button_map) / sizeof(button_map[0]))
		| map_bits(pad->misc_buttons, misc_map, sizeof(misc_map) / sizeof(misc_map[0]))
		| map_bits(pad->dpad, dpad_map, sizeof(dpad_map) / sizeof(dpad_map[0]));

	packet->flags = 0;
	if(pad->touchpad_click)
	{
		buttons |= CK_PAD_BUTTON_TOUCHPAD;
		packet->flags |= CK_PAD_FLAG_TOUCHPAD_PRESSED;
	}
	packet->buttons = buttons;
	packet->l2 = map_trigger(pad->brake, pad->buttons & CK_BP32_BUTTON_TRIGGER_L);
	packet->r2 = map_trigger(pad->throttle, pad->buttons & CK_BP32_BUTTON_TRIGGER_R);
	packet->lx = map_stick(pad->axis_x);
	packet->ly = map_stick(pad->axis_y);
	packet->rx = map_stick(pad->axis_rx);
	packet->ry = map_stick(pad->axis_ry);
}
