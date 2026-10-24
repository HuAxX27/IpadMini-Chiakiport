// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A6.4: what the settings screen offers and what the settings file may hold,
// on top of the C5 model (CKSettings). Plain C, tested on Linux
// (ios/tests/ui/settings.c). CKSettingsStore.m reads and writes the file, an
// XML plist next to the saved registrations:
//
//   <dict>
//     <key>bitrate_kbps</key>  <integer>, 0 = the core's for the resolution
//     <key>fps</key>           <integer>, 30 or 60
//     <key>resolution</key>    <integer>, the height: 360, 540 or 720
//   </dict>
//
// A value that is missing or not offered falls back to C5's default, so a
// damaged or hand-edited file never stops the app from opening.

#ifndef CK_UI_SETTINGS_H
#define CK_UI_SETTINGS_H

#include "../Bridge/CKSettings.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Where the app keeps its settings on the iPad. */
#define CK_UI_SETTINGS_PATH "/var/mobile/Library/Chiakiport/settings.plist"

#define CK_UI_RESOLUTION_COUNT 3
#define CK_UI_FPS_COUNT 2
#define CK_UI_BITRATE_COUNT 7

/** A bitrate set by hand in the file is kept when it falls in this range, in kbps. */
#define CK_UI_BITRATE_MIN_KBPS 1000
#define CK_UI_BITRATE_MAX_KBPS 30000

/** What the screen offers, in the order it shows them. */
extern const ChiakiVideoResolutionPreset ck_ui_resolutions[CK_UI_RESOLUTION_COUNT];
extern const ChiakiVideoFPSPreset ck_ui_fps[CK_UI_FPS_COUNT];
/** 0 first: the core's bitrate for the resolution. */
extern const uint32_t ck_ui_bitrates_kbps[CK_UI_BITRATE_COUNT];

/** 360, 540 or 720; 0 for a preset the app does not offer (1080p is beyond the A5). */
unsigned ck_ui_resolution_height(ChiakiVideoResolutionPreset resolution);

/** What the core asks the console for when bitrate_kbps is 0: 2000, 6000 or 10000 kbps. */
uint32_t ck_ui_auto_bitrate_kbps(ChiakiVideoResolutionPreset resolution);

/** The position of each value in the arrays above, or -1 when it is not there. */
int ck_ui_resolution_index(ChiakiVideoResolutionPreset resolution);
int ck_ui_fps_index(ChiakiVideoFPSPreset fps);
int ck_ui_bitrate_index(uint32_t bitrate_kbps);

/** "360p", "540p", "720p"; "30", "60"; "Auto", "2" ... "15" (Mbps). Never NULL. */
const char *ck_ui_resolution_label(ChiakiVideoResolutionPreset resolution);
const char *ck_ui_fps_label(ChiakiVideoFPSPreset fps);
const char *ck_ui_bitrate_label(uint32_t bitrate_kbps);

/**
 * The settings from the file's three numbers. -1 stands for a value that is
 * missing or not a number. Each value that is not valid takes C5's default
 * on its own. Returns true when all three were valid.
 */
bool ck_ui_settings_from_values(int64_t height, int64_t fps, int64_t bitrate_kbps, CKSettings *settings);

/** The three numbers the file stores. *settings is assumed valid (ck_ui_settings_valid()). */
void ck_ui_settings_to_values(const CKSettings *settings, int64_t *height, int64_t *fps, int64_t *bitrate_kbps);

/** Whether the app offers these settings, or accepts them from the file. */
bool ck_ui_settings_valid(const CKSettings *settings);

/**
 * One line for the console list, e.g. "540p · 30 fps · 6 Mbps (automático)".
 * Always NUL-terminated; cut when out is short.
 */
void ck_ui_settings_summary(const CKSettings *settings, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif // CK_UI_SETTINGS_H
