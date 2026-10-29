// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// How a physical pad's state reaches the console (A5.4): which button presses
// which, the dead zones of the sticks and the triggers, and whether the
// touchpad's fingers go through. It works on a ChiakiControllerState, so the
// same map serves the DualSense connected straight to the iPad (CKDS5Source)
// and the ESP32 bridge (A5.3), which leaves dead zones to the receiver.
//
// The PS button and the touchpad click are ordinary entries of the table: by
// default they go to the console as they are, so the PS button opens the
// PS5's control centre and holding it goes home, as on the console.
//
// Plain C with no I/O and no allocation, tested on Linux.

#ifndef CK_PAD_MAP_H
#define CK_PAD_MAP_H

#include "CKInputSource.h"

#include <chiaki/controller.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Radial dead zone of each stick by default, in ChiakiControllerState units
 * (32767 is full). At rest the DualSense's sticks read a few hundred units off
 * centre (T8 in docs/ipad/dualsense-directo.md); 1600 is about 5 %, small
 * enough that the game's own dead zone still decides how the stick feels.
 */
#define CK_PAD_MAP_STICK_DEADZONE_DEFAULT 1600

/** Triggers rest at 0 on the DualSense, so by default they have no dead zone. */
#define CK_PAD_MAP_TRIGGER_DEADZONE_DEFAULT 0

typedef struct ck_pad_map_t
{
	/**
	 * For each ChiakiControllerButton, by bit number (CROSS is 0, PS is 15):
	 * the ChiakiControllerButton bits it presses. 0 turns the button off.
	 * CHIAKI_CONTROLLER_ANALOG_BUTTON_L2 and _R2 press that trigger fully.
	 */
	uint32_t buttons[CHIAKI_CONTROLLER_BUTTONS_COUNT];
	/**
	 * Radial dead zone of the left and right stick, 0 to 32767. Inside it the
	 * stick reads 0; outside, the rest of the range is stretched back to full,
	 * so there is no jump at its edge and a full push still reads full.
	 */
	uint16_t left_deadzone, right_deadzone;
	/** Dead zone of L2 and R2, 0 to 255, stretched the same way. */
	uint8_t l2_deadzone, r2_deadzone;
	/** Pass the touchpad's fingers on. When false they are dropped; the click is buttons[]. */
	bool touchpad;
} CKPadMap;

/** Every button to itself, the default dead zones, touchpad on. */
void ck_pad_map_set_default(CKPadMap *map);

/** The bit number of a single ChiakiControllerButton, or -1 when button is not exactly one of them. */
int ck_pad_map_button_index(uint32_t button);

/**
 * Makes the button from press the buttons in to (0 turns it off). Returns
 * false, changing nothing, when from is not exactly one ChiakiControllerButton.
 */
bool ck_pad_map_set_button(CKPadMap *map, uint32_t from, uint32_t to);

/** Applies the map to *state in place. Motion is left as it is. */
void ck_pad_map_apply(const CKPadMap *map, ChiakiControllerState *state);

/**
 * Another source of contract C4 seen through a map: its poll calls the inner
 * source's poll and applies the map to what it returns.
 */
typedef struct ck_pad_map_source_t
{
	CKInputSource inner;
	CKPadMap map;
} CKPadMapSource;

/** The poll function of contract C4. Returns what the inner source returns. */
bool ck_pad_map_source_poll(void *user, ChiakiControllerState *state);

/**
 * Wraps inner with a copy of *map and returns the source as CKSession takes
 * it, under the inner source's name. *source must outlive the session's
 * polling. Change the map only while the session is stopped: the poll reads
 * it on CKSession's input thread.
 */
CKInputSource ck_pad_map_source_input(CKPadMapSource *source, CKInputSource inner, const CKPadMap *map);

#ifdef __cplusplus
}
#endif

#endif // CK_PAD_MAP_H
