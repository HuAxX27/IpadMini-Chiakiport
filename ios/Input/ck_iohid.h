// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// The part of IOKit's HID manager API that A5 uses on the iPad. The iOS 9.3 SDK
// exports these functions from IOKit.framework (see its IOKit.tbd) but ships no
// header for them, and Theos only brings the event-system headers. The
// declarations follow Apple's public IOHIDManager.h, IOHIDDevice.h and
// IOHIDKeys.h on macOS, where the same API has been documented since 10.5.

#ifndef CK_IOHID_H
#define CK_IOHID_H

#include <CoreFoundation/CoreFoundation.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t IOReturn; // kern_return_t
typedef uint32_t IOOptionBits;

#ifndef kIOReturnSuccess
#define kIOReturnSuccess 0
#endif
#define kIOHIDOptionsTypeNone 0x00

typedef struct __IOHIDManager *IOHIDManagerRef;
typedef struct __IOHIDDevice *IOHIDDeviceRef;

typedef enum
{
	kIOHIDReportTypeInput = 0,
	kIOHIDReportTypeOutput,
	kIOHIDReportTypeFeature,
	kIOHIDReportTypeCount
} IOHIDReportType;

typedef void (*IOHIDCallback)(void *context, IOReturn result, void *sender);
typedef void (*IOHIDDeviceCallback)(void *context, IOReturn result, void *sender, IOHIDDeviceRef device);
typedef void (*IOHIDReportCallback)(void *context, IOReturn result, void *sender, IOHIDReportType type,
	uint32_t reportID, uint8_t *report, CFIndex reportLength);

IOHIDManagerRef IOHIDManagerCreate(CFAllocatorRef allocator, IOOptionBits options);
IOReturn IOHIDManagerOpen(IOHIDManagerRef manager, IOOptionBits options);
IOReturn IOHIDManagerClose(IOHIDManagerRef manager, IOOptionBits options);
void IOHIDManagerSetDeviceMatching(IOHIDManagerRef manager, CFDictionaryRef matching);
void IOHIDManagerRegisterDeviceMatchingCallback(IOHIDManagerRef manager, IOHIDDeviceCallback callback, void *context);
void IOHIDManagerRegisterDeviceRemovalCallback(IOHIDManagerRef manager, IOHIDDeviceCallback callback, void *context);
void IOHIDManagerScheduleWithRunLoop(IOHIDManagerRef manager, CFRunLoopRef runLoop, CFStringRef runLoopMode);

CFTypeRef IOHIDDeviceGetProperty(IOHIDDeviceRef device, CFStringRef key);
IOReturn IOHIDDeviceOpen(IOHIDDeviceRef device, IOOptionBits options);
IOReturn IOHIDDeviceClose(IOHIDDeviceRef device, IOOptionBits options);
IOReturn IOHIDDeviceGetReport(IOHIDDeviceRef device, IOHIDReportType reportType, CFIndex reportID,
	uint8_t *report, CFIndex *pReportLength);
IOReturn IOHIDDeviceSetReport(IOHIDDeviceRef device, IOHIDReportType reportType, CFIndex reportID,
	const uint8_t *report, CFIndex reportLength);
void IOHIDDeviceRegisterInputReportCallback(IOHIDDeviceRef device, uint8_t *report, CFIndex reportLength,
	IOHIDReportCallback callback, void *context);
void IOHIDDeviceScheduleWithRunLoop(IOHIDDeviceRef device, CFRunLoopRef runLoop, CFStringRef runLoopMode);
void IOHIDDeviceUnscheduleFromRunLoop(IOHIDDeviceRef device, CFRunLoopRef runLoop, CFStringRef runLoopMode);

// Device property keys (IOHIDKeys.h).
#define kIOHIDTransportKey "Transport"
#define kIOHIDVendorIDKey "VendorID"
#define kIOHIDProductIDKey "ProductID"
#define kIOHIDManufacturerKey "Manufacturer"
#define kIOHIDProductKey "Product"
#define kIOHIDPrimaryUsagePageKey "PrimaryUsagePage"
#define kIOHIDPrimaryUsageKey "PrimaryUsage"
#define kIOHIDMaxInputReportSizeKey "MaxInputReportSize"
#define kIOHIDMaxFeatureReportSizeKey "MaxFeatureReportSize"

#ifdef __cplusplus
}
#endif

#endif // CK_IOHID_H
