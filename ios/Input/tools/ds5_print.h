// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Shared by the A5 test tools (ds5_hidraw on Linux, ckhidprobe on the iPad):
// runs each DualSense report through ck_ds5 and prints a line when the state
// changes, plus a report rate every 5 seconds.

#ifndef DS5_PRINT_H
#define DS5_PRINT_H

#include "ck_ds5.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct ds5_printer_t
{
	bool hex; // also print the bytes of every report that gets a line
	ChiakiControllerState state;
	ChiakiControllerState last;
	unsigned counts[CK_DS5_REPORT_BT_FULL + 1];
	unsigned reports;
	double window_start;
} DS5Printer;

void ds5_printer_init(DS5Printer *printer, bool hex, double now_s);

/** "usb", "bt-simple", "bt-full", "bad-crc" or "unknown". */
const char *ds5_kind_name(CKDS5ReportKind kind);

/** One line: the report kind (any label), then buttons, sticks, triggers and touches. */
void ds5_print_state(const char *label, const ChiakiControllerState *state);

/** A button, trigger or touch changed, or a stick moved more than its jitter at rest. */
bool ds5_state_changed(const ChiakiControllerState *a, const ChiakiControllerState *b);

/** Parses one report (starting at its report ID) and prints what changed. */
CKDS5ReportKind ds5_printer_feed(DS5Printer *printer, const uint8_t *report, size_t len, double now_s);

#endif // DS5_PRINT_H
