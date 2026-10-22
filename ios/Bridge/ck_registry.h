// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A2.5: the saved registrations, in the plist that contract C5 describes
// (docs/ipad/contracts.md): one dictionary per console, keyed by server_mac
// in hex, with the fields of ChiakiRegisteredHost that a session needs and
// the console's last known IP. Plain C, so ckctl and the Linux tests
// (ios/tests/core/registry.c) use the same code as the app.
//
// The file is an XML plist that NSDictionary and NSPropertyListSerialization
// read and write as is:
//
//   <dict>
//     <key>0A1B2C3D4E5F</key>          server_mac in uppercase hex
//     <dict>
//       <key>target</key>              <integer>, a ChiakiTarget
//       <key>server_nickname</key>     <string>
//       <key>server_mac</key>          <data>, 6 bytes
//       <key>rp_regist_key</key>       <data>, 16 bytes, NUL-padded text
//       <key>rp_key_type</key>         <integer>
//       <key>rp_key</key>              <data>, 16 bytes
//       <key>host</key>                <string>, the last known IP
//     </dict>
//   </dict>
//
// The reader skips keys it does not know, so the app may add its own. It
// reads XML only: whoever rewrites the file keeps it in XML. The file holds
// the console's keys: it is written with mode 0600 and never goes in the repo.

#ifndef CK_REGISTRY_H
#define CK_REGISTRY_H

#include <chiaki/common.h>
#include <chiaki/regist.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Where ckctl keeps the registrations on the iPad, readable by the app (mobile). */
#define CK_REGISTRY_DEFAULT_PATH "/var/mobile/Library/Chiakiport/consoles.plist"

#define CK_REGISTRY_MAX 8

typedef struct ck_registry_entry_t
{
	/** target, server_nickname, server_mac, rp_regist_key, rp_key_type and rp_key; the rest is zero. */
	ChiakiRegisteredHost host;
	/** The console's last known IP, empty when unknown. */
	char last_host[48];
} CKRegistryEntry;

typedef struct ck_registry_t
{
	size_t count;
	CKRegistryEntry entries[CK_REGISTRY_MAX];
} CKRegistry;

/**
 * Reads path into registry. A missing file is an empty registry. A file that
 * is not a registry plist fails with CHIAKI_ERR_INVALID_DATA, and then the
 * caller must not save over it: it may hold registrations it could not read.
 */
ChiakiErrorCode ck_registry_load(CKRegistry *registry, const char *path);

/**
 * Writes registry to path, through a temporary file renamed over it, so a
 * crash never leaves half a file. Creates the folder if needed. When run as
 * root, as ckctl over SSH, the file and the folder it creates get the owner
 * of the folder above, so the app, which runs as mobile, can still read them.
 */
ChiakiErrorCode ck_registry_save(const CKRegistry *registry, const char *path);

/** "0A1B2C3D4E5F", the registry key and CKConsole.host_id: out must hold 13 chars. */
void ck_registry_mac_hex(const uint8_t mac[6], char out[13]);

/** The entry for this server_mac, or NULL. */
CKRegistryEntry *ck_registry_find_mac(CKRegistry *registry, const uint8_t mac[6]);

/**
 * The entry of a discovered console: host_id is CKConsole.host_id, 12 hex
 * digits in any case. NULL when it is not registered.
 */
CKRegistryEntry *ck_registry_find_host_id(CKRegistry *registry, const char *host_id);

/**
 * The entry whose server_mac in hex (any case, as discovery's host_id),
 * last known IP or nickname is query, tried in that order, or NULL.
 */
CKRegistryEntry *ck_registry_find(CKRegistry *registry, const char *query);

/**
 * Adds a registration, or replaces the one with the same server_mac. last_host
 * may be NULL. Fails with CHIAKI_ERR_OVERFLOW when the registry is full.
 */
ChiakiErrorCode ck_registry_put(CKRegistry *registry, const ChiakiRegisteredHost *host, const char *last_host);

/** Removes the entry. Returns whether there was one. */
bool ck_registry_remove(CKRegistry *registry, CKRegistryEntry *entry);

#ifdef __cplusplus
}
#endif

#endif // CK_REGISTRY_H
