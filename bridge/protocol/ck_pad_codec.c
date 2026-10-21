// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "ck_pad_codec.h"

#include <string.h>

_Static_assert(sizeof(CKPadPacket) == CK_PAD_PACKET_SIZE, "CK_PAD_PACKET_SIZE must match CKPadPacket");

static void put_u16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v)
{
	put_u16(p, (uint16_t)v);
	put_u16(p + 2, (uint16_t)(v >> 16));
}

static uint16_t get_u16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t get_u32(const uint8_t *p)
{
	return (uint32_t)get_u16(p) | ((uint32_t)get_u16(p + 2) << 16);
}

void ck_pad_packet_init(CKPadPacket *packet)
{
	memset(packet, 0, sizeof(*packet));
	packet->magic[0] = CK_PAD_MAGIC_0;
	packet->magic[1] = CK_PAD_MAGIC_1;
	packet->version = CK_PAD_VERSION;
}

void ck_pad_packet_write(const CKPadPacket *packet, uint8_t out[CK_PAD_PACKET_SIZE])
{
	out[0] = packet->magic[0];
	out[1] = packet->magic[1];
	out[2] = packet->version;
	out[3] = packet->flags;
	put_u16(out + 4, packet->seq);
	put_u32(out + 6, packet->buttons);
	out[10] = packet->l2;
	out[11] = packet->r2;
	put_u16(out + 12, (uint16_t)packet->lx);
	put_u16(out + 14, (uint16_t)packet->ly);
	put_u16(out + 16, (uint16_t)packet->rx);
	put_u16(out + 18, (uint16_t)packet->ry);
}

bool ck_pad_packet_read(CKPadPacket *packet, const uint8_t *buf, size_t len)
{
	if(len != CK_PAD_PACKET_SIZE || buf[0] != CK_PAD_MAGIC_0 || buf[1] != CK_PAD_MAGIC_1 || buf[2] != CK_PAD_VERSION)
		return false;
	packet->magic[0] = buf[0];
	packet->magic[1] = buf[1];
	packet->version = buf[2];
	packet->flags = buf[3];
	packet->seq = get_u16(buf + 4);
	packet->buttons = get_u32(buf + 6);
	packet->l2 = buf[10];
	packet->r2 = buf[11];
	packet->lx = (int16_t)get_u16(buf + 12);
	packet->ly = (int16_t)get_u16(buf + 14);
	packet->rx = (int16_t)get_u16(buf + 16);
	packet->ry = (int16_t)get_u16(buf + 18);
	return true;
}

bool ck_pad_packet_same_state(const CKPadPacket *a, const CKPadPacket *b)
{
	return a->flags == b->flags
		&& a->buttons == b->buttons
		&& a->l2 == b->l2
		&& a->r2 == b->r2
		&& a->lx == b->lx
		&& a->ly == b->ly
		&& a->rx == b->rx
		&& a->ry == b->ry;
}

void ck_pad_rx_filter_init(CKPadRxFilter *filter)
{
	memset(filter, 0, sizeof(*filter));
}

bool ck_pad_rx_filter_accept(CKPadRxFilter *filter, uint16_t seq, uint32_t now_ms)
{
	bool resync = !filter->has_last || (uint32_t)(now_ms - filter->last_ms) >= CK_PAD_RESYNC_MS;
	if(!resync && !ck_pad_seq_newer(seq, filter->last_seq))
		return false;
	filter->has_last = true;
	filter->last_seq = seq;
	filter->last_ms = now_ms;
	return true;
}
