// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A6.3: what the app does when a registration ends, apart from UIKit, so it
// is tested on Linux: the sentence for the user, and saving the registration
// on top of the registry file, as ckctl regist does.

#ifndef CK_UI_REGIST_H
#define CK_UI_REGIST_H

#include "Bridge/CKConsoles.h"
#include "Bridge/ck_registry.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ck_ui_regist_save_t
{
	CK_UI_REGIST_SAVED,
	/** The file could not be read, so it was not written over: it may hold registrations. */
	CK_UI_REGIST_UNREADABLE,
	/** CK_REGISTRY_MAX other consoles are registered already. */
	CK_UI_REGIST_FULL,
	/** The file could not be written. */
	CK_UI_REGIST_UNWRITABLE,
} CKUIRegistSave;

/**
 * Reads path, adds host with last_host (NULL when unknown), or replaces the
 * registration with the same server_mac, and writes it back. Reading right
 * before writing keeps what ckctl wrote since the app last read the file.
 * On CK_UI_REGIST_SAVED, registry gets what the file now holds. err, which may
 * be NULL, gets the core's error for UNREADABLE and UNWRITABLE.
 */
CKUIRegistSave ck_ui_regist_save(const char *path, const ChiakiRegisteredHost *host, const char *last_host,
	CKRegistry *registry, ChiakiErrorCode *err);

/** Room for any ck_ui_regist_message(). */
#define CK_UI_REGIST_MESSAGE_SIZE 256

/**
 * The sentence for how a registration ended: ck_regist_result_string() and,
 * when the console gave them, its HTTP status and reason, as ckctl prints
 * them. Always NUL-terminated, and cut at a UTF-8 boundary when out is short.
 */
void ck_ui_regist_message(const CKRegistOutcome *outcome, char *out, size_t size);

#ifdef __cplusplus
}
#endif

#endif // CK_UI_REGIST_H
