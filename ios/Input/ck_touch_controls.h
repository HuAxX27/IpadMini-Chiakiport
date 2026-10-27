// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// The on-screen controls (A5.5), for playing or getting around the PS5's
// menus without a physical pad. This is the part that can be tested without
// an iPad: where each control goes, which control a finger is on, and what
// the fingers on the controls press. CKTouchControlsView is the thin UIKit
// layer that draws the layout and feeds it the touches.
//
// Touches arrive on the main thread; CKSession polls on its input thread. The
// state the fingers make is handed over under a mutex held for one copy.
// Plain C with no I/O and no allocation after init, tested on Linux.

#ifndef CK_TOUCH_CONTROLS_H
#define CK_TOUCH_CONTROLS_H

#include "CKInputSource.h"

#include <chiaki/controller.h>

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CK_TOUCH_CONTROLS_NAME "touch"

#define CK_TOUCH_CONTROLS_MAX 24
/** iOS follows up to 11 fingers; more than 10 on the controls is no game. */
#define CK_TOUCH_FINGERS_MAX 10

/** The size the default layout is drawn for: the iPad mini's screen in points, held sideways. */
#define CK_TOUCH_LAYOUT_WIDTH 1024.0f
#define CK_TOUCH_LAYOUT_HEIGHT 768.0f

/** A stick reads full when the finger is this far from where it went down, as a share of the stick's zone. */
#define CK_TOUCH_STICK_TRAVEL 0.28f

/** Chiaki's touchpad, in DualShock 4 units (see ck_ds5.h). */
#define CK_TOUCH_TOUCHPAD_WIDTH 1920
#define CK_TOUCH_TOUCHPAD_HEIGHT 942

typedef enum ck_touch_kind_t
{
	/** Presses its buttons while a finger is on it. A finger can slide from one button onto another. */
	CK_TOUCH_BUTTON = 0,
	/** The d-pad: the direction from its centre to the finger, diagonals included. */
	CK_TOUCH_DPAD,
	/**
	 * A stick that starts where the finger goes down, anywhere in its zone,
	 * so it never jumps on touch; it follows the finger even out of the zone.
	 */
	CK_TOUCH_STICK_LEFT,
	CK_TOUCH_STICK_RIGHT,
	/**
	 * The touchpad: a finger on it is a finger on the PS5's touchpad, at the
	 * same place, and also its click, as when pressing the real one.
	 */
	CK_TOUCH_TOUCHPAD
} CKTouchKind;

typedef struct ck_touch_control_t
{
	CKTouchKind kind;
	/** The frame, in the view's points. */
	float x, y, w, h;
	/** Hit and drawn as the ellipse inside the frame. */
	bool round;
	/** CK_TOUCH_BUTTON: ChiakiControllerButton bits, or CHIAKI_CONTROLLER_ANALOG_BUTTON_L2/R2 for a full trigger. */
	uint32_t buttons;
	/** What the button says, UTF-8. Never NULL; empty for the d-pad, which draws arrows. */
	const char *label;
} CKTouchControl;

typedef struct ck_touch_layout_t
{
	float width, height;
	unsigned count;
	CKTouchControl controls[CK_TOUCH_CONTROLS_MAX];
} CKTouchLayout;

/**
 * The default layout for a view of width x height points, laid out for
 * 1024 x 768 and scaled to fit, each control kept against its edge: L1, L2,
 * R1 and R2 in the top corners; Create, the touchpad, Options and PS at the
 * top centre; the d-pad above the left stick, the face buttons above the
 * right stick, as on the DualSense; L3 and R3 beside the sticks. It leaves
 * the middle of the screen free, and a strip along the bottom edge, where
 * iOS opens Control Center.
 */
void ck_touch_layout_default(CKTouchLayout *layout, float width, float height);

/** The control under the point, or -1. */
int ck_touch_layout_hit(const CKTouchLayout *layout, float x, float y);

typedef struct ck_touch_finger_t
{
	/** Who the finger is (on iOS, its UITouch); 0 for a free slot. */
	uintptr_t key;
	/** The control it holds, or -1 for a finger that holds nothing (a second finger on a stick). */
	int control;
	/** Where it went down and where it is now. */
	float x0, y0, x, y;
	/** On the touchpad: its slot in ChiakiControllerState.touches and its touch ID. */
	int slot;
	uint8_t touch_id;
} CKTouchFinger;

typedef struct ck_touch_controls_t
{
	CKTouchLayout layout;
	CKTouchFinger fingers[CK_TOUCH_FINGERS_MAX];
	uint8_t touch_id_next;
	/** What the fingers make, on the main thread. */
	ChiakiControllerState state;
	/** Bit i: control i is held. */
	uint32_t pressed;

	/** The copy the input thread polls. */
	pthread_mutex_t mutex;
	ChiakiControllerState published;
	bool published_active;
} CKTouchControls;

/** Copies the layout; nothing is pressed. Returns false if the mutex could not be made. */
bool ck_touch_controls_init(CKTouchControls *controls, const CKTouchLayout *layout);
void ck_touch_controls_fini(CKTouchControls *controls);

/** A new layout, for example after the view changed size. Lets go of everything. */
void ck_touch_controls_set_layout(CKTouchControls *controls, const CKTouchLayout *layout);

/**
 * A finger went down at (x, y). Returns true when it landed on a control:
 * from then on its moves and its end belong to the controls. key must not be
 * 0 and must stay the finger's until it ends.
 */
bool ck_touch_controls_began(CKTouchControls *controls, uintptr_t key, float x, float y);
void ck_touch_controls_moved(CKTouchControls *controls, uintptr_t key, float x, float y);
/** The finger left the screen, or iOS cancelled it. */
void ck_touch_controls_ended(CKTouchControls *controls, uintptr_t key);
/** Lets go of every finger: the view was hidden or left the screen. */
void ck_touch_controls_release_all(CKTouchControls *controls);

/** Bit i: control i is held. Main thread, for drawing. */
uint32_t ck_touch_controls_pressed(const CKTouchControls *controls);

/**
 * How far the finger on stick control i has moved, in points, cut to the
 * stick's travel. False when no finger holds it. Main thread, for drawing.
 */
bool ck_touch_controls_stick_offset(const CKTouchControls *controls, int i, float *dx, float *dy);

/** The poll function of contract C4. False while no finger is on a control. */
bool ck_touch_controls_poll(void *user, ChiakiControllerState *state);

/** The source as CKSession takes it. *controls must outlive the session's polling. */
CKInputSource ck_touch_controls_input(CKTouchControls *controls);

#ifdef __cplusplus
}
#endif

#endif // CK_TOUCH_CONTROLS_H
