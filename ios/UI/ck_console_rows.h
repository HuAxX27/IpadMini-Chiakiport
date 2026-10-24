// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A6.1, A6.2: the rows of the console list, built from what discovery found
// (CKConsoles.h, contract C1) and the saved registrations (ck_registry.h,
// contract C5). Plain C, tested on Linux (ios/tests/ui/console_rows.c): the
// list screen only draws the rows and runs their action.

#ifndef CK_CONSOLE_ROWS_H
#define CK_CONSOLE_ROWS_H

#include "../Bridge/CKConsoles.h"
#include "../Bridge/ck_registry.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Every console discovery can report plus every saved registration. */
#define CK_CONSOLE_ROWS_MAX (CK_CONSOLES_MAX + CK_REGISTRY_MAX)

typedef enum ck_console_row_state_t
{
	/** Answered the search, turned on. */
	CK_CONSOLE_ROW_READY,
	/** Answered the search from rest mode. */
	CK_CONSOLE_ROW_STANDBY,
	/** Answered with a state the core does not know. */
	CK_CONSOLE_ROW_UNKNOWN,
	/** Registered, but it did not answer: off, unplugged or on another network. */
	CK_CONSOLE_ROW_MISSING,
} CKConsoleRowState;

/** What a tap on the row does. */
typedef enum ck_console_action_t
{
	CK_CONSOLE_ACTION_NONE,
	/** Not registered and on: open the registration (A6.3). */
	CK_CONSOLE_ACTION_REGISTER,
	/** Registered and in rest mode, or missing with a known IP: send the wake-up. */
	CK_CONSOLE_ACTION_WAKE,
	/** Registered and on: open the game screen (A6.5). */
	CK_CONSOLE_ACTION_PLAY,
} CKConsoleAction;

typedef struct ck_console_row_t
{
	/** The name the console answered with, or the one saved when it registered. */
	char name[64];
	/** The address it answered from, else the last known one, else empty. */
	char addr[48];
	/** 12 hex digits in uppercase: CKConsole.host_id and the registry's key. */
	char host_id[32];
	CKConsoleRowState state;
	bool registered;
	bool ps5;
	/** The saved target when registered, else the discovered one. */
	ChiakiTarget target;
	/** The running game, empty when none or unknown. */
	char running_app_name[64];
	CKConsoleAction action;
} CKConsoleRow;

/**
 * Fills rows with one row per discovered console and one per registered
 * console that did not answer, and returns how many it wrote, at most
 * rows_max. Registered consoles come first, then by name ignoring ASCII case,
 * then by host_id, so rows keep their place while the list refreshes.
 * registry may be NULL: nothing is registered.
 */
size_t ck_console_rows_build(const CKConsole *consoles, size_t count, const CKRegistry *registry,
	CKConsoleRow *rows, size_t rows_max);

/**
 * A6.2: writes the address each registered console answered from into its
 * last known IP. Returns whether any entry changed, so the caller saves the
 * registry only then.
 */
bool ck_console_rows_update_registry(const CKConsole *consoles, size_t count, CKRegistry *registry);

/** "encendida", "en reposo", "estado desconocido" or "no contesta". */
const char *ck_console_row_state_string(CKConsoleRowState state);

/** The button's title: "Jugar", "Despertar", "Registrar", or "" for none. */
const char *ck_console_action_string(CKConsoleAction action);

#ifdef __cplusplus
}
#endif

#endif // CK_CONSOLE_ROWS_H
