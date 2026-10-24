// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "ds5_print.h"

#include <stdio.h>
#include <string.h>

static const struct { uint32_t bit; const char *name; } button_names[] = {
	{ CHIAKI_CONTROLLER_BUTTON_CROSS, "cross" },
	{ CHIAKI_CONTROLLER_BUTTON_MOON, "circle" },
	{ CHIAKI_CONTROLLER_BUTTON_BOX, "square" },
	{ CHIAKI_CONTROLLER_BUTTON_PYRAMID, "triangle" },
	{ CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT, "left" },
	{ CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT, "right" },
	{ CHIAKI_CONTROLLER_BUTTON_DPAD_UP, "up" },
	{ CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN, "down" },
	{ CHIAKI_CONTROLLER_BUTTON_L1, "L1" },
	{ CHIAKI_CONTROLLER_BUTTON_R1, "R1" },
	{ CHIAKI_CONTROLLER_BUTTON_L3, "L3" },
	{ CHIAKI_CONTROLLER_BUTTON_R3, "R3" },
	{ CHIAKI_CONTROLLER_BUTTON_OPTIONS, "options" },
	{ CHIAKI_CONTROLLER_BUTTON_SHARE, "create" },
	{ CHIAKI_CONTROLLER_BUTTON_TOUCHPAD, "touchpad" },
	{ CHIAKI_CONTROLLER_BUTTON_PS, "PS" },
};

const char *ds5_kind_name(CKDS5ReportKind kind)
{
	switch(kind)
	{
		case CK_DS5_REPORT_USB: return "usb";
		case CK_DS5_REPORT_BT_SIMPLE: return "bt-simple";
		case CK_DS5_REPORT_BT_FULL: return "bt-full";
		case CK_DS5_REPORT_BAD_CRC: return "bad-crc";
		default: return "unknown";
	}
}

void ds5_print_state(const char *label, const ChiakiControllerState *s)
{
	printf("%-9s", label);
	for(size_t i = 0; i < sizeof(button_names) / sizeof(button_names[0]); i++)
		if(s->buttons & button_names[i].bit)
			printf(" %s", button_names[i].name);
	printf(" | L %6d,%6d R %6d,%6d | L2 %3u R2 %3u", s->left_x, s->left_y, s->right_x, s->right_y,
		s->l2_state, s->r2_state);
	for(int i = 0; i < CHIAKI_CONTROLLER_TOUCHES_MAX; i++)
		if(s->touches[i].id >= 0)
			printf(" | touch %d: %u,%u", s->touches[i].id, s->touches[i].x, s->touches[i].y);
	printf("\n");
}

// Sticks jitter by a few units at rest; only a bigger move counts as a change worth a line.
static bool stick_moved(int16_t a, int16_t b)
{
	int d = (int)a - (int)b;
	return d > 2048 || d < -2048;
}

// A finger counts as moved after about a twentieth of the touchpad's width.
static bool touch_moved(uint16_t a, uint16_t b)
{
	int d = (int)a - (int)b;
	return d > 96 || d < -96;
}

bool ds5_state_changed(const ChiakiControllerState *a, const ChiakiControllerState *b)
{
	if(a->buttons != b->buttons || a->l2_state != b->l2_state || a->r2_state != b->r2_state)
		return true;
	if(stick_moved(a->left_x, b->left_x) || stick_moved(a->left_y, b->left_y)
		|| stick_moved(a->right_x, b->right_x) || stick_moved(a->right_y, b->right_y))
		return true;
	for(int i = 0; i < CHIAKI_CONTROLLER_TOUCHES_MAX; i++)
	{
		if(a->touches[i].id != b->touches[i].id)
			return true;
		if(a->touches[i].id >= 0 && (touch_moved(a->touches[i].x, b->touches[i].x)
			|| touch_moved(a->touches[i].y, b->touches[i].y)))
			return true;
	}
	return false;
}

void ds5_printer_init(DS5Printer *printer, bool hex, double now_s)
{
	memset(printer, 0, sizeof(*printer));
	printer->hex = hex;
	chiaki_controller_state_set_idle(&printer->state);
	chiaki_controller_state_set_idle(&printer->last);
	printer->window_start = now_s;
}

CKDS5ReportKind ds5_printer_feed(DS5Printer *printer, const uint8_t *report, size_t len, double now_s)
{
	CKDS5ReportKind kind = ck_ds5_parse_input_report(report, len, &printer->state);
	printer->counts[kind]++;
	printer->reports++;

	if(kind == CK_DS5_REPORT_UNKNOWN || kind == CK_DS5_REPORT_BAD_CRC || ds5_state_changed(&printer->state, &printer->last))
	{
		ds5_print_state(ds5_kind_name(kind), &printer->state);
		if(printer->hex)
		{
			printf("  %zu bytes:", len);
			for(size_t i = 0; i < len; i++)
				printf(" %02x", report[i]);
			printf("\n");
		}
		printer->last = printer->state;
	}

	double elapsed = now_s - printer->window_start;
	if(elapsed >= 5.0)
	{
		printf("-- %.0f reports/s: usb %u, bt-full %u, bt-simple %u, bad-crc %u, unknown %u\n",
			printer->reports / elapsed, printer->counts[CK_DS5_REPORT_USB], printer->counts[CK_DS5_REPORT_BT_FULL],
			printer->counts[CK_DS5_REPORT_BT_SIMPLE], printer->counts[CK_DS5_REPORT_BAD_CRC],
			printer->counts[CK_DS5_REPORT_UNKNOWN]);
		memset(printer->counts, 0, sizeof(printer->counts));
		printer->reports = 0;
		printer->window_start = now_s;
	}
	fflush(stdout);
	return kind;
}
