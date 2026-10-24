// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// ckhidprobe (A5.7, A5.8): lists every HID device iOS publishes and, when one
// is a DualSense, opens it with IOHIDManager and prints what ck_ds5 makes of
// its reports, the same way ds5_hidraw does on Linux. It is the test for both
// direct paths: USB through the camera adapter, and Bluetooth once ckbtfilter
// lets BTServer pair the pad. Run it as root over SSH:
//
//   ckhidprobe        # list devices, then follow the DualSense
//   ckhidprobe -x     # also print the bytes of every report that gets a line
//   ckhidprobe -r     # first list the HID and Bluetooth services in the IORegistry
//   ckhidprobe -s     # read the reports ckbtfilter shares instead (see ck_ds5_shared.h)
//   ckhidprobe -c     # read the pad through CKDS5Source, as the app does (A5.8)
//   ckhidprobe -c -m  # the same through the default pad map (A5.4): dead zones applied

#include "ck_ds5_shared.h"
#include "ck_ds5_source.h"
#include "ck_iohid.h"
#include "ck_pad_map.h"
#include "ds5_print.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

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

static void entry_string(io_registry_entry_t entry, const char *key, char *out, size_t size)
{
	CFStringRef k = CFStringCreateWithCString(kCFAllocatorDefault, key, kCFStringEncodingUTF8);
	CFTypeRef value = IORegistryEntryCreateCFProperty(entry, k, kCFAllocatorDefault, 0);
	CFRelease(k);
	cf_string(value, out, size);
	if(value)
		CFRelease(value);
}

static long entry_number(io_registry_entry_t entry, const char *key)
{
	CFStringRef k = CFStringCreateWithCString(kCFAllocatorDefault, key, kCFStringEncodingUTF8);
	CFTypeRef value = IORegistryEntryCreateCFProperty(entry, k, kCFAllocatorDefault, 0);
	CFRelease(k);
	long n = cf_number(value);
	if(value)
		CFRelease(value);
	return n;
}

// Walks the whole IORegistry and prints every service whose class mentions HID
// or Bluetooth, or whose product looks like a controller. It shows whether
// BTServer published the pad in some form that IOHIDManager does not match.
static void list_registry(void)
{
	io_iterator_t it;
	kern_return_t r = IORegistryCreateIterator(kIOMasterPortDefault, kIOServicePlane, kIORegistryIterateRecursively, &it);
	if(r != KERN_SUCCESS)
	{
		printf("IORegistryCreateIterator failed: 0x%08x\n", (unsigned)r);
		return;
	}
	unsigned seen = 0, shown = 0;
	io_object_t entry;
	while((entry = IOIteratorNext(it)) != 0)
	{
		seen++;
		io_name_t cls = "", name = "";
		IOObjectGetClass(entry, cls);
		IORegistryEntryGetName(entry, name);
		char product[128];
		entry_string(entry, kIOHIDProductKey, product, sizeof(product));
		bool interesting = strstr(cls, "HID") || strstr(cls, "Bluetooth") || strstr(cls, "BT")
			|| strstr(product, "Controller") || strstr(product, "DualSense");
		if(interesting)
		{
			shown++;
			char transport[64];
			entry_string(entry, kIOHIDTransportKey, transport, sizeof(transport));
			printf("registry: %s \"%s\"%s", cls, name, IOObjectConformsTo(entry, "IOHIDDevice") ? " [IOHIDDevice]" : "");
			if(product[0])
				printf(", product \"%s\"", product);
			if(transport[0])
				printf(", transport \"%s\"", transport);
			long vendor = entry_number(entry, kIOHIDVendorIDKey);
			if(vendor >= 0)
				printf(", %04lx:%04lx", vendor & 0xffff, entry_number(entry, kIOHIDProductIDKey) & 0xffff);
			long page = entry_number(entry, kIOHIDPrimaryUsagePageKey);
			if(page >= 0)
				printf(", usage %ld:%ld", page, entry_number(entry, kIOHIDPrimaryUsageKey));
			printf("\n");
		}
		IOObjectRelease(entry);
	}
	IOObjectRelease(it);
	printf("registry: %u of %u services shown\n", shown, seen);
	fflush(stdout);
}

// Follows the reports the BTServer tweak copies into CK_DS5_SHARED_PATH,
// polling every 4 ms: the path the app takes if iOS never publishes the pad.
static int follow_shared(void)
{
	int fd = open(CK_DS5_SHARED_PATH, O_RDONLY);
	struct stat st;
	if(fd < 0 || fstat(fd, &st) != 0 || st.st_size < (off_t)sizeof(CKDS5Shared))
	{
		printf("%s: %s. ckbtfilter makes it when a pad connects.\n", CK_DS5_SHARED_PATH,
			fd < 0 ? strerror(errno) : "too short");
		return 1;
	}
	void *map = mmap(NULL, sizeof(CKDS5Shared), PROT_READ, MAP_SHARED, fd, 0);
	close(fd);
	if(map == MAP_FAILED)
	{
		printf("mmap %s: %s\n", CK_DS5_SHARED_PATH, strerror(errno));
		return 1;
	}
	const CKDS5Shared *shared = map;
	if(!ck_ds5_shared_valid(shared))
		printf("%s is not set up yet; waiting for ckbtfilter.\n", CK_DS5_SHARED_PATH);
	printf("Following the reports ckbtfilter shares in %s. Press buttons, move the sticks; Ctrl-C to quit.\n",
		CK_DS5_SHARED_PATH);
	fflush(stdout);

	DS5Printer printer;
	ds5_printer_init(&printer, hex, CFAbsoluteTimeGetCurrent());
	uint32_t seq = 0;
	uint8_t report[CK_DS5_SHARED_REPORT_MAX];
	size_t len;
	int pad = -1; // unknown, no, yes
	// Every 5 s: how many reports ckbtfilter wrote (the sequence grows by 2
	// per report) against how many this loop read, to tell a pad that sends
	// little from a reader that misses reports.
	uint32_t window_seq = 0;
	unsigned window_reads = 0;
	double window_start = CFAbsoluteTimeGetCurrent();
	for(;;)
	{
		double now = CFAbsoluteTimeGetCurrent();
		if(now - window_start >= 5.0)
		{
			if(window_seq)
				printf("-- shared: %u reports written by ckbtfilter, %u read here\n", (unsigned)(seq - window_seq) / 2,
					window_reads);
			window_seq = seq;
			window_reads = 0;
			window_start = now;
		}
		if(ck_ds5_shared_read(shared, &seq, report, &len))
		{
			window_reads++;
			if(!window_seq)
				window_seq = seq;
			if(len == 0 && pad != 0)
			{
				printf("no pad connected\n");
				pad = 0;
			}
			else if(len > 0)
			{
				if(pad != 1)
					printf("pad reports arriving\n");
				pad = 1;
				ds5_printer_feed(&printer, report, len, CFAbsoluteTimeGetCurrent());
			}
			fflush(stdout);
		}
		usleep(4000);
	}
}

static uint64_t now_us(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (uint64_t)tv.tv_sec * 1000000u + (uint64_t)tv.tv_usec;
}

// -c: the input source the app hands CKSession (contract C4), polled every
// CK_INPUT_POLL_INTERVAL_MS like CKSession does. Prints the state when it
// changes and, every 5 s, what the polls cost. With -m, through the default
// pad map, as the app hands it over.
static int follow_source(bool mapped)
{
	static CKDS5Source source;
	static CKPadMapSource map_source;
	ck_ds5_source_init(&source, CK_DS5_SHARED_PATH);
	CKInputSource input = ck_ds5_source_input(&source);
	if(mapped)
	{
		CKPadMap map;
		ck_pad_map_set_default(&map);
		input = ck_pad_map_source_input(&map_source, input, &map);
		printf("Default pad map: stick dead zone %d of 32767, trigger dead zone %d of 255.\n",
			map.left_deadzone, map.l2_deadzone);
	}
	printf("Polling the \"%s\" input source every %d ms, like the app. Press buttons, move the sticks, touch the "
		"touchpad; Ctrl-C to quit.\n", input.name, CK_INPUT_POLL_INTERVAL_MS);
	fflush(stdout);

	ChiakiControllerState state, last;
	chiaki_controller_state_set_idle(&last);
	bool had_pad = false;
	uint32_t maps = 0;
	uint32_t kinds[CK_DS5_REPORT_BT_FULL + 1] = { 0 };
	unsigned polls = 0, polls_with_pad = 0;
	uint64_t slowest = 0, window_start = now_us();
	struct timespec interval = { 0, CK_INPUT_POLL_INTERVAL_MS * 1000000L };
	for(;;)
	{
		uint64_t t = now_us();
		bool has_pad = input.poll(input.user, &state);
		uint64_t took = now_us() - t;
		if(took > slowest)
			slowest = took;
		polls++;
		if(source.maps != maps)
		{
			printf("mapped %s%s\n", CK_DS5_SHARED_PATH, maps ? " again: BTServer made a new one" : "");
			maps = source.maps;
		}
		if(has_pad != had_pad)
		{
			printf(has_pad ? "pad connected\n" : "no pad\n");
			had_pad = has_pad;
		}
		if(has_pad)
		{
			polls_with_pad++;
			if(ds5_state_changed(&state, &last))
			{
				ds5_print_state(ds5_kind_name(source.kind), &state);
				last = state;
			}
		}
		if(t - window_start >= 5000000u)
		{
			if(!source.shared)
				printf("-- no %s yet: it appears when BTServer starts with ckbtfilter.\n", CK_DS5_SHARED_PATH);
			else
				printf("-- 5 s: %u polls, pad in %u; new reports: bt-full %u, bt-simple %u, bad-crc %u, unknown %u; "
					"slowest poll %llu us\n", polls, polls_with_pad,
					source.kinds[CK_DS5_REPORT_BT_FULL] - kinds[CK_DS5_REPORT_BT_FULL],
					source.kinds[CK_DS5_REPORT_BT_SIMPLE] - kinds[CK_DS5_REPORT_BT_SIMPLE],
					source.kinds[CK_DS5_REPORT_BAD_CRC] - kinds[CK_DS5_REPORT_BAD_CRC],
					source.kinds[CK_DS5_REPORT_UNKNOWN] - kinds[CK_DS5_REPORT_UNKNOWN], (unsigned long long)slowest);
			memcpy(kinds, source.kinds, sizeof(kinds));
			polls = polls_with_pad = 0;
			slowest = 0;
			window_start = t;
		}
		fflush(stdout);
		nanosleep(&interval, NULL);
	}
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
	bool registry = false, from_shared = false, from_source = false, mapped = false;
	for(int i = 1; i < argc; i++)
	{
		if(strcmp(argv[i], "-x") == 0)
			hex = true;
		else if(strcmp(argv[i], "-r") == 0)
			registry = true;
		else if(strcmp(argv[i], "-s") == 0)
			from_shared = true;
		else if(strcmp(argv[i], "-c") == 0)
			from_source = true;
		else if(strcmp(argv[i], "-m") == 0)
			mapped = true;
	}
	if(registry)
		list_registry();
	if(from_source)
		return follow_source(mapped);
	if(from_shared)
		return follow_shared();

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
