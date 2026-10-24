// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Watches the HCI byte stream between iOS 9's BTServer and the Bluetooth chip
// and makes BTServer willing to pair a DualSense (A5.7). BTServer only pairs
// the classes of device it supports, so in inquiry results and incoming
// connection requests a peripheral that calls itself a gamepad or joystick is
// rewritten as a keyboard. The pad keeps its own HID descriptor, so once
// paired it still shows up as a gamepad.
//
// Plain C with no I/O and no allocation, tested on Linux. The BTServer tweak
// (ios/Input/ckdirect/btfilter) feeds it every buffer it sees on the HCI socket.
// The method comes from MFiWrapper's BTServerFilter (Jason Fetters, 2014,
// GPLv3 or later, github.com/meancoot/MFiWrapper); this is a new implementation
// that also follows packets split across reads.

#ifndef CK_HCI_FILTER_H
#define CK_HCI_FILTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** H4 packet types, the first byte of every packet on the socket. */
#define CK_HCI_COMMAND 0x01
#define CK_HCI_ACL 0x02
#define CK_HCI_SCO 0x03
#define CK_HCI_EVENT 0x04

/** The events that carry a remote device's class of device. */
#define CK_HCI_EVENT_INQUIRY_RESULT 0x02
#define CK_HCI_EVENT_CONNECTION_REQUEST 0x04
#define CK_HCI_EVENT_INQUIRY_RESULT_WITH_RSSI 0x22
#define CK_HCI_EVENT_EXTENDED_INQUIRY_RESULT 0x2f

/** The controller's count of ACL packets it is done with, which the host's flow control runs on. */
#define CK_HCI_EVENT_NUMBER_OF_COMPLETED_PACKETS 0x13

/** ACL packet boundary flags, from host to controller: the first fragment of an L2CAP frame. */
#define CK_HCI_ACL_START 0x2

/** Class of device of the DualSense and the DualShock 4: peripheral, gamepad. */
#define CK_HCI_COD_DUALSENSE 0x002508
/** What the filter turns it into: peripheral, keyboard. */
#define CK_HCI_COD_MINOR_KEYBOARD 0x40

/**
 * First payload bytes of a packet that the filter keeps for the caller: enough
 * for a whole DualSense Bluetooth input report in one ACL packet (L2CAP
 * header, HIDP header and the 78-byte report 0x31: 83 bytes).
 */
#define CK_HCI_PREFIX_SIZE 96

typedef struct ck_hci_packet_t
{
	uint8_t type;
	/** Event code, or command opcode, or connection handle for ACL and SCO. */
	uint16_t id;
	/** ACL and SCO: the four flag bits above the handle. For ACL, (flags & 3) == 1 marks a continuing fragment. */
	uint8_t flags;
	/** Payload length from the header. */
	uint16_t length;
	/** The first min(length, CK_HCI_PREFIX_SIZE) payload bytes, after any rewrite. */
	uint8_t prefix[CK_HCI_PREFIX_SIZE];
} CKHciPacket;

/** One inquiry result or connection request the filter looked at. */
typedef struct ck_hci_device_class_t
{
	/** CK_HCI_EVENT_INQUIRY_RESULT, ..._WITH_RSSI, CK_HCI_EVENT_EXTENDED_INQUIRY_RESULT or CK_HCI_EVENT_CONNECTION_REQUEST. */
	uint8_t event;
	uint8_t bdaddr[6]; // as on the wire: least significant byte first
	uint32_t cod;      // as the controller sent it
	/** The class was a gamepad or joystick and is now a keyboard. */
	bool rewritten;
	/** A gamepad or joystick whose class arrived split across two reads: left alone. */
	bool split;
} CKHciDeviceClass;

typedef struct ck_hci_filter_t CKHciFilter;

typedef struct ck_hci_filter_callbacks_t
{
	void *user;
	/** Called for every complete packet. May be NULL. */
	void (*packet)(void *user, const CKHciPacket *packet);
	/**
	 * Called, before packet, for every connection request and every inquiry
	 * result with exactly one response. May be NULL.
	 */
	void (*device_class)(void *user, const CKHciDeviceClass *device);
	/**
	 * Only with rewrite set: called for each connection in a Number Of
	 * Completed Packets event while its count is still in the caller's
	 * buffer. The count it returns goes on in place of the controller's.
	 * May be NULL.
	 */
	uint16_t (*completed)(void *user, uint16_t handle, uint16_t count);
} CKHciFilterCallbacks;

/** Follows one direction of the stream. All state lives here; no allocation. */
struct ck_hci_filter_t
{
	CKHciFilterCallbacks cb;
	/** Rewrite gamepad classes. Only for the controller-to-host direction. */
	bool rewrite;
	/** Set when a byte that cannot start a packet showed up; the filter then stops looking. */
	bool lost_sync;

	uint8_t header[4];
	size_t header_have;
	size_t header_need;
	CKHciPacket packet;
	size_t payload_have;

	// For a packet that carries a class of device: the payload offsets of the
	// address and of the class (0 when the packet has none), the original minor
	// class byte, where that byte sits in the current buffer (NULL once that
	// read has returned), and whether a gamepad class could not be rewritten
	// because of that.
	uint8_t addr_at;
	uint8_t cod_at;
	uint8_t cod_minor;
	uint8_t *cod_minor_at;
	bool cod_split;

	// For a Number Of Completed Packets event that may be rewritten: where the
	// low byte of the count being read sits in the current buffer, or NULL.
	bool completed_event;
	uint8_t *count_low_at;

	uint32_t packets;
	uint32_t rewrites;
	/** Completed packet counts that arrived split across two reads, so could not be rewritten. */
	uint32_t completed_split;
};

void ck_hci_filter_init(CKHciFilter *filter, bool rewrite, const CKHciFilterCallbacks *callbacks);

/**
 * Feeds the next bytes of the stream, exactly as read() or write() saw them.
 * With rewrite set it may change bytes inside buf; it never changes len.
 */
void ck_hci_filter_feed(CKHciFilter *filter, uint8_t *buf, size_t len);

/** True between two packets: what goes next in the stream starts a new packet. */
bool ck_hci_filter_between_packets(const CKHciFilter *filter);

/**
 * Writes an H4 ACL packet holding one whole L2CAP frame (type, handle and
 * flags, ACL length, L2CAP length, channel ID, data) into out. Returns its
 * size, or 0 when it does not fit in size bytes.
 */
size_t ck_hci_acl_frame(uint8_t *out, size_t size, uint16_t handle, uint8_t flags, uint16_t cid, const uint8_t *data,
	size_t len);

/** True for a peripheral whose minor class is gamepad or joystick (and not keyboard or pointer). */
bool ck_hci_cod_is_gamepad(uint32_t cod);

#ifdef __cplusplus
}
#endif

#endif // CK_HCI_FILTER_H
