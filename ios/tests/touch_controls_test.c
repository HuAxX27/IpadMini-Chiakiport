// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Tests for the on-screen controls (A5.5): the default layout, hit testing,
// what fingers on buttons, the d-pad, the sticks and the touchpad press, and
// the hand-over to the input thread, with a thread polling while fingers come
// and go. Run with ./ios/tests/input_tests.sh.

#include "ck_touch_controls.h"

#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
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

#define ALL_BUTTONS ((1u << CHIAKI_CONTROLLER_BUTTONS_COUNT) - 1u)
#define DPAD_BUTTONS (CHIAKI_CONTROLLER_BUTTON_DPAD_UP | CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN \
	| CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT | CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT)

static CKTouchLayout layout;
static CKTouchControls controls;

// The control of a kind, or the button that presses exactly these bits.
static int find(CKTouchKind kind, uint32_t buttons)
{
	for(unsigned i = 0; i < layout.count; i++)
	{
		const CKTouchControl *c = &layout.controls[i];
		if(c->kind == kind && (kind != CK_TOUCH_BUTTON || c->buttons == buttons))
			return (int)i;
	}
	return -1;
}

static float cx(int i)
{
	return layout.controls[i].x + layout.controls[i].w / 2.0f;
}

static float cy(int i)
{
	return layout.controls[i].y + layout.controls[i].h / 2.0f;
}

static bool overlap(const CKTouchControl *a, const CKTouchControl *b)
{
	return a->x < b->x + b->w && b->x < a->x + a->w && a->y < b->y + b->h && b->y < a->y + a->h;
}

static ChiakiControllerState polled(bool *active)
{
	ChiakiControllerState s;
	chiaki_controller_state_set_idle(&s);
	*active = ck_touch_controls_poll(&controls, &s);
	return s;
}

static void check_layout(float width, float height)
{
	ck_touch_layout_default(&layout, width, height);
	CHECK_EQ(layout.width, width);
	CHECK_EQ(layout.height, height);
	CHECK(layout.count > 0 && layout.count <= CK_TOUCH_CONTROLS_MAX);
	float s = fminf(width / CK_TOUCH_LAYOUT_WIDTH, height / CK_TOUCH_LAYOUT_HEIGHT);
	uint32_t reach = 0;
	for(unsigned i = 0; i < layout.count; i++)
	{
		const CKTouchControl *c = &layout.controls[i];
		CHECK(c->label != NULL);
		// On screen, clear of the bottom strip where iOS opens Control Center.
		CHECK(c->x >= 0.0f && c->y >= 0.0f);
		CHECK(c->x + c->w <= width && c->y + c->h <= height - 40.0f * s);
		// Big enough for a finger at full size: Apple asks for 44 points.
		if(s >= 1.0f)
			CHECK(c->w >= 44.0f && c->h >= 44.0f);
		for(unsigned j = 0; j < i; j++)
		{
			if(overlap(c, &layout.controls[j]))
			{
				fprintf(stderr, "%gx%g: controls %u (%s) and %u (%s) overlap\n", width, height, i, c->label, j,
					layout.controls[j].label);
				failures++;
			}
		}
		// Its centre hits it.
		CHECK_EQ(ck_touch_layout_hit(&layout, c->x + c->w / 2.0f, c->y + c->h / 2.0f), (int)i);
		if(c->kind == CK_TOUCH_BUTTON)
			reach |= c->buttons;
		else if(c->kind == CK_TOUCH_DPAD)
			reach |= DPAD_BUTTONS;
		else if(c->kind == CK_TOUCH_TOUCHPAD)
			reach |= CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;
	}
	// Every button of the DualSense and both full triggers are on screen.
	CHECK_EQ(reach, ALL_BUTTONS | CHIAKI_CONTROLLER_ANALOG_BUTTON_L2 | CHIAKI_CONTROLLER_ANALOG_BUTTON_R2);
	CHECK(find(CK_TOUCH_DPAD, 0) >= 0);
	CHECK(find(CK_TOUCH_STICK_LEFT, 0) >= 0);
	CHECK(find(CK_TOUCH_STICK_RIGHT, 0) >= 0);
	CHECK(find(CK_TOUCH_TOUCHPAD, 0) >= 0);
	// The middle of the screen is free, for the picture and the app's own taps.
	CHECK_EQ(ck_touch_layout_hit(&layout, width / 2.0f, height / 2.0f), -1);
	// Outside the screen, nothing.
	CHECK_EQ(ck_touch_layout_hit(&layout, -1.0f, -1.0f), -1);
	CHECK_EQ(ck_touch_layout_hit(&layout, width + 1.0f, height / 2.0f), -1);
}

static void test_layout(void)
{
	check_layout(1024.0f, 768.0f); // the iPad mini, sideways
	check_layout(768.0f, 1024.0f); // and upright
	check_layout(2048.0f, 1536.0f);
	check_layout(480.0f, 320.0f);

	// A round button's corner is outside it.
	ck_touch_layout_default(&layout, 1024.0f, 768.0f);
	int cross = find(CK_TOUCH_BUTTON, CHIAKI_CONTROLLER_BUTTON_CROSS);
	CHECK(cross >= 0);
	if(cross < 0)
		return;
	CHECK(layout.controls[cross].round);
	const CKTouchControl *c = &layout.controls[cross];
	CHECK_EQ(ck_touch_layout_hit(&layout, c->x + 2.0f, c->y + 2.0f), -1);
	CHECK_EQ(ck_touch_layout_hit(&layout, c->x + c->w / 2.0f, c->y + 2.0f), cross);
}

static void test_buttons(void)
{
	ck_touch_layout_default(&layout, 1024.0f, 768.0f);
	CHECK(ck_touch_controls_init(&controls, &layout));
	bool active;
	(void)polled(&active);
	CHECK(!active);

	// A tap outside every control is not the controls'.
	CHECK(!ck_touch_controls_began(&controls, 1, 512.0f, 384.0f));
	(void)polled(&active);
	CHECK(!active);
	CHECK_EQ(ck_touch_controls_pressed(&controls), 0);

	int cross = find(CK_TOUCH_BUTTON, CHIAKI_CONTROLLER_BUTTON_CROSS);
	int square = find(CK_TOUCH_BUTTON, CHIAKI_CONTROLLER_BUTTON_BOX);
	int ps = find(CK_TOUCH_BUTTON, CHIAKI_CONTROLLER_BUTTON_PS);
	int l2 = find(CK_TOUCH_BUTTON, CHIAKI_CONTROLLER_ANALOG_BUTTON_L2);
	CHECK(cross >= 0 && square >= 0 && ps >= 0 && l2 >= 0);
	if(cross < 0 || square < 0 || ps < 0 || l2 < 0)
	{
		ck_touch_controls_fini(&controls);
		return;
	}

	CHECK(ck_touch_controls_began(&controls, 1, cx(cross), cy(cross)));
	ChiakiControllerState s = polled(&active);
	CHECK(active);
	CHECK_EQ(s.buttons, CHIAKI_CONTROLLER_BUTTON_CROSS);
	CHECK_EQ(ck_touch_controls_pressed(&controls), 1u << cross);
	// The same finger twice, or key 0, is refused.
	CHECK(!ck_touch_controls_began(&controls, 1, cx(ps), cy(ps)));
	CHECK(!ck_touch_controls_began(&controls, 0, cx(ps), cy(ps)));

	// A second finger on PS and a third on L2, which is the full trigger, not a button bit.
	CHECK(ck_touch_controls_began(&controls, 2, cx(ps), cy(ps)));
	CHECK(ck_touch_controls_began(&controls, 3, cx(l2), cy(l2)));
	s = polled(&active);
	CHECK_EQ(s.buttons, CHIAKI_CONTROLLER_BUTTON_CROSS | CHIAKI_CONTROLLER_BUTTON_PS);
	CHECK_EQ(s.l2_state, 255);
	CHECK_EQ(s.r2_state, 0);
	ck_touch_controls_ended(&controls, 3);
	ck_touch_controls_ended(&controls, 2);
	s = polled(&active);
	CHECK_EQ(s.buttons, CHIAKI_CONTROLLER_BUTTON_CROSS);
	CHECK_EQ(s.l2_state, 0);

	// Sliding from cross onto square moves the press; off into nothing keeps it.
	ck_touch_controls_moved(&controls, 1, cx(square), cy(square));
	s = polled(&active);
	CHECK_EQ(s.buttons, CHIAKI_CONTROLLER_BUTTON_BOX);
	ck_touch_controls_moved(&controls, 1, 512.0f, 384.0f);
	s = polled(&active);
	CHECK_EQ(s.buttons, CHIAKI_CONTROLLER_BUTTON_BOX);
	CHECK_EQ(ck_touch_controls_pressed(&controls), 1u << square);

	// Moves and ends of fingers the controls do not know change nothing.
	ck_touch_controls_moved(&controls, 99, cx(ps), cy(ps));
	ck_touch_controls_ended(&controls, 99);
	ck_touch_controls_ended(&controls, 0);
	s = polled(&active);
	CHECK_EQ(s.buttons, CHIAKI_CONTROLLER_BUTTON_BOX);

	ck_touch_controls_ended(&controls, 1);
	(void)polled(&active);
	CHECK(!active);
	CHECK_EQ(ck_touch_controls_pressed(&controls), 0);

	// Ten fingers at most; release_all lets go of every one.
	for(uintptr_t k = 1; k <= CK_TOUCH_FINGERS_MAX; k++)
		CHECK(ck_touch_controls_began(&controls, k, cx(cross), cy(cross)));
	CHECK(!ck_touch_controls_began(&controls, CK_TOUCH_FINGERS_MAX + 1, cx(ps), cy(ps)));
	s = polled(&active);
	CHECK_EQ(s.buttons, CHIAKI_CONTROLLER_BUTTON_CROSS);
	ck_touch_controls_release_all(&controls);
	(void)polled(&active);
	CHECK(!active);

	// So does a new layout.
	CHECK(ck_touch_controls_began(&controls, 1, cx(cross), cy(cross)));
	ck_touch_controls_set_layout(&controls, &layout);
	(void)polled(&active);
	CHECK(!active);
	ck_touch_controls_fini(&controls);
}

static uint32_t dpad_at(int dpad, float angle_deg, float share)
{
	const CKTouchControl *c = &layout.controls[dpad];
	float r = c->w / 2.0f * share;
	float a = angle_deg * 3.14159265f / 180.0f;
	// Screen y grows downwards: 90 degrees is down.
	CHECK(ck_touch_controls_began(&controls, 7, cx(dpad) + r * cosf(a), cy(dpad) + r * sinf(a)));
	bool active;
	ChiakiControllerState s = polled(&active);
	CHECK(active);
	ck_touch_controls_ended(&controls, 7);
	return s.buttons;
}

static void test_dpad(void)
{
	ck_touch_layout_default(&layout, 1024.0f, 768.0f);
	CHECK(ck_touch_controls_init(&controls, &layout));
	int dpad = find(CK_TOUCH_DPAD, 0);
	CHECK(dpad >= 0);
	if(dpad < 0)
		return;
	const uint32_t U = CHIAKI_CONTROLLER_BUTTON_DPAD_UP, D = CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN;
	const uint32_t L = CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT, R = CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT;
	CHECK_EQ(dpad_at(dpad, 0, 0.8f), R);
	CHECK_EQ(dpad_at(dpad, 90, 0.8f), D);
	CHECK_EQ(dpad_at(dpad, 180, 0.8f), L);
	CHECK_EQ(dpad_at(dpad, 270, 0.8f), U);
	CHECK_EQ(dpad_at(dpad, 45, 0.8f), D | R);
	CHECK_EQ(dpad_at(dpad, 135, 0.8f), D | L);
	CHECK_EQ(dpad_at(dpad, 225, 0.8f), U | L);
	CHECK_EQ(dpad_at(dpad, 315, 0.8f), U | R);
	// 25 degrees off a direction is still only that direction.
	CHECK_EQ(dpad_at(dpad, 25, 0.8f), R);
	CHECK_EQ(dpad_at(dpad, 245, 0.8f), U);
	// The centre presses nothing, but the finger is on the d-pad.
	CHECK_EQ(dpad_at(dpad, 0, 0.1f), 0);

	// A thumb sliding round the d-pad, and out of it, keeps steering.
	CHECK(ck_touch_controls_began(&controls, 1, cx(dpad), cy(dpad) - 60.0f));
	bool active;
	ChiakiControllerState s = polled(&active);
	CHECK_EQ(s.buttons, U);
	ck_touch_controls_moved(&controls, 1, cx(dpad) + 150.0f, cy(dpad));
	s = polled(&active);
	CHECK_EQ(s.buttons, R);
	ck_touch_controls_ended(&controls, 1);
	ck_touch_controls_fini(&controls);
}

static void test_sticks(void)
{
	ck_touch_layout_default(&layout, 1024.0f, 768.0f);
	CHECK(ck_touch_controls_init(&controls, &layout));
	int left = find(CK_TOUCH_STICK_LEFT, 0), right = find(CK_TOUCH_STICK_RIGHT, 0);
	CHECK(left >= 0 && right >= 0);
	if(left < 0 || right < 0)
		return;
	float travel = layout.controls[left].w * CK_TOUCH_STICK_TRAVEL;

	// Down off centre: the stick starts where the finger lands, with no jump.
	float x0 = cx(left) + 40.0f, y0 = cy(left) - 30.0f;
	CHECK(ck_touch_controls_began(&controls, 1, x0, y0));
	bool active;
	ChiakiControllerState s = polled(&active);
	CHECK(active);
	CHECK_EQ(s.left_x, 0);
	CHECK_EQ(s.left_y, 0);
	float dx, dy;
	CHECK(ck_touch_controls_stick_offset(&controls, left, &dx, &dy));
	CHECK(dx == 0.0f && dy == 0.0f);
	CHECK(!ck_touch_controls_stick_offset(&controls, right, &dx, &dy));

	// Half the travel to the right, then all of it up: up is negative, as in Chiaki.
	ck_touch_controls_moved(&controls, 1, x0 + travel / 2.0f, y0);
	s = polled(&active);
	CHECK(s.left_x > 16000 && s.left_x < 16800);
	CHECK_EQ(s.left_y, 0);
	ck_touch_controls_moved(&controls, 1, x0, y0 - travel);
	s = polled(&active);
	CHECK_EQ(s.left_x, 0);
	CHECK_EQ(s.left_y, -32767);

	// Far past the travel on a diagonal, and out of the zone: cut to the circle.
	ck_touch_controls_moved(&controls, 1, x0 + 3.0f * travel, y0 + 3.0f * travel);
	s = polled(&active);
	CHECK(abs(s.left_x - 23170) < 20 && abs(s.left_y - 23170) < 20);
	CHECK(ck_touch_controls_stick_offset(&controls, left, &dx, &dy));
	CHECK(fabsf(sqrtf(dx * dx + dy * dy) - travel) < 0.01f);

	// A second finger on the same stick holds nothing; the right stick takes its own.
	CHECK(ck_touch_controls_began(&controls, 2, cx(left), cy(left)));
	ck_touch_controls_moved(&controls, 2, cx(left) - travel, cy(left));
	CHECK(ck_touch_controls_began(&controls, 3, cx(right), cy(right)));
	ck_touch_controls_moved(&controls, 3, cx(right) - travel, cy(right));
	s = polled(&active);
	CHECK(s.left_x > 23000);
	CHECK_EQ(s.right_x, -32767);
	CHECK_EQ(s.right_y, 0);
	CHECK_EQ(ck_touch_controls_pressed(&controls), (1u << left) | (1u << right));

	// The first finger lifts: the left stick is free, even with the second one still down.
	ck_touch_controls_ended(&controls, 1);
	s = polled(&active);
	CHECK_EQ(s.left_x, 0);
	CHECK_EQ(s.left_y, 0);
	CHECK_EQ(s.right_x, -32767);
	ck_touch_controls_release_all(&controls);
	ck_touch_controls_fini(&controls);
}

static void test_touchpad(void)
{
	ck_touch_layout_default(&layout, 1024.0f, 768.0f);
	CHECK(ck_touch_controls_init(&controls, &layout));
	int pad = find(CK_TOUCH_TOUCHPAD, 0);
	CHECK(pad >= 0);
	if(pad < 0)
		return;
	const CKTouchControl *c = &layout.controls[pad];

	// Top left corner: the PS5's touchpad at 0, 0, and its click.
	CHECK(ck_touch_controls_began(&controls, 1, c->x, c->y));
	bool active;
	ChiakiControllerState s = polled(&active);
	CHECK(active);
	CHECK_EQ(s.buttons, CHIAKI_CONTROLLER_BUTTON_TOUCHPAD);
	CHECK_EQ(s.touches[0].id, 0);
	CHECK_EQ(s.touches[0].x, 0);
	CHECK_EQ(s.touches[0].y, 0);
	CHECK_EQ(s.touches[1].id, -1);

	// A second finger at the bottom right corner, then the first slides out to the left: clamped.
	CHECK(ck_touch_controls_began(&controls, 2, c->x + c->w - 0.01f, c->y + c->h - 0.01f));
	ck_touch_controls_moved(&controls, 1, c->x - 100.0f, c->y + c->h / 2.0f);
	s = polled(&active);
	CHECK_EQ(s.touches[0].id, 0);
	CHECK_EQ(s.touches[0].x, 0);
	CHECK(abs(s.touches[0].y - 471) <= 1);
	CHECK_EQ(s.touches[1].id, 1);
	CHECK(s.touches[1].x >= CK_TOUCH_TOUCHPAD_WIDTH - 2 && s.touches[1].x <= CK_TOUCH_TOUCHPAD_WIDTH - 1);
	CHECK(s.touches[1].y >= CK_TOUCH_TOUCHPAD_HEIGHT - 2 && s.touches[1].y <= CK_TOUCH_TOUCHPAD_HEIGHT - 1);

	// A third finger holds nothing: the PS5's touchpad takes two.
	CHECK(ck_touch_controls_began(&controls, 3, cx(pad), cy(pad)));
	s = polled(&active);
	CHECK_EQ(s.touches[0].id, 0);
	CHECK_EQ(s.touches[1].id, 1);

	// The first lifts: the second keeps its slot, and a new finger gets the free one with a new ID.
	ck_touch_controls_ended(&controls, 1);
	s = polled(&active);
	CHECK_EQ(s.touches[0].id, -1);
	CHECK_EQ(s.touches[1].id, 1);
	CHECK_EQ(s.buttons, CHIAKI_CONTROLLER_BUTTON_TOUCHPAD);
	CHECK(ck_touch_controls_began(&controls, 4, cx(pad), cy(pad)));
	s = polled(&active);
	CHECK_EQ(s.touches[0].id, 2);
	CHECK(abs(s.touches[0].x - 960) <= 1);
	CHECK_EQ(s.touches[1].id, 1);
	ck_touch_controls_release_all(&controls);
	(void)polled(&active);
	CHECK(!active);

	// IDs are 7 bits, as in Chiaki, and wrap.
	for(int i = 0; i < 130; i++)
	{
		CHECK(ck_touch_controls_began(&controls, 5, cx(pad), cy(pad)));
		s = polled(&active);
		CHECK(s.touches[0].id >= 0 && s.touches[0].id <= 127);
		ck_touch_controls_ended(&controls, 5);
	}
	ck_touch_controls_fini(&controls);
}

// The touch controls and the DualSense at once, merged as CKSession does.
static void test_merge(void)
{
	ck_touch_layout_default(&layout, 1024.0f, 768.0f);
	CHECK(ck_touch_controls_init(&controls, &layout));
	int cross = find(CK_TOUCH_BUTTON, CHIAKI_CONTROLLER_BUTTON_CROSS);
	CHECK(ck_touch_controls_began(&controls, 1, cx(cross), cy(cross)));
	bool active;
	ChiakiControllerState touch = polled(&active);
	ChiakiControllerState pad;
	chiaki_controller_state_set_idle(&pad);
	pad.buttons = CHIAKI_CONTROLLER_BUTTON_MOON;
	pad.left_x = -20000;
	ChiakiControllerState merged;
	chiaki_controller_state_or(&merged, &pad, &touch);
	CHECK_EQ(merged.buttons, CHIAKI_CONTROLLER_BUTTON_CROSS | CHIAKI_CONTROLLER_BUTTON_MOON);
	CHECK_EQ(merged.left_x, -20000);
	ck_touch_controls_fini(&controls);
}

static atomic_bool stop_polling;
static atomic_uint polls_active;

// Polls as CKSession's input thread does, while one finger slides back and
// forth between cross and L2 and another holds the left stick full right. A
// whole state is either cross or L2, never both or neither, and always the
// stick: anything else is half of one state and half of the next.
static void *poller(void *user)
{
	(void)user;
	while(!atomic_load(&stop_polling))
	{
		ChiakiControllerState s;
		chiaki_controller_state_set_idle(&s);
		bool active = ck_touch_controls_poll(&controls, &s);
		atomic_fetch_add(&polls_active, 1);
		bool cross = s.buttons == CHIAKI_CONTROLLER_BUTTON_CROSS && s.l2_state == 0;
		bool l2 = s.buttons == 0 && s.l2_state == 255;
		if(!active || s.left_x != 32767 || !(cross || l2))
		{
			fprintf(stderr, "touch_controls_test: polled half a state: active %d, buttons 0x%x, l2 %u, left_x %d\n",
				active, (unsigned)s.buttons, (unsigned)s.l2_state, s.left_x);
			failures++;
			break;
		}
	}
	return NULL;
}

static void test_threads(void)
{
	ck_touch_layout_default(&layout, 1024.0f, 768.0f);
	CHECK(ck_touch_controls_init(&controls, &layout));
	int cross = find(CK_TOUCH_BUTTON, CHIAKI_CONTROLLER_BUTTON_CROSS);
	int l2 = find(CK_TOUCH_BUTTON, CHIAKI_CONTROLLER_ANALOG_BUTTON_L2);
	int left = find(CK_TOUCH_STICK_LEFT, 0);
	float travel = layout.controls[left].w * CK_TOUCH_STICK_TRAVEL;
	CHECK(ck_touch_controls_began(&controls, 2, cx(left), cy(left)));
	ck_touch_controls_moved(&controls, 2, cx(left) + 2.0f * travel, cy(left));
	CHECK(ck_touch_controls_began(&controls, 1, cx(cross), cy(cross)));
	pthread_t thread;
	atomic_store(&stop_polling, false);
	CHECK(pthread_create(&thread, NULL, poller, NULL) == 0);
	for(int i = 0; i < 50000; i++)
	{
		ck_touch_controls_moved(&controls, 1, cx(l2), cy(l2));
		ck_touch_controls_moved(&controls, 1, cx(cross), cy(cross));
	}
	atomic_store(&stop_polling, true);
	pthread_join(thread, NULL);
	ck_touch_controls_fini(&controls);
}

int main(void)
{
	test_layout();
	test_buttons();
	test_dpad();
	test_sticks();
	test_touchpad();
	test_merge();
	test_threads();
	if(failures)
	{
		fprintf(stderr, "touch_controls_test: %d failure(s)\n", failures);
		return 1;
	}
	printf("touch_controls_test: ok (%u polls while a finger slid)\n", atomic_load(&polls_active));
	return 0;
}
