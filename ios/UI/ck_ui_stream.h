// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A6.5: what the game screen says and decides, apart from UIKit, so it is
// tested on Linux: why a session ended, when to ask the console again, and
// the metrics line.

#ifndef CK_UI_STREAM_H
#define CK_UI_STREAM_H

#include "Bridge/CKSettings.h"

#include <chiaki/session.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * After a session closes, TheHuAx's PS5 refuses a new one for 10 to 20 s:
 * sometimes first by not taking the control connection, then with "Remote is
 * already in use" (A2 with cksession, A3 with CK Video). The screen asks
 * again every CK_UI_SESSION_RETRY_SECONDS, up to CK_UI_SESSION_RETRY_MAX
 * times: a window of 30 s.
 */
#define CK_UI_SESSION_RETRY_SECONDS 2
#define CK_UI_SESSION_RETRY_MAX 15

/** A failed control connection this soon after the previous session ended counts as the console still closing it. */
#define CK_UI_SESSION_BUSY_SECONDS 30

/**
 * Whether to start the session again after it ended with reason, before any
 * video came (streamed false) and while retries, the ones already made, is
 * below CK_UI_SESSION_RETRY_MAX: on "already in use", and on a failed control
 * connection while the console may still be closing the previous session,
 * that is, since_last_session seconds after one ended or once retrying.
 * since_last_session is negative when no session has ended yet.
 */
bool ck_ui_session_should_retry(ChiakiQuitReason reason, bool streamed, unsigned retries, double since_last_session);

/** A Spanish sentence for why the session ended, and what to do about it. */
const char *ck_ui_quit_reason_string(ChiakiQuitReason reason);

/** Room for any ck_ui_metrics_line(). */
#define CK_UI_METRICS_LINE_SIZE 192

/**
 * The metrics overlay's line, e.g. "29,8 fps · decodifica 4,2 ms · 1,9 Mbps ·
 * 0 cuadros perdidos · audio 0 ocultados, 80 ms · 46 MB de RAM". Any metrics
 * fit in CK_UI_METRICS_LINE_SIZE; a shorter out gets the line cut, maybe
 * inside a character. Always NUL-terminated.
 */
void ck_ui_metrics_line(const CKMetrics *metrics, char *out, size_t size);

#ifdef __cplusplus
}
#endif

#endif // CK_UI_STREAM_H
