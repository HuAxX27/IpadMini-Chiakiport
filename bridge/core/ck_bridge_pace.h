// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Decides when the bridge sends a packet:
// - a change of buttons or flags goes out right away;
// - stick and trigger motion goes out at most every min_interval_ms, and stick
//   jitter under stick_threshold waits for the next keepalive;
// - with no change, the state is repeated every keepalive_ms, so a lost packet
//   is fixed quickly and the iPad knows the pad is still there;
// - when the pad goes away, a neutral state is sent release_count times so
//   nothing stays pressed, and then the bridge goes quiet.
// Pure logic with the clock passed in, so it is tested on Linux.

#ifndef CK_BRIDGE_PACE_H
#define CK_BRIDGE_PACE_H

#include "ck_pad_packet.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CK_BRIDGE_PACE_WAIT_FOREVER UINT32_MAX

typedef struct ck_bridge_pace_config_t
{
	uint32_t min_interval_ms;
	uint32_t keepalive_ms;
	int32_t stick_threshold; // in the int16 units of CKPadPacket
	uint32_t release_count;
} CKBridgePaceConfig;

typedef struct ck_bridge_pace_t
{
	CKBridgePaceConfig config;
	CKPadPacket sent; // last packet sent
	bool has_sent;
	bool pad_present;
	uint32_t last_send_ms;
	uint32_t releases_left;
	uint16_t next_seq;
} CKBridgePace;

/** 4 ms, 20 ms, 512 (about 2 counts of the DualSense's 8-bit sticks) and 3. */
void ck_bridge_pace_default_config(CKBridgePaceConfig *config);

void ck_bridge_pace_init(CKBridgePace *pace, const CKBridgePaceConfig *config);

/**
 * Call it when the pad state may have changed and when the last wait runs out.
 * state is the current pad state, or NULL when no pad is connected.
 * Returns true when *out must be sent now; *out then carries the next seq.
 * Always sets *wait_ms to how long the caller may sleep if nothing changes,
 * CK_BRIDGE_PACE_WAIT_FOREVER when there is nothing left to send.
 */
bool ck_bridge_pace_poll(CKBridgePace *pace, const CKPadPacket *state, uint32_t now_ms, CKPadPacket *out, uint32_t *wait_ms);

#ifdef __cplusplus
}
#endif

#endif // CK_BRIDGE_PACE_H
