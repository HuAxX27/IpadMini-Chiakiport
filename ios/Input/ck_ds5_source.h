// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// The DualSense connected straight to the iPad, as an input source for
// CKSession (contract C4, A5.8). ckbtfilter, inside BTServer, copies the pad's
// input reports into a mapped file (ck_ds5_shared.h). This source maps that
// file read-only and, on every poll, parses the newest report with ck_ds5.
//
// Plain POSIX C, tested on Linux. A poll never allocates. It makes no system
// call either, except one stat() every CK_DS5_SOURCE_CHECK_POLLS polls, to
// find the file or notice that a new one replaced it, and the open() and
// mmap() when that happens.

#ifndef CK_DS5_SOURCE_H
#define CK_DS5_SOURCE_H

#include "CKInputSource.h"
#include "ck_ds5.h"
#include "ck_ds5_shared.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CK_DS5_SOURCE_NAME "ds5-direct"

/** Polls between two looks at the file: one second at CK_INPUT_POLL_INTERVAL_MS. */
#define CK_DS5_SOURCE_CHECK_POLLS (1000 / CK_INPUT_POLL_INTERVAL_MS)

typedef struct ck_ds5_source_t
{
	const char *path;
	/** The mapped file, or NULL while there is none. */
	const CKDS5Shared *shared;
	uint64_t dev, ino;
	unsigned polls_to_check;
	uint32_t seq;
	/**
	 * A pad sent a report it understood, and has not gone since. Written by
	 * the poll; other threads read it with ck_ds5_source_connected().
	 */
	_Atomic bool connected;
	ChiakiControllerState state;
	uint8_t report[CK_DS5_SHARED_REPORT_MAX];
	/** The newest report's kind, and the reports parsed since init by kind. For the probe and the logs. */
	CKDS5ReportKind kind;
	uint32_t kinds[CK_DS5_REPORT_BT_FULL + 1];
	/** Files mapped since init: more than one means BTServer made a new one. */
	uint32_t maps;
} CKDS5Source;

/** path is normally CK_DS5_SHARED_PATH; it must outlive the source. Nothing is opened yet. */
void ck_ds5_source_init(CKDS5Source *source, const char *path);

/** Unmaps the file. The source can be polled again afterwards. */
void ck_ds5_source_fini(CKDS5Source *source);

/**
 * The poll function of contract C4. Returns false while no pad is connected:
 * no file yet, BTServer said the pad left, or no report it understood since.
 * Between reports it returns the last state, so a held button stays held.
 */
bool ck_ds5_source_poll(void *user, ChiakiControllerState *state);

/**
 * Whether a pad is connected, as of the last poll. Safe from any thread, for
 * example to hide the touch controls on the main thread while the DualSense
 * reports.
 */
bool ck_ds5_source_connected(const CKDS5Source *source);

/** The source as CKSession takes it. */
CKInputSource ck_ds5_source_input(CKDS5Source *source);

#ifdef __cplusplus
}
#endif

#endif // CK_DS5_SOURCE_H
