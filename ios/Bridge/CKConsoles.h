// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Consoles on the LAN: discovery, registration and wake-up. ckctl (A2.4) uses
// it from the command line, and the console list and registration screens
// (A6.2, A6.3) from the app. Plain C, tested on Linux against a fake PS5
// (ios/tests/core/consoles.c). The implementation is ios/Bridge/ck_consoles.c.
// Owner: A2. A change to this file goes in its own PR, approved by A0.
//
// Callbacks run on core threads. The app hops to the main thread itself,
// copying what it needs, and never calls back into this API from a callback.

#ifndef CK_CONSOLES_H
#define CK_CONSOLES_H

#include <chiaki/common.h>
#include <chiaki/discovery.h>
#include <chiaki/log.h>
#include <chiaki/regist.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Consoles a browser or a one-shot discovery keeps track of at once. */
#define CK_CONSOLES_MAX 8

/**
 * A console that answered a discovery search. Every string is a copy, cut to
 * fit and always NUL-terminated, so a CKConsole can be kept and copied freely.
 */
typedef struct ck_console_t
{
	/** IPv4 address, e.g. "192.168.0.10". */
	char addr[48];
	/** The name set on the console. */
	char name[64];
	/**
	 * The console's MAC address as 12 hex digits, uppercase whatever case the
	 * console sent. It equals the uppercase hex of
	 * ChiakiRegisteredHost.server_mac, the key of the saved registration (C5),
	 * which is how the app matches a discovered console with it.
	 */
	char host_id[32];
	/** Firmware as the console reports it, e.g. "09060000". */
	char system_version[16];
	/** The running game, both empty when none. */
	char running_app_titleid[16];
	char running_app_name[64];
	/** CHIAKI_DISCOVERY_HOST_STATE_READY (on) or _STANDBY (rest mode). */
	ChiakiDiscoveryHostState state;
	bool ps5;
	/** What to register and stream with: CHIAKI_TARGET_PS5_1 for any PS5. */
	ChiakiTarget target;
} CKConsole;

/** "encendida", "en reposo" or "desconocido", for the UI and ckctl. */
const char *ck_console_state_string(ChiakiDiscoveryHostState state);

// ---------------------------------------------------------------------------
// Discovery
// ---------------------------------------------------------------------------

/**
 * Searches for consoles for up to timeout_ms, repeating the search every
 * 500 ms in case a packet is lost, and fills consoles[0..*count).
 *
 * host NULL broadcasts the search on every IPv4 interface. host set to an
 * address asks only that console and returns as soon as it answers, so a
 * console that is known to be there costs one round trip.
 *
 * Returns CHIAKI_ERR_SUCCESS even when no console answered (*count == 0), and
 * an error only when the search could not be sent at all.
 */
ChiakiErrorCode ck_consoles_discover(ChiakiLog *log, const char *host, uint32_t timeout_ms,
	CKConsole *consoles, size_t consoles_max, size_t *count);

/**
 * The console list as it changes: called with the whole list every time a
 * console appears, changes (state, name, running game) or stops answering
 * for 3 searches in a row. consoles is only valid during the call.
 */
typedef void (*CKConsoleBrowserCb)(const CKConsole *consoles, size_t count, void *user);

typedef struct ck_console_browser_t CKConsoleBrowser;

/**
 * Starts searching every second, as ck_consoles_discover() does with host,
 * until ck_console_browser_stop(). log must outlive the browser.
 */
ChiakiErrorCode ck_console_browser_start(CKConsoleBrowser **browser, ChiakiLog *log, const char *host,
	CKConsoleBrowserCb cb, void *user);

/** Stops the searches, waits for the browser's threads and frees it. NULL is fine. */
void ck_console_browser_stop(CKConsoleBrowser *browser);

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

/**
 * Parses a PSN Account ID as base64 (12 characters, what chiaki-ng shows) or
 * as the decimal number (stored as 8 little-endian bytes). Spaces around it
 * are ignored. An all-zero ID is rejected: it means the account is unknown.
 */
ChiakiErrorCode ck_parse_psn_account_id(const char *text, uint8_t account_id[CHIAKI_PSN_ACCOUNT_ID_SIZE]);

/** Parses the 8-digit PIN the console shows; spaces and hyphens are ignored. */
ChiakiErrorCode ck_parse_regist_pin(const char *text, uint32_t *pin);

/** ck_regist_start() copies the request: host only has to live during the call. */
typedef struct ck_regist_request_t
{
	/** The console's address. */
	const char *host;
	/** CKConsole.target, or CHIAKI_TARGET_PS5_1 when the console is not discovered. */
	ChiakiTarget target;
	uint8_t psn_account_id[CHIAKI_PSN_ACCOUNT_ID_SIZE];
	uint32_t pin;
} CKRegistRequest;

/**
 * How a registration ended. The core only says whether it worked; ck_regist
 * tells the cases apart so the UI can say what to fix.
 */
typedef enum ck_regist_result_t
{
	CK_REGIST_SUCCESS,
	CK_REGIST_CANCELED,
	/** The address does not resolve. */
	CK_REGIST_BAD_HOST,
	/** Nothing answered the search: wrong address, console off or not ready to link. */
	CK_REGIST_NOT_FOUND,
	/** No route to the console, or it refused the connection or dropped it. */
	CK_REGIST_UNREACHABLE,
	/** Wrong or expired PIN (application reason 0x80108b09). */
	CK_REGIST_WRONG_PIN,
	/** The console rejected the PSN Account ID (application reason 0x80108b02). */
	CK_REGIST_WRONG_ACCOUNT,
	/** Any other refusal; see http_code and reason in CKRegistOutcome. */
	CK_REGIST_REJECTED,
	/** The console accepted the request but sent no answer in time. */
	CK_REGIST_NO_REPLY,
	/** The answer came without the keys, or could not be read. */
	CK_REGIST_BAD_REPLY,
	/** Anything else: the request could not be built or sent. */
	CK_REGIST_FAILED,
} CKRegistResult;

/** A short Spanish sentence for the result, for the UI and ckctl. */
const char *ck_regist_result_string(CKRegistResult result);

typedef struct ck_regist_outcome_t
{
	CKRegistResult result;
	/** The HTTP status of a refusal, 0 when there was none. */
	int http_code;
	/** The RP-Application-Reason of a refusal, 0 when there was none. */
	uint32_t reason;
	/** The registration to save (A2.5); valid only on CK_REGIST_SUCCESS. */
	ChiakiRegisteredHost registered_host;
} CKRegistOutcome;

typedef void (*CKRegistCb)(const CKRegistOutcome *outcome, void *user);

typedef struct ck_regist_t CKRegist;

/**
 * Registers with the console in the background. cb is called exactly once,
 * on the core's registration thread, also after a cancel. It takes up to
 * about 6 seconds: the core waits 3 for the search and 3 for the answer.
 * log must outlive the registration.
 */
ChiakiErrorCode ck_regist_start(CKRegist **regist, ChiakiLog *log, const CKRegistRequest *request,
	CKRegistCb cb, void *user);

/** Asks a running registration to stop; cb then reports CK_REGIST_CANCELED. */
void ck_regist_cancel(CKRegist *regist);

/** Waits for the registration's thread and frees it. Not from cb. NULL is fine. */
void ck_regist_free(CKRegist *regist);

/** ck_regist_start() and waiting for it, for ckctl. Returns outcome->result. */
CKRegistResult ck_regist_run(ChiakiLog *log, const CKRegistRequest *request, CKRegistOutcome *outcome);

// ---------------------------------------------------------------------------
// Wake-up
// ---------------------------------------------------------------------------

/**
 * The wake-up credential: rp_regist_key holds up to CHIAKI_SESSION_AUTH_SIZE
 * hex digits as text, NUL-padded. Fails on an empty or non-hex key.
 */
ChiakiErrorCode ck_regist_key_credential(const char rp_regist_key[CHIAKI_SESSION_AUTH_SIZE], uint64_t *credential);

/**
 * Sends the wake-up packet to a console in rest mode. It only sends: the
 * console takes 10 to 20 seconds to turn on, which ck_consoles_discover()
 * with host shows as CHIAKI_DISCOVERY_HOST_STATE_READY.
 */
ChiakiErrorCode ck_console_wakeup(ChiakiLog *log, const char *host, const ChiakiRegisteredHost *registered_host);

#ifdef __cplusplus
}
#endif

#endif // CK_CONSOLES_H
