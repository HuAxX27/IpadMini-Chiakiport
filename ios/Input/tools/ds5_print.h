// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Shared by the A5.7 test tools (ds5_hidraw on Linux, ckhidprobe on the iPad):
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

/** Parses one report (starting at its report ID) and prints what changed. */
CKDS5ReportKind ds5_printer_feed(DS5Printer *printer, const uint8_t *report, size_t len, double now_s);

#endif // DS5_PRINT_H
