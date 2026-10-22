// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "ck_hci_filter.h"

#include <string.h>

// Payload offsets in the events that carry a class of device, which is
// little-endian: minor byte first, then major, then service.
//   Inquiry Result: num_responses, BD_ADDR (6), page scan repetition mode,
//     reserved (2), class of device (3), clock offset (2)
//   Inquiry Result with RSSI and Extended Inquiry Result: num_responses,
//     BD_ADDR (6), page scan repetition mode, reserved, class of device (3), ...
//   Connection Request: BD_ADDR (6), class of device (3), link type
// Inquiry results with more than one response put each field in an array; the
// filter leaves those alone, as controllers send one response per event.
#define INQUIRY_NUM_RESPONSES 0

#define COD_MAJOR_MASK 0x1f00
#define COD_MAJOR_PERIPHERAL 0x0500
#define COD_MINOR_POINTING_KEYBOARD_MASK 0xc0
#define COD_MINOR_DEVICE_MASK 0x3c
#define COD_MINOR_JOYSTICK 0x04
#define COD_MINOR_GAMEPAD 0x08

bool ck_hci_cod_is_gamepad(uint32_t cod)
{
	if((cod & COD_MAJOR_MASK) != COD_MAJOR_PERIPHERAL)
		return false;
	uint8_t minor = cod & 0xff;
	if(minor & COD_MINOR_POINTING_KEYBOARD_MASK)
		return false;
	uint8_t device = minor & COD_MINOR_DEVICE_MASK;
	return device == COD_MINOR_JOYSTICK || device == COD_MINOR_GAMEPAD;
}

void ck_hci_filter_init(CKHciFilter *filter, bool rewrite, const CKHciFilterCallbacks *callbacks)
{
	memset(filter, 0, sizeof(*filter));
	filter->rewrite = rewrite;
	if(callbacks)
		filter->cb = *callbacks;
}

// Finds where a packet keeps the address and class of device, once its header is known.
static void find_class(CKHciFilter *filter)
{
	const CKHciPacket *p = &filter->packet;
	uint8_t addr_at = 0, cod_at = 0;
	if(p->type == CK_HCI_EVENT)
	{
		switch(p->id)
		{
			case CK_HCI_EVENT_INQUIRY_RESULT: addr_at = 1; cod_at = 10; break;
			case CK_HCI_EVENT_INQUIRY_RESULT_WITH_RSSI:
			case CK_HCI_EVENT_EXTENDED_INQUIRY_RESULT: addr_at = 1; cod_at = 9; break;
			case CK_HCI_EVENT_CONNECTION_REQUEST: addr_at = 0; cod_at = 6; break;
			default: break;
		}
	}
	if(cod_at && p->length < cod_at + 3)
		cod_at = 0;
	filter->addr_at = addr_at;
	filter->cod_at = cod_at;
	filter->completed_event = p->type == CK_HCI_EVENT && p->id == CK_HCI_EVENT_NUMBER_OF_COMPLETED_PACKETS
		&& filter->rewrite && filter->cb.completed;
}

// One device per event: always for a connection request, only with
// num_responses 1 for an inquiry result. Needs the payload's first byte.
static bool single_device(const CKHciPacket *p)
{
	return p->id == CK_HCI_EVENT_CONNECTION_REQUEST || p->prefix[INQUIRY_NUM_RESPONSES] == 1;
}

static void start_packet(CKHciFilter *filter, uint8_t type)
{
	switch(type)
	{
		case CK_HCI_COMMAND: filter->header_need = 3; break; // opcode (2), length (1)
		case CK_HCI_ACL: filter->header_need = 4; break;     // handle and flags (2), length (2)
		case CK_HCI_SCO: filter->header_need = 3; break;     // handle and flags (2), length (1)
		case CK_HCI_EVENT: filter->header_need = 2; break;   // code (1), length (1)
		default:
			filter->lost_sync = true;
			return;
	}
	memset(&filter->packet, 0, sizeof(filter->packet));
	filter->packet.type = type;
	filter->header_have = 0;
	filter->payload_have = 0;
	filter->addr_at = 0;
	filter->cod_at = 0;
	filter->cod_minor = 0;
	filter->cod_minor_at = NULL;
	filter->cod_split = false;
	filter->completed_event = false;
	filter->count_low_at = NULL;
}

static void parse_header(CKHciFilter *filter)
{
	const uint8_t *h = filter->header;
	CKHciPacket *p = &filter->packet;
	switch(p->type)
	{
		case CK_HCI_COMMAND:
			p->id = (uint16_t)(h[0] | (h[1] << 8));
			p->length = h[2];
			break;
		case CK_HCI_ACL:
			p->id = (uint16_t)((h[0] | (h[1] << 8)) & 0x0fff);
			p->flags = h[1] >> 4;
			p->length = (uint16_t)(h[2] | (h[3] << 8));
			break;
		case CK_HCI_SCO:
			p->id = (uint16_t)((h[0] | (h[1] << 8)) & 0x0fff);
			p->flags = h[1] >> 4;
			p->length = h[2];
			break;
		default: // CK_HCI_EVENT
			p->id = h[0];
			p->length = h[1];
			break;
	}
}

static void end_packet(CKHciFilter *filter)
{
	const CKHciPacket *p = &filter->packet;
	filter->packets++;
	filter->header_need = 0;

	uint8_t c = filter->cod_at;
	if(c && single_device(p) && filter->cb.device_class)
	{
		CKHciDeviceClass device;
		device.event = (uint8_t)p->id;
		memcpy(device.bdaddr, p->prefix + filter->addr_at, sizeof(device.bdaddr));
		device.cod = filter->cod_minor | (uint32_t)p->prefix[c + 1] << 8 | (uint32_t)p->prefix[c + 2] << 16;
		device.rewritten = p->prefix[c] != filter->cod_minor;
		device.split = filter->cod_split;
		filter->cb.device_class(filter->cb.user, &device);
	}
	if(filter->cb.packet)
		filter->cb.packet(filter->cb.user, p);
}

// The major class byte has just arrived, so minor and major are both known:
// rewrite the minor byte if it is a gamepad and the byte is still in the
// caller's buffer.
static void check_class(CKHciFilter *filter)
{
	CKHciPacket *p = &filter->packet;
	uint32_t cod = filter->cod_minor | (uint32_t)p->prefix[filter->cod_at + 1] << 8;
	if(!filter->rewrite || !single_device(p) || !ck_hci_cod_is_gamepad(cod))
		return;
	if(!filter->cod_minor_at)
	{
		filter->cod_split = true; // the minor byte already went back to BTServer
		return;
	}
	uint8_t minor = (uint8_t)((filter->cod_minor & 0x03) | CK_HCI_COD_MINOR_KEYBOARD);
	*filter->cod_minor_at = minor;
	p->prefix[filter->cod_at] = minor;
	filter->rewrites++;
}

// Number Of Completed Packets: the number of handles, then a handle (2) and a
// count (2) for each, little-endian. The high byte of a count has just
// arrived at payload offset at: hand the count to the caller, and put back
// what it returns if both bytes are still in the caller's buffer.
static void check_completed(CKHciFilter *filter, size_t at, uint8_t *high_at)
{
	CKHciPacket *p = &filter->packet;
	if(!filter->count_low_at)
	{
		filter->completed_split++;
		return;
	}
	uint16_t handle = (uint16_t)((p->prefix[at - 3] | (p->prefix[at - 2] << 8)) & 0x0fff);
	uint16_t count = (uint16_t)(p->prefix[at - 1] | (p->prefix[at] << 8));
	uint16_t now = filter->cb.completed(filter->cb.user, handle, count);
	if(now == count)
		return;
	*filter->count_low_at = p->prefix[at - 1] = (uint8_t)(now & 0xff);
	*high_at = p->prefix[at] = (uint8_t)(now >> 8);
}

void ck_hci_filter_feed(CKHciFilter *filter, uint8_t *buf, size_t len)
{
	// A pointer into an earlier buffer is no use: that read already returned.
	filter->cod_minor_at = NULL;
	filter->count_low_at = NULL;

	for(size_t i = 0; i < len && !filter->lost_sync; i++)
	{
		uint8_t b = buf[i];
		if(filter->header_need == 0)
		{
			start_packet(filter, b);
			continue;
		}
		if(filter->header_have < filter->header_need)
		{
			filter->header[filter->header_have++] = b;
			if(filter->header_have == filter->header_need)
			{
				parse_header(filter);
				find_class(filter);
				if(filter->packet.length == 0)
					end_packet(filter);
			}
			continue;
		}

		CKHciPacket *p = &filter->packet;
		size_t at = filter->payload_have++;
		if(at < CK_HCI_PREFIX_SIZE)
			p->prefix[at] = b;
		if(filter->cod_at)
		{
			if(at == filter->cod_at)
			{
				filter->cod_minor = b;
				filter->cod_minor_at = buf + i;
			}
			else if(at == (size_t)filter->cod_at + 1)
				check_class(filter);
		}
		// Counts past the kept prefix are left alone: that is the 24th handle on.
		if(filter->completed_event && at >= 1 && at < CK_HCI_PREFIX_SIZE && (at - 1) / 4 < p->prefix[0])
		{
			if((at - 1) % 4 == 2)
				filter->count_low_at = buf + i;
			else if((at - 1) % 4 == 3)
				check_completed(filter, at, buf + i);
		}
		if(filter->payload_have == p->length)
			end_packet(filter);
	}
}

bool ck_hci_filter_between_packets(const CKHciFilter *filter)
{
	return filter->header_need == 0 && !filter->lost_sync;
}

size_t ck_hci_acl_frame(uint8_t *out, size_t size, uint16_t handle, uint8_t flags, uint16_t cid, const uint8_t *data,
	size_t len)
{
	if(len > 0xffff - 4 || size < 9 + len)
		return 0;
	size_t acl_len = len + 4;
	out[0] = CK_HCI_ACL;
	out[1] = (uint8_t)(handle & 0xff);
	out[2] = (uint8_t)(((handle >> 8) & 0x0f) | (flags << 4));
	out[3] = (uint8_t)(acl_len & 0xff);
	out[4] = (uint8_t)(acl_len >> 8);
	out[5] = (uint8_t)(len & 0xff);
	out[6] = (uint8_t)(len >> 8);
	out[7] = (uint8_t)(cid & 0xff);
	out[8] = (uint8_t)(cid >> 8);
	memcpy(out + 9, data, len);
	return 9 + len;
}
