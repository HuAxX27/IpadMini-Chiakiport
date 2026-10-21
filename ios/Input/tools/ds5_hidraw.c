// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Linux only: reads a real DualSense through hidraw and prints what
// ck_ds5_parse_input_report() makes of it, to check the parser against the
// pad before the iPad can run anything (A5.7). Works over USB and Bluetooth.
//
//   cc -std=c11 -O2 -Ilib/include -Iios/Input -o ds5_hidraw ios/Input/tools/ds5_hidraw.c ios/Input/ck_ds5.c lib/src/controller.c
//   sudo ./ds5_hidraw            # finds the first DualSense
//   sudo ./ds5_hidraw -x /dev/hidraw3   # also prints each changed report in hex

#define _DEFAULT_SOURCE

#include "ck_ds5.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

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

static const char *kind_name(CKDS5ReportKind kind)
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

static int open_dualsense(const char *path, struct hidraw_devinfo *info)
{
	char candidate[32];
	for(int i = 0; i < 64; i++)
	{
		if(path)
			snprintf(candidate, sizeof(candidate), "%s", path);
		else
			snprintf(candidate, sizeof(candidate), "/dev/hidraw%d", i);
		int fd = open(candidate, O_RDWR);
		if(fd >= 0)
		{
			if(ioctl(fd, HIDIOCGRAWINFO, info) == 0 && (uint16_t)info->vendor == CK_DS5_VENDOR_ID
				&& ((uint16_t)info->product == CK_DS5_PRODUCT_ID || (uint16_t)info->product == CK_DS5_EDGE_PRODUCT_ID))
			{
				printf("%s: DualSense %04x:%04x over %s\n", candidate, (uint16_t)info->vendor, (uint16_t)info->product,
					info->bustype == BUS_BLUETOOTH ? "Bluetooth" : info->bustype == BUS_USB ? "USB" : "another bus");
				return fd;
			}
			close(fd);
		}
		else if(path || errno == EACCES)
			fprintf(stderr, "%s: %s\n", candidate, strerror(errno));
		if(path)
			break;
	}
	return -1;
}

static void print_state(CKDS5ReportKind kind, const ChiakiControllerState *s)
{
	printf("%-9s", kind_name(kind));
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
static int stick_moved(int16_t a, int16_t b)
{
	int d = (int)a - (int)b;
	return d > 2048 || d < -2048;
}

static int state_changed(const ChiakiControllerState *a, const ChiakiControllerState *b)
{
	if(a->buttons != b->buttons || a->l2_state != b->l2_state || a->r2_state != b->r2_state)
		return 1;
	if(stick_moved(a->left_x, b->left_x) || stick_moved(a->left_y, b->left_y)
		|| stick_moved(a->right_x, b->right_x) || stick_moved(a->right_y, b->right_y))
		return 1;
	for(int i = 0; i < CHIAKI_CONTROLLER_TOUCHES_MAX; i++)
		if(a->touches[i].id != b->touches[i].id)
			return 1;
	return 0;
}

static double now_s(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts); // Linux tool; the iOS code must not use clock_gettime
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
	int hex = 0;
	const char *path = NULL;
	for(int i = 1; i < argc; i++)
	{
		if(strcmp(argv[i], "-x") == 0)
			hex = 1;
		else
			path = argv[i];
	}

	struct hidraw_devinfo info;
	int fd = open_dualsense(path, &info);
	if(fd < 0)
	{
		fprintf(stderr, "No DualSense found. Is it connected, and are you root?\n");
		return 1;
	}

	if(info.bustype == BUS_BLUETOOTH)
	{
		// Leaves the simple report for the full one. Linux's hid-playstation already does this.
		uint8_t feature[CK_DS5_FEATURE_REPORT_CALIBRATION_SIZE] = { CK_DS5_FEATURE_REPORT_CALIBRATION };
		int r = ioctl(fd, HIDIOCGFEATURE(sizeof(feature)), feature);
		printf("feature report 0x%02x: %s\n", CK_DS5_FEATURE_REPORT_CALIBRATION, r < 0 ? strerror(errno) : "read");
	}

	printf("Press buttons, move the sticks, touch the touchpad. Ctrl-C to quit.\n");

	ChiakiControllerState state, last;
	chiaki_controller_state_set_idle(&state);
	chiaki_controller_state_set_idle(&last);
	unsigned counts[CK_DS5_REPORT_BT_FULL + 1] = { 0 };
	unsigned reports = 0;
	double window_start = now_s();
	uint8_t buf[128];

	for(;;)
	{
		ssize_t n = read(fd, buf, sizeof(buf));
		if(n < 0)
		{
			fprintf(stderr, "read: %s\n", strerror(errno));
			return 1;
		}
		CKDS5ReportKind kind = ck_ds5_parse_input_report(buf, (size_t)n, &state);
		counts[kind]++;
		reports++;

		if(kind == CK_DS5_REPORT_UNKNOWN || kind == CK_DS5_REPORT_BAD_CRC || state_changed(&state, &last))
		{
			print_state(kind, &state);
			if(hex)
			{
				printf("  %zd bytes:", n);
				for(ssize_t i = 0; i < n; i++)
					printf(" %02x", buf[i]);
				printf("\n");
			}
			last = state;
		}

		double t = now_s();
		if(t - window_start >= 5.0)
		{
			printf("-- %.0f reports/s: usb %u, bt-full %u, bt-simple %u, bad-crc %u, unknown %u\n",
				reports / (t - window_start), counts[CK_DS5_REPORT_USB], counts[CK_DS5_REPORT_BT_FULL],
				counts[CK_DS5_REPORT_BT_SIMPLE], counts[CK_DS5_REPORT_BAD_CRC], counts[CK_DS5_REPORT_UNKNOWN]);
			memset(counts, 0, sizeof(counts));
			reports = 0;
			window_start = t;
		}
	}
}
