// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Contract C4 (A5, A6 -> A2): every controller, physical or on screen, is an
// input source that yields a ChiakiControllerState.
// Owner: A5. A change to this file goes in its own PR, approved by A0.

#ifndef CK_INPUT_SOURCE_H
#define CK_INPUT_SOURCE_H

#include <chiaki/controller.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** How often CKSession polls its sources. 8 ms = 125 Hz. */
#define CK_INPUT_POLL_INTERVAL_MS 8

typedef struct ck_input_source_t
{
	/** Short name for logs, e.g. "esp32-udp" or "touch". */
	const char *name;
	void *user;

	/**
	 * Writes the source's latest state into *state and returns true, or
	 * returns false when it has nothing to report (disconnected or idle).
	 * Called on CKSession's input thread every CK_INPUT_POLL_INTERVAL_MS.
	 * Must not block: hold a lock for microseconds at most.
	 *
	 * CKSession merges all sources with chiaki_controller_state_or() before
	 * chiaki_session_set_controller_state(), so the touch overlay and the
	 * DualSense can be used at the same time.
	 */
	bool (*poll)(void *user, ChiakiControllerState *state);
} CKInputSource;

#ifdef __cplusplus
}
#endif

#endif // CK_INPUT_SOURCE_H
