// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Linux only: reads a real DualSense through hidraw and prints what
// ck_ds5_parse_input_report() makes of it, to check the parser against the
// pad before the iPad can run anything (A5.7). Works over USB and Bluetooth.
//
//   cc -std=c11 -O2 -Ilib/include -Iios/Input -o ds5_hidraw ios/Input/tools/ds5_hidraw.c ios/Input/tools/ds5_print.c ios/Input/ck_ds5.c lib/src/controller.c
//   sudo ./ds5_hidraw            # finds the first DualSense
//   sudo ./ds5_hidraw -x /dev/hidraw3   # also prints each changed report in hex

#define _DEFAULT_SOURCE

#include "ds5_print.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

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

static double now_s(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts); // Linux tool; the iOS code must not use clock_gettime
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
	bool hex = false;
	const char *path = NULL;
	for(int i = 1; i < argc; i++)
	{
		if(strcmp(argv[i], "-x") == 0)
			hex = true;
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

	DS5Printer printer;
	ds5_printer_init(&printer, hex, now_s());
	uint8_t buf[128];
	for(;;)
	{
		ssize_t n = read(fd, buf, sizeof(buf));
		if(n < 0)
		{
			fprintf(stderr, "read: %s\n", strerror(errno));
			return 1;
		}
		ds5_printer_feed(&printer, buf, (size_t)n, now_s());
	}
}
