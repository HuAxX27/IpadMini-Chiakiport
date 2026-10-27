// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "ck_touch_controls.h"

#include <math.h>
#include <string.h>

// Edges of the screen a control is kept against.
typedef enum
{
	LEFT,
	CENTER,
	RIGHT
} AnchorX;

typedef enum
{
	TOP,
	BOTTOM
} AnchorY;

// The default layout in 1024 x 768 points: x and y from the anchor, negative
// from the right and the bottom edge. The bottom 40 points stay free for
// Control Center's swipe.
typedef struct
{
	CKTouchKind kind;
	AnchorX ax;
	AnchorY ay;
	float x, y, w, h;
	bool round;
	uint32_t buttons;
	const char *label;
} Place;

#define M 24.0f
#define FACE 76.0f
#define FACE_STEP 78.0f
#define FACE_X (-140.0f)
#define FACE_Y (-405.0f)

static const Place default_places[] = {
	{ CK_TOUCH_BUTTON, LEFT, TOP, M, M, 128, 52, false, CHIAKI_CONTROLLER_ANALOG_BUTTON_L2, "L2" },
	{ CK_TOUCH_BUTTON, LEFT, TOP, M, 86, 128, 52, false, CHIAKI_CONTROLLER_BUTTON_L1, "L1" },
	{ CK_TOUCH_BUTTON, RIGHT, TOP, -M - 128, M, 128, 52, false, CHIAKI_CONTROLLER_ANALOG_BUTTON_R2, "R2" },
	{ CK_TOUCH_BUTTON, RIGHT, TOP, -M - 128, 86, 128, 52, false, CHIAKI_CONTROLLER_BUTTON_R1, "R1" },
	{ CK_TOUCH_BUTTON, CENTER, TOP, -232, M + 26, 88, 44, false, CHIAKI_CONTROLLER_BUTTON_SHARE, "Crear" },
	{ CK_TOUCH_TOUCHPAD, CENTER, TOP, -130, M, 260, 96, false, 0, "Panel táctil" },
	{ CK_TOUCH_BUTTON, CENTER, TOP, 144, M + 26, 88, 44, false, CHIAKI_CONTROLLER_BUTTON_OPTIONS, "Opciones" },
	{ CK_TOUCH_BUTTON, CENTER, TOP, -30, 134, 60, 60, true, CHIAKI_CONTROLLER_BUTTON_PS, "PS" },
	{ CK_TOUCH_DPAD, LEFT, BOTTOM, 32, -500, 190, 190, false, 0, "" },
	{ CK_TOUCH_STICK_LEFT, LEFT, BOTTOM, 150, -270, 230, 230, false, 0, "L" },
	{ CK_TOUCH_BUTTON, LEFT, BOTTOM, M, -104, 60, 60, true, CHIAKI_CONTROLLER_BUTTON_L3, "L3" },
	{ CK_TOUCH_BUTTON, RIGHT, BOTTOM, FACE_X - FACE / 2, FACE_Y - FACE_STEP - FACE / 2, FACE, FACE, true,
		CHIAKI_CONTROLLER_BUTTON_PYRAMID, "\xe2\x96\xb3" }, // △
	{ CK_TOUCH_BUTTON, RIGHT, BOTTOM, FACE_X - FACE_STEP - FACE / 2, FACE_Y - FACE / 2, FACE, FACE, true,
		CHIAKI_CONTROLLER_BUTTON_BOX, "\xe2\x96\xa1" }, // □
	{ CK_TOUCH_BUTTON, RIGHT, BOTTOM, FACE_X + FACE_STEP - FACE / 2, FACE_Y - FACE / 2, FACE, FACE, true,
		CHIAKI_CONTROLLER_BUTTON_MOON, "\xe2\x97\x8b" }, // ○
	{ CK_TOUCH_BUTTON, RIGHT, BOTTOM, FACE_X - FACE / 2, FACE_Y + FACE_STEP - FACE / 2, FACE, FACE, true,
		CHIAKI_CONTROLLER_BUTTON_CROSS, "\xe2\x9c\x95" }, // ✕
	{ CK_TOUCH_STICK_RIGHT, RIGHT, BOTTOM, -380, -270, 230, 230, false, 0, "R" },
	{ CK_TOUCH_BUTTON, RIGHT, BOTTOM, -M - 60, -104, 60, 60, true, CHIAKI_CONTROLLER_BUTTON_R3, "R3" },
};

_Static_assert(sizeof(default_places) / sizeof(default_places[0]) <= CK_TOUCH_CONTROLS_MAX, "too many controls");

void ck_touch_layout_default(CKTouchLayout *layout, float width, float height)
{
	memset(layout, 0, sizeof(*layout));
	layout->width = width;
	layout->height = height;
	float sx = width / CK_TOUCH_LAYOUT_WIDTH, sy = height / CK_TOUCH_LAYOUT_HEIGHT;
	float s = sx < sy ? sx : sy;
	for(unsigned i = 0; i < sizeof(default_places) / sizeof(default_places[0]); i++)
	{
		const Place *p = &default_places[i];
		CKTouchControl *c = &layout->controls[layout->count++];
		float edge_x = p->ax == LEFT ? 0.0f : p->ax == CENTER ? width / 2.0f : width;
		float edge_y = p->ay == TOP ? 0.0f : height;
		c->kind = p->kind;
		c->x = edge_x + p->x * s;
		c->y = edge_y + p->y * s;
		c->w = p->w * s;
		c->h = p->h * s;
		c->round = p->round;
		c->buttons = p->buttons;
		c->label = p->label;
	}
}

static bool control_contains(const CKTouchControl *c, float x, float y)
{
	if(c->round)
	{
		float rx = c->w / 2.0f, ry = c->h / 2.0f;
		if(rx <= 0.0f || ry <= 0.0f)
			return false;
		float dx = (x - (c->x + rx)) / rx, dy = (y - (c->y + ry)) / ry;
		return dx * dx + dy * dy <= 1.0f;
	}
	return x >= c->x && x < c->x + c->w && y >= c->y && y < c->y + c->h;
}

int ck_touch_layout_hit(const CKTouchLayout *layout, float x, float y)
{
	for(unsigned i = 0; i < layout->count; i++)
	{
		if(control_contains(&layout->controls[i], x, y))
			return (int)i;
	}
	return -1;
}

bool ck_touch_controls_init(CKTouchControls *controls, const CKTouchLayout *layout)
{
	memset(controls, 0, sizeof(*controls));
	if(pthread_mutex_init(&controls->mutex, NULL) != 0)
		return false;
	chiaki_controller_state_set_idle(&controls->published);
	ck_touch_controls_set_layout(controls, layout);
	return true;
}

void ck_touch_controls_fini(CKTouchControls *controls)
{
	pthread_mutex_destroy(&controls->mutex);
}

static bool is_stick(CKTouchKind kind)
{
	return kind == CK_TOUCH_STICK_LEFT || kind == CK_TOUCH_STICK_RIGHT;
}

static int16_t axis(float v)
{
	float scaled = v * 32767.0f;
	if(scaled >= 32767.0f)
		return 32767;
	if(scaled <= -32767.0f)
		return -32767;
	return (int16_t)(scaled < 0.0f ? scaled - 0.5f : scaled + 0.5f);
}

// The finger's offset on a stick, as a share of its travel, cut to the unit circle.
static void stick_vector(const CKTouchControl *c, const CKTouchFinger *f, float *vx, float *vy)
{
	float travel = (c->w < c->h ? c->w : c->h) * CK_TOUCH_STICK_TRAVEL;
	float x = (f->x - f->x0) / travel, y = (f->y - f->y0) / travel;
	float len = sqrtf(x * x + y * y);
	if(len > 1.0f)
	{
		x /= len;
		y /= len;
	}
	*vx = x;
	*vy = y;
}

// Nothing near the centre; past it, a direction is pressed when the finger is
// within 60 degrees of it, so the diagonals take 30 degrees each and the four
// directions 60: a thumb going down a menu does not slip sideways.
#define DPAD_CENTER 0.25f
#define DPAD_COS_60 0.5f

static uint32_t dpad_buttons(const CKTouchControl *c, float x, float y)
{
	float half = (c->w < c->h ? c->w : c->h) / 2.0f;
	float nx = (x - (c->x + c->w / 2.0f)) / half, ny = (y - (c->y + c->h / 2.0f)) / half;
	float len = sqrtf(nx * nx + ny * ny);
	if(len < DPAD_CENTER)
		return 0;
	nx /= len;
	ny /= len;
	uint32_t buttons = 0;
	if(nx > DPAD_COS_60)
		buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT;
	if(nx < -DPAD_COS_60)
		buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT;
	if(ny > DPAD_COS_60)
		buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN;
	if(ny < -DPAD_COS_60)
		buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_UP;
	return buttons;
}

static uint16_t touchpad_coord(float v, float origin, float size, int units)
{
	float t = size > 0.0f ? (v - origin) / size : 0.0f;
	if(t < 0.0f)
		t = 0.0f;
	if(t > 1.0f)
		t = 1.0f;
	return (uint16_t)(t * (float)(units - 1) + 0.5f);
}

// Works out the state from the fingers and hands it to the input thread.
static void update(CKTouchControls *controls)
{
	ChiakiControllerState *s = &controls->state;
	chiaki_controller_state_set_idle(s);
	uint32_t pressed = 0;
	uint32_t buttons = 0;
	for(int i = 0; i < CK_TOUCH_FINGERS_MAX; i++)
	{
		const CKTouchFinger *f = &controls->fingers[i];
		if(!f->key || f->control < 0)
			continue;
		const CKTouchControl *c = &controls->layout.controls[f->control];
		pressed |= 1u << f->control;
		float vx, vy;
		switch(c->kind)
		{
			case CK_TOUCH_BUTTON:
				buttons |= c->buttons;
				break;
			case CK_TOUCH_DPAD:
				buttons |= dpad_buttons(c, f->x, f->y);
				break;
			case CK_TOUCH_STICK_LEFT:
				stick_vector(c, f, &vx, &vy);
				s->left_x = axis(vx);
				s->left_y = axis(vy);
				break;
			case CK_TOUCH_STICK_RIGHT:
				stick_vector(c, f, &vx, &vy);
				s->right_x = axis(vx);
				s->right_y = axis(vy);
				break;
			case CK_TOUCH_TOUCHPAD:
				buttons |= CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;
				s->touches[f->slot].id = (int8_t)f->touch_id;
				s->touches[f->slot].x = touchpad_coord(f->x, c->x, c->w, CK_TOUCH_TOUCHPAD_WIDTH);
				s->touches[f->slot].y = touchpad_coord(f->y, c->y, c->h, CK_TOUCH_TOUCHPAD_HEIGHT);
				break;
		}
	}
	if(buttons & CHIAKI_CONTROLLER_ANALOG_BUTTON_L2)
		s->l2_state = 255;
	if(buttons & CHIAKI_CONTROLLER_ANALOG_BUTTON_R2)
		s->r2_state = 255;
	s->buttons = buttons & ((1u << CHIAKI_CONTROLLER_BUTTONS_COUNT) - 1u);
	controls->pressed = pressed;

	pthread_mutex_lock(&controls->mutex);
	controls->published = *s;
	controls->published_active = pressed != 0;
	pthread_mutex_unlock(&controls->mutex);
}

void ck_touch_controls_set_layout(CKTouchControls *controls, const CKTouchLayout *layout)
{
	controls->layout = *layout;
	if(controls->layout.count > CK_TOUCH_CONTROLS_MAX)
		controls->layout.count = CK_TOUCH_CONTROLS_MAX;
	memset(controls->fingers, 0, sizeof(controls->fingers));
	update(controls);
}

static CKTouchFinger *find_finger(CKTouchControls *controls, uintptr_t key)
{
	for(int i = 0; i < CK_TOUCH_FINGERS_MAX; i++)
	{
		if(controls->fingers[i].key == key)
			return &controls->fingers[i];
	}
	return NULL;
}

static bool control_held(const CKTouchControls *controls, int control)
{
	for(int i = 0; i < CK_TOUCH_FINGERS_MAX; i++)
	{
		if(controls->fingers[i].key && controls->fingers[i].control == control)
			return true;
	}
	return false;
}

static int free_touch_slot(const CKTouchControls *controls)
{
	bool used[CHIAKI_CONTROLLER_TOUCHES_MAX] = { false };
	for(int i = 0; i < CK_TOUCH_FINGERS_MAX; i++)
	{
		const CKTouchFinger *f = &controls->fingers[i];
		if(f->key && f->control >= 0 && controls->layout.controls[f->control].kind == CK_TOUCH_TOUCHPAD)
			used[f->slot] = true;
	}
	for(int i = 0; i < CHIAKI_CONTROLLER_TOUCHES_MAX; i++)
	{
		if(!used[i])
			return i;
	}
	return -1;
}

bool ck_touch_controls_began(CKTouchControls *controls, uintptr_t key, float x, float y)
{
	if(!key || find_finger(controls, key))
		return false;
	int control = ck_touch_layout_hit(&controls->layout, x, y);
	if(control < 0)
		return false;
	CKTouchFinger *f = find_finger(controls, 0);
	if(!f)
		return false;
	const CKTouchControl *c = &controls->layout.controls[control];
	// A stick follows one finger, and the touchpad takes as many as the PS5's.
	// A finger past that is kept, so it does not fall through to the view
	// below, but holds nothing. f is free, so the two checks do not count it.
	int held = control;
	int slot = 0;
	if(is_stick(c->kind) && control_held(controls, control))
		held = -1;
	else if(c->kind == CK_TOUCH_TOUCHPAD && (slot = free_touch_slot(controls)) < 0)
		held = -1;
	memset(f, 0, sizeof(*f));
	f->key = key;
	f->control = held;
	f->x0 = f->x = x;
	f->y0 = f->y = y;
	if(held >= 0 && c->kind == CK_TOUCH_TOUCHPAD)
	{
		f->slot = slot;
		f->touch_id = controls->touch_id_next;
		controls->touch_id_next = (uint8_t)((controls->touch_id_next + 1) & 0x7f);
	}
	update(controls);
	return true;
}

void ck_touch_controls_moved(CKTouchControls *controls, uintptr_t key, float x, float y)
{
	CKTouchFinger *f = key ? find_finger(controls, key) : NULL;
	if(!f)
		return;
	f->x = x;
	f->y = y;
	if(f->control >= 0 && controls->layout.controls[f->control].kind == CK_TOUCH_BUTTON)
	{
		// Sliding onto another button moves the press there; sliding off into
		// nothing keeps it, so a thumb a little off the button does not let go.
		int under = ck_touch_layout_hit(&controls->layout, x, y);
		if(under >= 0 && controls->layout.controls[under].kind == CK_TOUCH_BUTTON)
			f->control = under;
	}
	update(controls);
}

void ck_touch_controls_ended(CKTouchControls *controls, uintptr_t key)
{
	CKTouchFinger *f = key ? find_finger(controls, key) : NULL;
	if(!f)
		return;
	memset(f, 0, sizeof(*f));
	update(controls);
}

void ck_touch_controls_release_all(CKTouchControls *controls)
{
	memset(controls->fingers, 0, sizeof(controls->fingers));
	update(controls);
}

uint32_t ck_touch_controls_pressed(const CKTouchControls *controls)
{
	return controls->pressed;
}

bool ck_touch_controls_stick_offset(const CKTouchControls *controls, int i, float *dx, float *dy)
{
	if(i < 0 || (unsigned)i >= controls->layout.count)
		return false;
	const CKTouchControl *c = &controls->layout.controls[i];
	if(!is_stick(c->kind))
		return false;
	for(int k = 0; k < CK_TOUCH_FINGERS_MAX; k++)
	{
		const CKTouchFinger *f = &controls->fingers[k];
		if(f->key && f->control == i)
		{
			float vx, vy;
			stick_vector(c, f, &vx, &vy);
			float travel = (c->w < c->h ? c->w : c->h) * CK_TOUCH_STICK_TRAVEL;
			*dx = vx * travel;
			*dy = vy * travel;
			return true;
		}
	}
	return false;
}

bool ck_touch_controls_poll(void *user, ChiakiControllerState *state)
{
	CKTouchControls *controls = user;
	pthread_mutex_lock(&controls->mutex);
	bool active = controls->published_active;
	if(active)
		*state = controls->published;
	pthread_mutex_unlock(&controls->mutex);
	return active;
}

CKInputSource ck_touch_controls_input(CKTouchControls *controls)
{
	CKInputSource input = { CK_TOUCH_CONTROLS_NAME, controls, ck_touch_controls_poll };
	return input;
}
