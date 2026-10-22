// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// ckhidprobe (A5.7): lists every HID device iOS publishes and, when one is a
// DualSense, opens it with IOHIDManager and prints what ck_ds5 makes of its
// reports, the same way ds5_hidraw does on Linux. It is the test for both
// direct paths: USB through the camera adapter, and Bluetooth once ckbtfilter
// lets BTServer pair the pad. Run it as root over SSH:
//
//   ckhidprobe        # list devices, then follow the DualSense
//   ckhidprobe -x     # also print the bytes of every report that gets a line

#include "ck_iohid.h"
#include "ds5_print.h"

#include <stdio.h>
#include <string.h>

static bool hex;
static bool found_dualsense;

typedef struct pad_t
{
	IOHIDDeviceRef device;
	DS5Printer printer;
	bool id_missing_noted;
	uint8_t report[128];
	uint8_t with_id[129];
} Pad;

// One DualSense at a time is all the probe needs; no allocation per report.
static Pad pad;

// The IOReturn codes (IOReturn.h) most likely here, so a report from the
// device says what went wrong without a lookup.
static const char *ioreturn_str(IOReturn r)
{
	switch((uint32_t)r)
	{
		case 0: return "ok";
		case 0xe00002bc: return "general error";
		case 0xe00002c0: return "no such device";
		case 0xe00002c1: return "not privileged";
		case 0xe00002c2: return "bad argument";
		case 0xe00002c5: return "exclusive access, another process has it";
		case 0xe00002c7: return "unsupported";
		case 0xe00002cd: return "device not open";
		case 0xe00002d6: return "timeout";
		case 0xe00002e2: return "not permitted";
		case 0xe00002ed: return "device not responding";
		default: return "failed";
	}
}

static void cf_string(CFTypeRef value, char *out, size_t size)
{
	out[0] = '\0';
	if(value && CFGetTypeID(value) == CFStringGetTypeID())
		CFStringGetCString((CFStringRef)value, out, (CFIndex)size, kCFStringEncodingUTF8);
}

static long cf_number(CFTypeRef value)
{
	long n = -1;
	if(value && CFGetTypeID(value) == CFNumberGetTypeID())
		CFNumberGetValue((CFNumberRef)value, kCFNumberLongType, &n);
	return n;
}

static long device_number(IOHIDDeviceRef device, const char *key)
{
	CFStringRef k = CFStringCreateWithCString(kCFAllocatorDefault, key, kCFStringEncodingUTF8);
	long n = cf_number(IOHIDDeviceGetProperty(device, k));
	CFRelease(k);
	return n;
}

static void device_string(IOHIDDeviceRef device, const char *key, char *out, size_t size)
{
	CFStringRef k = CFStringCreateWithCString(kCFAllocatorDefault, key, kCFStringEncodingUTF8);
	cf_string(IOHIDDeviceGetProperty(device, k), out, size);
	CFRelease(k);
}

static void on_report(void *context, IOReturn result, void *sender, IOHIDReportType type, uint32_t report_id,
	uint8_t *report, CFIndex length)
{
	(void)context;
	(void)sender;
	(void)type;
	if(result != kIOReturnSuccess || length <= 0)
	{
		printf("report error: %s (0x%08x)\n", ioreturn_str(result), (unsigned)result);
		return;
	}
	const uint8_t *r = report;
	size_t n = (size_t)length;
	// ck_ds5 wants the report ID first, as IOHIDManager hands it over on macOS.
	// Should this HID stack leave it out (one byte short of every known size), put it back.
	bool id_left_out = n == CK_DS5_USB_INPUT_SIZE - 1 || n == CK_DS5_BT_SIMPLE_INPUT_SIZE - 1
		|| n == CK_DS5_BT_INPUT_SIZE - 1;
	if(report_id != 0 && id_left_out && n < sizeof(pad.with_id))
	{
		if(!pad.id_missing_noted)
		{
			pad.id_missing_noted = true;
			printf("note: reports come without their ID byte (ID 0x%02x, %zu bytes); adding it back\n",
				(unsigned)report_id, n);
		}
		pad.with_id[0] = (uint8_t)report_id;
		memcpy(pad.with_id + 1, report, n);
		r = pad.with_id;
		n++;
	}
	ds5_printer_feed(&pad.printer, r, n, CFAbsoluteTimeGetCurrent());
}

static void follow_dualsense(IOHIDDeviceRef device, bool bluetooth)
{
	if(pad.device)
	{
		printf("  another DualSense is already followed; ignoring this one\n");
		return;
	}
	IOReturn r = IOHIDDeviceOpen(device, kIOHIDOptionsTypeNone);
	printf("  open: %s (0x%08x)\n", ioreturn_str(r), (unsigned)r);
	if(r != kIOReturnSuccess)
		return;
	found_dualsense = true;
	pad.device = device;
	ds5_printer_init(&pad.printer, hex, CFAbsoluteTimeGetCurrent());

	if(bluetooth)
	{
		// Leaves the simple Bluetooth report for the full one, touchpad included.
		uint8_t feature[64] = { CK_DS5_FEATURE_REPORT_CALIBRATION };
		CFIndex len = CK_DS5_FEATURE_REPORT_CALIBRATION_SIZE;
		r = IOHIDDeviceGetReport(device, kIOHIDReportTypeFeature, CK_DS5_FEATURE_REPORT_CALIBRATION, feature, &len);
		printf("  feature report 0x%02x: %s (0x%08x), %ld bytes\n", CK_DS5_FEATURE_REPORT_CALIBRATION,
			ioreturn_str(r), (unsigned)r, (long)len);
	}

	IOHIDDeviceRegisterInputReportCallback(device, pad.report, sizeof(pad.report), on_report, NULL);
	IOHIDDeviceScheduleWithRunLoop(device, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
	printf("Press buttons, move the sticks, touch the touchpad. Ctrl-C to quit.\n");
}

static void on_device(void *context, IOReturn result, void *sender, IOHIDDeviceRef device)
{
	(void)context;
	(void)result;
	(void)sender;
	char product[128], manufacturer[128], transport[64];
	device_string(device, kIOHIDProductKey, product, sizeof(product));
	device_string(device, kIOHIDManufacturerKey, manufacturer, sizeof(manufacturer));
	device_string(device, kIOHIDTransportKey, transport, sizeof(transport));
	long vendor = device_number(device, kIOHIDVendorIDKey);
	long product_id = device_number(device, kIOHIDProductIDKey);
	printf("device: \"%s\" by \"%s\", %04lx:%04lx, transport \"%s\", usage %ld:%ld, input report up to %ld bytes\n",
		product, manufacturer, vendor & 0xffff, product_id & 0xffff, transport,
		device_number(device, kIOHIDPrimaryUsagePageKey), device_number(device, kIOHIDPrimaryUsageKey),
		device_number(device, kIOHIDMaxInputReportSizeKey));
	fflush(stdout);

	if(vendor == CK_DS5_VENDOR_ID && (product_id == CK_DS5_PRODUCT_ID || product_id == CK_DS5_EDGE_PRODUCT_ID))
		follow_dualsense(device, strstr(transport, "Bluetooth") != NULL);
}

static void on_removed(void *context, IOReturn result, void *sender, IOHIDDeviceRef device)
{
	(void)context;
	(void)result;
	(void)sender;
	if(device == pad.device)
	{
		printf("DualSense removed\n");
		memset(&pad, 0, sizeof(pad));
	}
	fflush(stdout);
}

static void waiting_hint(CFRunLoopTimerRef timer, void *info)
{
	(void)timer;
	(void)info;
	if(!found_dualsense)
	{
		printf("No DualSense yet. Connect it now (USB, or Bluetooth once paired); the probe keeps waiting.\n");
		fflush(stdout);
	}
}

int main(int argc, char **argv)
{
	for(int i = 1; i < argc; i++)
		if(strcmp(argv[i], "-x") == 0)
			hex = true;

	IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
	if(!manager)
	{
		fprintf(stderr, "IOHIDManagerCreate failed\n");
		return 1;
	}
	IOHIDManagerSetDeviceMatching(manager, NULL); // every HID device
	IOHIDManagerRegisterDeviceMatchingCallback(manager, on_device, NULL);
	IOHIDManagerRegisterDeviceRemovalCallback(manager, on_removed, NULL);
	IOHIDManagerScheduleWithRunLoop(manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
	IOReturn r = IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone);
	printf("IOHIDManagerOpen: %s (0x%08x)\n", ioreturn_str(r), (unsigned)r);
	fflush(stdout);

	CFRunLoopTimerRef timer = CFRunLoopTimerCreate(kCFAllocatorDefault, CFAbsoluteTimeGetCurrent() + 3.0, 0, 0, 0,
		waiting_hint, NULL);
	CFRunLoopAddTimer(CFRunLoopGetCurrent(), timer, kCFRunLoopDefaultMode);
	CFRunLoopRun();
	return 0;
}
