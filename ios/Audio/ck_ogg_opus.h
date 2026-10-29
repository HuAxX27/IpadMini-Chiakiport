// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A4.1: reads an Ogg Opus file (RFC 7845) that is already in memory, packet
// by packet: the recordings cksession -g makes (ios/Bridge/ck_recorder.h) and
// any other with one stream. Used by the tests and by ckaudio to play a
// recording through the player. Portable C.
//
// ck_recorder writes a frame lost on the network as a packet of one byte (the
// TOC alone); ck_ogg_opus_next() hands it over as it is, and
// ck_ogg_opus_packet_is_lost() tells it apart, so a reader can pass it on as
// contract C3 does: NULL with size 0.

#ifndef CK_OGG_OPUS_H
#define CK_OGG_OPUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** A packet longer than this is refused; the PS5's are a few hundred bytes. */
#define CK_OGG_OPUS_PACKET_MAX 8192

typedef struct ck_ogg_opus_t
{
	const uint8_t *data;
	size_t size;

	/** From OpusHead. */
	uint8_t channels;
	uint16_t pre_skip;
	uint32_t input_rate;
	int16_t output_gain;
	/** The last page's granule position: samples at 48 kHz, pre-skip included, up to the end. */
	uint64_t end_granule;
	uint32_t pages;

	/** Where reading is: the next page, and inside the current one. */
	size_t next_page;
	const uint8_t *lacing;
	size_t lacing_count;
	size_t lacing_index;
	const uint8_t *body;
	size_t body_pos;

	uint8_t packet[CK_OGG_OPUS_PACKET_MAX];
	size_t packet_size;
} CKOggOpus;

/**
 * Checks every page (capture pattern, version, CRC, one stream) and reads
 * OpusHead and OpusTags. Returns NULL on success, or what is wrong, in
 * Spanish, for a message.
 */
const char *ck_ogg_opus_open(CKOggOpus *ogg, const uint8_t *data, size_t size);

/** The next audio packet: 1 with *packet and *size set, 0 at the end, -1 when the file is broken. */
int ck_ogg_opus_next(CKOggOpus *ogg, const uint8_t **packet, size_t *size);

/** ck_recorder's mark for a frame lost on the network: the TOC byte alone. */
static inline bool ck_ogg_opus_packet_is_lost(size_t size)
{
	return size == 1;
}

#ifdef __cplusplus
}
#endif

#endif // CK_OGG_OPUS_H
