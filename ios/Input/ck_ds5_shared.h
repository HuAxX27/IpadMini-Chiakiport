// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// The latest DualSense input report, shared between processes through a mapped
// file (A5.7). On the iPad, BTServer receives the pad's reports but does not
// publish the pad as an IOHIDDevice, so the BTServer tweak (ckbtfilter) copies
// each report here, and ckhidprobe, and later the app (A5.8), read it on their
// own schedule. A sequence lock keeps a reader from ever seeing half a report
// without making the writer wait: BTServer never blocks on its readers.
//
// Plain C with no I/O and no allocation; opening and mapping the file is the
// caller's job. Tested on Linux.

#ifndef CK_DS5_SHARED_H
#define CK_DS5_SHARED_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Written by BTServer, which runs as mobile; any process can read it. */
#define CK_DS5_SHARED_PATH "/tmp/ckds5.report"
#define CK_DS5_SHARED_MAGIC 0x35444b43 // "CKD5"
#define CK_DS5_SHARED_VERSION 1
/** Room for the largest report, the Bluetooth 0x31 one (78 bytes with its ID). */
#define CK_DS5_SHARED_REPORT_MAX 80

typedef struct ck_ds5_shared_t
{
	uint32_t magic;
	uint32_t version;
	/** Even when the report is whole, odd while the writer is changing it. Grows by 2 per report. */
	_Atomic uint32_t seq;
	/** Bytes in report; 0 when no pad is connected. */
	_Atomic uint32_t length;
	/**
	 * The input report as the pad sent it, from the report ID on (no HIDP
	 * header). Atomic bytes so that a reader racing the writer is defined
	 * behaviour; on ARM they are plain loads and stores.
	 */
	_Atomic uint8_t report[CK_DS5_SHARED_REPORT_MAX];
} CKDS5Shared;

/**
 * Writer, once the file is mapped: sets up a fresh region, or keeps the
 * sequence number of one a previous writer left (so readers keep up across a
 * BTServer restart), and marks it as having no pad.
 */
void ck_ds5_shared_attach(CKDS5Shared *shared);

/** Writer: publishes one report. len 0 (report may be NULL) says the pad is gone. Longer reports are cut. */
void ck_ds5_shared_write(CKDS5Shared *shared, const uint8_t *report, size_t len);

/** True when the region was set up by a writer of this version. */
bool ck_ds5_shared_valid(const CKDS5Shared *shared);

/**
 * Reader: when there is a whole report newer than *seq, copies it to out
 * (CK_DS5_SHARED_REPORT_MAX bytes), sets *len (0 means no pad) and *seq, and
 * returns true. Returns false when there is nothing new, the region is not
 * valid, or the writer was busy on every try; the next call catches up.
 * Start with *seq = 0.
 */
bool ck_ds5_shared_read(const CKDS5Shared *shared, uint32_t *seq, uint8_t *out, size_t *len);

#ifdef __cplusplus
}
#endif

#endif // CK_DS5_SHARED_H
