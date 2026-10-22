// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// DualSense input reports to ChiakiControllerState, for a DualSense connected
// straight to the iPad over USB or Bluetooth (A5.7). Plain C with no I/O and no
// allocation, so it runs the same on Linux tests and on the iPad: the transport
// (IOHIDManager on iOS, hidraw on Linux) hands it one whole report at a time.
//
// Report layouts follow SDL (src/joystick/hidapi/SDL_hidapi_ps5.c, zlib) and
// Bluepad32 (parser/uni_hid_parser_ds5.c, Apache 2.0). No code is copied.

#ifndef CK_DS5_H
#define CK_DS5_H

#include <chiaki/controller.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CK_DS5_VENDOR_ID 0x054c
#define CK_DS5_PRODUCT_ID 0x0ce6
#define CK_DS5_EDGE_PRODUCT_ID 0x0df2

/** First byte of every input report. 0x01 is the full report on USB and the simple one on Bluetooth. */
#define CK_DS5_REPORT_ID_INPUT 0x01
#define CK_DS5_REPORT_ID_BT_INPUT 0x31

/** Sizes including the report ID. */
#define CK_DS5_USB_INPUT_SIZE 64
#define CK_DS5_BT_SIMPLE_INPUT_SIZE 10
#define CK_DS5_BT_INPUT_SIZE 78

/**
 * Over Bluetooth the DualSense starts with the simple report (sticks, buttons
 * and triggers only). Reading this feature report once switches it to the full
 * report 0x31 for the rest of the connection.
 */
#define CK_DS5_FEATURE_REPORT_CALIBRATION 0x05
#define CK_DS5_FEATURE_REPORT_CALIBRATION_SIZE 41

/**
 * The Bluetooth output report: rumble, lights and more, 78 bytes with its
 * CRC. Sending any of them also switches the pad to the full input report.
 */
#define CK_DS5_REPORT_ID_BT_OUTPUT 0x31
#define CK_DS5_BT_OUTPUT_SIZE 78

/** The five LEDs under the touchpad, bit 0 on the left. Player 1 is the middle one, as on the PS5. */
#define CK_DS5_PLAYER_LEDS_1 0x04

/** The DualSense touchpad is 1920 x 1080; Chiaki sends touches in DualShock 4 units, 1920 x 942. */
#define CK_DS5_TOUCHPAD_HEIGHT 1080
#define CK_DS5_CHIAKI_TOUCHPAD_HEIGHT 942

typedef enum ck_ds5_report_kind_t
{
	/** Not a DualSense input report, or too short. The state is left as it was. */
	CK_DS5_REPORT_UNKNOWN = 0,
	/** A Bluetooth 0x31 report whose CRC-32 does not match. The state is left as it was. */
	CK_DS5_REPORT_BAD_CRC,
	/** USB report 0x01: everything, touchpad included. */
	CK_DS5_REPORT_USB,
	/** Bluetooth report 0x01: no touchpad. Read CK_DS5_FEATURE_REPORT_CALIBRATION to leave it. */
	CK_DS5_REPORT_BT_SIMPLE,
	/** Bluetooth report 0x31: same content as USB, CRC-32 checked. */
	CK_DS5_REPORT_BT_FULL
} CKDS5ReportKind;

/** What one output report changes. Fields left false keep what the pad had. */
typedef struct ck_ds5_output_t
{
	/** 0 to 15, one more for every report sent to the pad. */
	uint8_t seq;
	/**
	 * Ends the lightbar's blue animation after the pad connects. The pad
	 * ignores lightbar colours until this has been sent once.
	 */
	bool lightbar_fade_out;
	bool set_lightbar;
	uint8_t red, green, blue;
	bool set_player_leds;
	uint8_t player_leds;
} CKDS5Output;

/**
 * Builds a Bluetooth output report with its CRC, ready to go on the
 * interrupt channel after the 0xa2 HIDP header. The layout follows Linux
 * (drivers/hid/hid-playstation.c, GPL-2.0); no code is copied.
 */
void ck_ds5_bt_output_report(uint8_t report[CK_DS5_BT_OUTPUT_SIZE], const CKDS5Output *output);

/**
 * Parses one input report, starting at its report ID, the way hidraw and
 * IOHIDDevice deliver it. On a raw L2CAP interrupt channel drop the 0xa1 HIDP
 * header first.
 *
 * On USB, BT_SIMPLE and BT_FULL it overwrites buttons, triggers, sticks and
 * touches in *state and leaves motion untouched, so call
 * chiaki_controller_state_set_idle() once before the first report. BT_SIMPLE
 * clears the touches.
 */
CKDS5ReportKind ck_ds5_parse_input_report(const uint8_t *report, size_t len, ChiakiControllerState *state);

/** zlib-compatible CRC-32: ck_ds5_crc32(0, "123456789", 9) == 0xcbf43926. */
uint32_t ck_ds5_crc32(uint32_t crc, const uint8_t *buf, size_t len);

/** Checks the CRC-32 at the end of a Bluetooth report, which also covers the 0xa1 HIDP header. */
bool ck_ds5_bt_crc_ok(const uint8_t *report, size_t len);

#ifdef __cplusplus
}
#endif

#endif // CK_DS5_H
