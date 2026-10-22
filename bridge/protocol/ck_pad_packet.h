// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Contract C4, wire format: the UDP packet the ESP32 bridge sends to the iPad.
// Shared by bridge/esp32 (firmware) and ios/Input (receiver). Plain C with no
// Chiaki includes, so the firmware can build it on its own.
// Owner: A5. A change to this file bumps CK_PAD_VERSION and goes in its own PR.

#ifndef CK_PAD_PACKET_H
#define CK_PAD_PACKET_H

#include <stdint.h>

#define CK_PAD_PORT 9400
#define CK_PAD_MAGIC_0 'C'
#define CK_PAD_MAGIC_1 'K'
#define CK_PAD_VERSION 1

/** flags */
#define CK_PAD_FLAG_TOUCHPAD_PRESSED (1 << 0)

/**
 * Little-endian on the wire (ESP32 and armv7 are both little-endian, so the
 * struct is sent as is). The receiver drops packets whose seq is not newer
 * than the last one it accepted, comparing with wraparound
 * ((int16_t)(seq - last) > 0), so a late packet never undoes a newer state.
 * Once 500 ms pass since the last accepted packet it takes the next one
 * whatever its seq, so a bridge that rebooted (seq back at 0) is heard again.
 * While a pad is connected the bridge repeats its state at least every 20 ms.
 */
typedef struct __attribute__((packed)) ck_pad_packet_t
{
	uint8_t magic[2]; // CK_PAD_MAGIC_0, CK_PAD_MAGIC_1
	uint8_t version;  // CK_PAD_VERSION
	uint8_t flags;    // CK_PAD_FLAG_*
	uint16_t seq;     // wraps around; compare with (int16_t)(seq - last) > 0
	uint32_t buttons; // same bits as ChiakiControllerButton in lib/include/chiaki/controller.h
	uint8_t l2;
	uint8_t r2;
	int16_t lx;
	int16_t ly;
	int16_t rx;
	int16_t ry;
} CKPadPacket;

_Static_assert(sizeof(CKPadPacket) == 20, "CKPadPacket must stay 20 bytes on the wire");

#endif // CK_PAD_PACKET_H
