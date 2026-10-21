// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Turns the gamepad state Bluepad32 reports into the fields of the C4 packet.
// No Bluepad32, ESP-IDF or Chiaki includes, so it is tested on Linux: the
// firmware checks at build time that the CK_BP32_* values match Bluepad32,
// and bridge/tests checks that the CK_PAD_BUTTON_* values match Chiaki.

#ifndef CK_BRIDGE_MAP_H
#define CK_BRIDGE_MAP_H

#include "ck_pad_packet.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Bluepad32 uni_gamepad_t bits (controller/uni_gamepad.h, Bluepad32 4.2).
#define CK_BP32_DPAD_UP (1 << 0)
#define CK_BP32_DPAD_DOWN (1 << 1)
#define CK_BP32_DPAD_RIGHT (1 << 2)
#define CK_BP32_DPAD_LEFT (1 << 3)

#define CK_BP32_BUTTON_A (1 << 0) // DualSense cross (south)
#define CK_BP32_BUTTON_B (1 << 1) // circle (east)
#define CK_BP32_BUTTON_X (1 << 2) // square (west)
#define CK_BP32_BUTTON_Y (1 << 3) // triangle (north)
#define CK_BP32_BUTTON_SHOULDER_L (1 << 4)
#define CK_BP32_BUTTON_SHOULDER_R (1 << 5)
#define CK_BP32_BUTTON_TRIGGER_L (1 << 6)
#define CK_BP32_BUTTON_TRIGGER_R (1 << 7)
#define CK_BP32_BUTTON_THUMB_L (1 << 8)
#define CK_BP32_BUTTON_THUMB_R (1 << 9)

#define CK_BP32_MISC_SYSTEM (1 << 0)  // PS
#define CK_BP32_MISC_SELECT (1 << 1)  // Create
#define CK_BP32_MISC_START (1 << 2)   // Options
#define CK_BP32_MISC_CAPTURE (1 << 3) // mute; Chiaki has no bit for it, so it is not sent

#define CK_BP32_PEDAL_MAX 1023

// ChiakiControllerButton bits (lib/include/chiaki/controller.h), the bits of CKPadPacket.buttons.
#define CK_PAD_BUTTON_CROSS (1u << 0)
#define CK_PAD_BUTTON_MOON (1u << 1)
#define CK_PAD_BUTTON_BOX (1u << 2)
#define CK_PAD_BUTTON_PYRAMID (1u << 3)
#define CK_PAD_BUTTON_DPAD_LEFT (1u << 4)
#define CK_PAD_BUTTON_DPAD_RIGHT (1u << 5)
#define CK_PAD_BUTTON_DPAD_UP (1u << 6)
#define CK_PAD_BUTTON_DPAD_DOWN (1u << 7)
#define CK_PAD_BUTTON_L1 (1u << 8)
#define CK_PAD_BUTTON_R1 (1u << 9)
#define CK_PAD_BUTTON_L3 (1u << 10)
#define CK_PAD_BUTTON_R3 (1u << 11)
#define CK_PAD_BUTTON_OPTIONS (1u << 12)
#define CK_PAD_BUTTON_SHARE (1u << 13)
#define CK_PAD_BUTTON_TOUCHPAD (1u << 14)
#define CK_PAD_BUTTON_PS (1u << 15)

/** The fields of Bluepad32's uni_gamepad_t that the bridge uses. */
typedef struct ck_bridge_gamepad_t
{
	uint8_t dpad;         // CK_BP32_DPAD_*
	uint16_t buttons;     // CK_BP32_BUTTON_*
	uint8_t misc_buttons; // CK_BP32_MISC_*
	int32_t axis_x;       // -512 to 511, negative is left
	int32_t axis_y;       // negative is up
	int32_t axis_rx;
	int32_t axis_ry;
	int32_t brake;        // L2, 0 to 1023
	int32_t throttle;     // R2
	bool touchpad_click;  // reported by Bluepad32 on the virtual mouse, not on the gamepad
} CKBridgeGamepad;

/**
 * Writes flags, buttons, l2, r2 and the sticks of *packet. Leaves magic,
 * version and seq alone. Sticks go from -512..511 to the int16 range of
 * ChiakiControllerState, with the same sign (negative is left and up).
 * Dead zones are left to the receiver (A5.4).
 */
void ck_bridge_map(const CKBridgeGamepad *pad, CKPadPacket *packet);

#ifdef __cplusplus
}
#endif

#endif // CK_BRIDGE_MAP_H
