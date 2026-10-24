// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "ck_pad_map.h"

#include <math.h>

#define STICK_FULL 32767.0f
#define TRIGGER_FULL 255

// Bits a table entry may press: the 16 buttons and the two full triggers.
#define BUTTONS_MASK ((1u << CHIAKI_CONTROLLER_BUTTONS_COUNT) - 1u)
#define TABLE_MASK (BUTTONS_MASK | CHIAKI_CONTROLLER_ANALOG_BUTTON_L2 | CHIAKI_CONTROLLER_ANALOG_BUTTON_R2)

void ck_pad_map_set_default(CKPadMap *map)
{
	for(int i = 0; i < CHIAKI_CONTROLLER_BUTTONS_COUNT; i++)
		map->buttons[i] = 1u << i;
	map->left_deadzone = CK_PAD_MAP_STICK_DEADZONE_DEFAULT;
	map->right_deadzone = CK_PAD_MAP_STICK_DEADZONE_DEFAULT;
	map->l2_deadzone = CK_PAD_MAP_TRIGGER_DEADZONE_DEFAULT;
	map->r2_deadzone = CK_PAD_MAP_TRIGGER_DEADZONE_DEFAULT;
	map->touchpad = true;
}

int ck_pad_map_button_index(uint32_t button)
{
	if(button == 0 || (button & ~BUTTONS_MASK) || (button & (button - 1)))
		return -1;
	int i = 0;
	while(!(button & 1u))
	{
		button >>= 1;
		i++;
	}
	return i;
}

bool ck_pad_map_set_button(CKPadMap *map, uint32_t from, uint32_t to)
{
	int i = ck_pad_map_button_index(from);
	if(i < 0)
		return false;
	map->buttons[i] = to & TABLE_MASK;
	return true;
}

static int16_t clamp_axis(float v)
{
	if(v >= STICK_FULL)
		return 32767;
	if(v <= -STICK_FULL - 1.0f)
		return -32768;
	return (int16_t)(v < 0.0f ? v - 0.5f : v + 0.5f);
}

// Scaled radial dead zone: the length of the stick's vector loses deadzone
// and is stretched so that STICK_FULL stays STICK_FULL. The direction is kept,
// so a diagonal stays a diagonal.
static void stick_deadzone(int16_t *x, int16_t *y, uint16_t deadzone)
{
	if(deadzone == 0)
		return;
	float fx = *x, fy = *y;
	float r = sqrtf(fx * fx + fy * fy);
	if(r <= (float)deadzone || deadzone >= 32767)
	{
		*x = 0;
		*y = 0;
		return;
	}
	float scale = (r - (float)deadzone) / r * (STICK_FULL / (STICK_FULL - (float)deadzone));
	*x = clamp_axis(fx * scale);
	*y = clamp_axis(fy * scale);
}

static uint8_t trigger_deadzone(uint8_t v, uint8_t deadzone)
{
	if(deadzone == 0)
		return v;
	if(v <= deadzone || deadzone >= TRIGGER_FULL)
		return 0;
	unsigned span = TRIGGER_FULL - deadzone;
	return (uint8_t)(((unsigned)(v - deadzone) * TRIGGER_FULL + span / 2) / span);
}

void ck_pad_map_apply(const CKPadMap *map, ChiakiControllerState *state)
{
	uint32_t in = state->buttons;
	uint32_t out = 0;
	for(int i = 0; i < CHIAKI_CONTROLLER_BUTTONS_COUNT; i++)
	{
		if(in & (1u << i))
			out |= map->buttons[i];
	}

	stick_deadzone(&state->left_x, &state->left_y, map->left_deadzone);
	stick_deadzone(&state->right_x, &state->right_y, map->right_deadzone);
	state->l2_state = trigger_deadzone(state->l2_state, map->l2_deadzone);
	state->r2_state = trigger_deadzone(state->r2_state, map->r2_deadzone);
	if(out & CHIAKI_CONTROLLER_ANALOG_BUTTON_L2)
		state->l2_state = TRIGGER_FULL;
	if(out & CHIAKI_CONTROLLER_ANALOG_BUTTON_R2)
		state->r2_state = TRIGGER_FULL;
	state->buttons = out & BUTTONS_MASK;

	if(!map->touchpad)
	{
		for(int i = 0; i < CHIAKI_CONTROLLER_TOUCHES_MAX; i++)
		{
			state->touches[i].id = -1;
			state->touches[i].x = 0;
			state->touches[i].y = 0;
		}
	}
}

bool ck_pad_map_source_poll(void *user, ChiakiControllerState *state)
{
	CKPadMapSource *source = user;
	if(!source->inner.poll || !source->inner.poll(source->inner.user, state))
		return false;
	ck_pad_map_apply(&source->map, state);
	return true;
}

CKInputSource ck_pad_map_source_input(CKPadMapSource *source, CKInputSource inner, const CKPadMap *map)
{
	source->inner = inner;
	source->map = *map;
	CKInputSource input = { inner.name, source, ck_pad_map_source_poll };
	return input;
}
