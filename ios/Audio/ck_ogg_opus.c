// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A4.1: see ck_ogg_opus.h. Pages follow RFC 3533 and the Opus headers
// RFC 7845, as ios/Bridge/ck_recorder.c writes them.

#include "ck_ogg_opus.h"

#include <string.h>

#define PAGE_HEADER 27
#define FLAG_CONTINUED 0x01
#define FLAG_BOS 0x02
#define OPUS_HEAD_SIZE 19
/** A page with no packet ending on it says so with this granule position. */
#define GRANULE_NONE UINT64_MAX

static uint32_t le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t le64(const uint8_t *p)
{
	return (uint64_t)le32(p) | (uint64_t)le32(p + 4) << 32;
}

/** Ogg's CRC-32 (polynomial 0x04c11db7, not reflected), over the page with its CRC field as zeros. */
static uint32_t page_crc(const uint8_t *page, size_t size)
{
	uint32_t crc = 0;
	for(size_t i = 0; i < size; i++)
	{
		uint8_t b = i >= 22 && i < 26 ? 0 : page[i];
		crc ^= (uint32_t)b << 24;
		for(int k = 0; k < 8; k++)
			crc = crc & 0x80000000u ? crc << 1 ^ 0x04c11db7u : crc << 1;
	}
	return crc;
}

/** The size of the page at pos, or 0 when it is not a whole, valid one. */
static size_t page_size(const uint8_t *data, size_t size, size_t pos)
{
	if(pos > size || size - pos < PAGE_HEADER)
		return 0;
	const uint8_t *p = data + pos;
	if(memcmp(p, "OggS", 4) != 0 || p[4] != 0)
		return 0;
	size_t segments = p[26];
	if(size - pos < PAGE_HEADER + segments)
		return 0;
	size_t total = PAGE_HEADER + segments;
	for(size_t i = 0; i < segments; i++)
		total += p[PAGE_HEADER + i];
	if(size - pos < total || page_crc(p, total) != le32(p + 22))
		return 0;
	return total;
}

static int next_packet(CKOggOpus *ogg, const uint8_t **packet, size_t *size)
{
	ogg->packet_size = 0;
	for(;;)
	{
		if(ogg->lacing_index >= ogg->lacing_count)
		{
			// A packet still open at the end of the file was cut short.
			if(ogg->next_page >= ogg->size)
				return ogg->packet_size ? -1 : 0;
			size_t n = page_size(ogg->data, ogg->size, ogg->next_page);
			if(!n)
				return -1;
			const uint8_t *p = ogg->data + ogg->next_page;
			// Only a packet longer than its page goes on, and then the next page says so.
			bool continued = (p[5] & FLAG_CONTINUED) != 0;
			if(continued != (ogg->packet_size > 0))
				return -1;
			ogg->lacing = p + PAGE_HEADER;
			ogg->lacing_count = p[26];
			ogg->lacing_index = 0;
			ogg->body = p + PAGE_HEADER + ogg->lacing_count;
			ogg->body_pos = 0;
			ogg->next_page += n;
			continue;
		}
		uint8_t lacing = ogg->lacing[ogg->lacing_index++];
		if(ogg->packet_size + lacing > CK_OGG_OPUS_PACKET_MAX)
			return -1;
		memcpy(ogg->packet + ogg->packet_size, ogg->body + ogg->body_pos, lacing);
		ogg->packet_size += lacing;
		ogg->body_pos += lacing;
		// A lacing value under 255 ends the packet.
		if(lacing < 255)
		{
			*packet = ogg->packet;
			*size = ogg->packet_size;
			return 1;
		}
	}
}

const char *ck_ogg_opus_open(CKOggOpus *ogg, const uint8_t *data, size_t size)
{
	memset(ogg, 0, sizeof(*ogg));
	ogg->data = data;
	ogg->size = size;
	ogg->end_granule = 0;

	// Every page first, so a broken file is refused before any sound.
	uint32_t serial = 0;
	for(size_t pos = 0; pos < size;)
	{
		size_t n = page_size(data, size, pos);
		if(!n)
			return ogg->pages ? "una página está dañada o cortada" : "no es un archivo Ogg";
		const uint8_t *p = data + pos;
		if(ogg->pages == 0)
		{
			if(!(p[5] & FLAG_BOS))
				return "la primera página no abre el stream";
			serial = le32(p + 14);
		}
		else if(le32(p + 14) != serial)
			return "tiene más de un stream";
		uint64_t granule = le64(p + 6);
		if(granule != GRANULE_NONE)
			ogg->end_granule = granule;
		ogg->pages++;
		pos += n;
	}
	if(!ogg->pages)
		return "está vacío";

	const uint8_t *packet;
	size_t packet_size;
	if(next_packet(ogg, &packet, &packet_size) != 1 || packet_size < OPUS_HEAD_SIZE
		|| memcmp(packet, "OpusHead", 8) != 0)
		return "no empieza con OpusHead";
	// Version 0.x: the high nibble is the major version, which a reader must know.
	if((packet[8] & 0xf0) != 0)
		return "la versión de OpusHead es desconocida";
	ogg->channels = packet[9];
	ogg->pre_skip = (uint16_t)(packet[10] | packet[11] << 8);
	ogg->input_rate = le32(packet + 12);
	ogg->output_gain = (int16_t)(uint16_t)(packet[16] | packet[17] << 8);
	// Mapping family 0: one stream of one or two channels, the only kind the PS5 sends.
	if(packet[18] != 0 || ogg->channels < 1 || ogg->channels > 2)
		return "no es Opus de uno o dos canales (familia 0)";

	if(next_packet(ogg, &packet, &packet_size) != 1 || packet_size < 8 || memcmp(packet, "OpusTags", 8) != 0)
		return "falta OpusTags";
	return NULL;
}

int ck_ogg_opus_next(CKOggOpus *ogg, const uint8_t **packet, size_t *size)
{
	return next_packet(ogg, packet, size);
}
