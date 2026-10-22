// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A fake PS5 on 127.0.0.1 for the Linux tests of Bridge/CKConsoles.h and
// ckctl (ios/tests/core/consoles.c). It answers what a real PS5 answers:
//
//   UDP 9302   discovery: SRCH gets the console's state, WAKEUP with the
//              right credential turns it on.
//   UDP 9295   registration search: SRC3 gets RES3.
//   TCP 9295   registration: decrypts the request with the PIN, checks the
//              PSN Account ID and answers with the console's keys, encrypted
//              like a PS5 does, or with the refusal a PS5 sends.
//
// It uses the core's own rpcrypt, so a registration that works here works
// the same way up to the bytes on the wire. Every value in it is made up.

#ifndef CK_FAKE_PS5_H
#define CK_FAKE_PS5_H

#include <chiaki/discovery.h>
#include <chiaki/regist.h>

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#define FAKE_PS5_NAME "PS5-Falsa"
#define FAKE_PS5_HOST_ID "0A1B2C3D4E5F"
#define FAKE_PS5_SYSTEM_VERSION "09060000"
/** The registration key as the PS5 keeps it: 8 hex digits as text. */
#define FAKE_PS5_REGIST_KEY "1a2b3c4d"
#define FAKE_PS5_CREDENTIAL UINT64_C(0x1a2b3c4d)
/** rp_key: 16 bytes of 0xf0, low entropy so no secret scanner mistakes it for a key. */
#define FAKE_PS5_RP_KEY_BYTE 0xf0
#define FAKE_PS5_RP_KEY_HEX "f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0"

typedef enum fake_ps5_regist_mode_t
{
	/** Answer like a PS5 with the PIN on screen. */
	FAKE_PS5_REGIST_OK,
	/** Do not answer the registration search. */
	FAKE_PS5_REGIST_SILENT,
	/** Answer the search, but nobody listens on TCP 9295. */
	FAKE_PS5_REGIST_NO_TCP,
	/** Accept the request and close without answering. */
	FAKE_PS5_REGIST_HANG_UP,
	/** Answer 200 without the registration key. */
	FAKE_PS5_REGIST_NO_KEY,
	/** Answer 500 with no application reason. */
	FAKE_PS5_REGIST_SERVER_ERROR,
} FakePs5RegistMode;

typedef struct fake_ps5_t
{
	// Set before fake_ps5_start().
	uint32_t pin;
	uint8_t psn_account_id[CHIAKI_PSN_ACCOUNT_ID_SIZE];
	ChiakiDiscoveryHostState state;
	bool discovery_silent;
	FakePs5RegistMode regist_mode;

	// Read with the mutex held while it runs.
	pthread_mutex_t mutex;
	int searches;
	int wakeups;
	uint64_t last_credential;
	int regist_requests;
	/** The PSN Account ID of the last request, as the request carried it. */
	char last_account_b64[32];

	int udp_discovery;
	int udp_regist;
	int tcp_regist;
	int stop_pipe[2];
	pthread_t thread;
} FakePs5;

/** Defaults: PIN 12345678, a made-up account, in rest mode, everything answered. */
void fake_ps5_init(FakePs5 *ps5);
/** Binds the ports on 127.0.0.1 and starts answering. Returns 0, or -1 (errno set). */
int fake_ps5_start(FakePs5 *ps5);
void fake_ps5_stop(FakePs5 *ps5);

ChiakiDiscoveryHostState fake_ps5_state(FakePs5 *ps5);
int fake_ps5_wakeups(FakePs5 *ps5);

#endif // CK_FAKE_PS5_H
