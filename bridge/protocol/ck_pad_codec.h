// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Helpers for the contract C4 packet (ck_pad_packet.h): write and read it byte
// by byte in little-endian whatever the host, and decide which packets a
// receiver keeps. Plain C with no Chiaki includes, so the firmware and the
// iPad receiver share it.

#ifndef CK_PAD_CODEC_H
#define CK_PAD_CODEC_H

#include "ck_pad_packet.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CK_PAD_PACKET_SIZE 20

/**
 * A receiver that has kept nothing for this long takes the next packet
 * whatever its seq. Without it, a bridge that rebooted (seq back at 0) would
 * be ignored until its seq caught up with the old one.
 */
#define CK_PAD_RESYNC_MS 500

/** Sets magic and version and zeroes the rest: nothing pressed, sticks centered. */
void ck_pad_packet_init(CKPadPacket *packet);

/** Writes the packet in wire order. */
void ck_pad_packet_write(const CKPadPacket *packet, uint8_t out[CK_PAD_PACKET_SIZE]);

/** Reads a datagram. Returns false unless it is a version 1 packet of exactly CK_PAD_PACKET_SIZE bytes. */
bool ck_pad_packet_read(CKPadPacket *packet, const uint8_t *buf, size_t len);

/** True when both packets carry the same pad state. magic, version and seq are ignored. */
bool ck_pad_packet_same_state(const CKPadPacket *a, const CKPadPacket *b);

/** True if seq comes after last, counting the wraparound at 65535. */
static inline bool ck_pad_seq_newer(uint16_t seq, uint16_t last)
{
	return (int16_t)(uint16_t)(seq - last) > 0;
}

/** Receiver side: drops duplicated, late and out of order packets. */
typedef struct ck_pad_rx_filter_t
{
	bool has_last;
	uint16_t last_seq;
	uint32_t last_ms;
} CKPadRxFilter;

void ck_pad_rx_filter_init(CKPadRxFilter *filter);

/** Returns true if the packet with this seq, received at now_ms, must be used. */
bool ck_pad_rx_filter_accept(CKPadRxFilter *filter, uint16_t seq, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif // CK_PAD_CODEC_H
