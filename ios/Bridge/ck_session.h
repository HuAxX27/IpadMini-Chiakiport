// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A2.6: the plain C half of CKSession (contract C1, CKSession.h). It owns the
// ChiakiSession and everything around it that can be tested on Linux: the
// connect info from the settings, the session's state, the input thread that
// merges the sources of contract C4, and the counters behind CKMetrics.
// CKSession.m only adds Objective-C and the hop to the main thread. Tested in
// ios/tests/core/session.c against the fake PS5.
//
// Callbacks run on core threads and must not call back into this API.

#ifndef CK_SESSION_H
#define CK_SESSION_H

#include <chiaki/audioreceiver.h>
#include <chiaki/log.h>
#include <chiaki/regist.h>
#include <chiaki/session.h>

#include "CKSettings.h"
#include "../Input/CKInputSource.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Input sources one session polls at most: the DualSense, the touch overlay and spares. */
#define CK_SESSION_INPUT_SOURCES_MAX 4

/** The same values, in the same order, as CKSessionState in CKSession.h. */
typedef enum ck_session_core_state_t
{
	CK_SESSION_CORE_IDLE,
	CK_SESSION_CORE_CONNECTING,
	CK_SESSION_CORE_STREAMING,
	CK_SESSION_CORE_STOPPED,
} CKSessionCoreState;

typedef struct ck_session_callbacks_t
{
	/** Every change of state. */
	void (*state)(CKSessionCoreState state, void *user);
	/**
	 * The stream ended, once per start, right before the state becomes
	 * CK_SESSION_CORE_STOPPED. reason_str is the console's text or NULL, and
	 * only valid during the call.
	 */
	void (*quit)(ChiakiQuitReason reason, const char *reason_str, void *user);
	void *user;
} CKSessionCallbacks;

/** What went through the session since the previous ck_session_take_stats(). */
typedef struct ck_session_stats_t
{
	/** Video frames handed to the video callback, and how many it took. */
	uint32_t video_frames;
	uint32_t video_frames_accepted;
	/** Frames the core reported lost before the ones it handed over. */
	uint32_t video_frames_lost;
	uint64_t video_bytes;
	/** Time spent inside the video callback, in microseconds. */
	uint64_t video_callback_us;
	/** Opus frames handed to the audio sink; lost ones come as NULL. */
	uint32_t audio_frames;
	uint32_t audio_frames_lost;
	uint64_t audio_bytes;
	/** Microseconds since the previous call, or since the session started. */
	uint64_t elapsed_us;
} CKSessionStats;

typedef struct ck_session_t CKSessionCore;

/**
 * The stream the settings ask for: always H.264, the resolution and frame
 * rate of the settings, and their bitrate when set.
 */
void ck_session_video_profile(const CKSettings *settings, ChiakiConnectVideoProfile *profile);

/**
 * A session with the console at host (copied), using a saved registration
 * (copied). log must outlive the session. Returns NULL without memory.
 */
CKSessionCore *ck_session_new(ChiakiLog *log, const char *host, const ChiakiRegisteredHost *registered_host,
	const CKSettings *settings, const CKSessionCallbacks *callbacks);

/** Stops the session if it runs, and frees it. NULL is fine. */
void ck_session_free(CKSessionCore *session);

/** Set before ck_session_start(). cb has the signature and the rules of contract C2. */
void ck_session_set_video_cb(CKSessionCore *session, ChiakiVideoSampleCallback cb, void *user);

/** Set before ck_session_start(). The sink is copied (contract C3). */
void ck_session_set_audio_sink(CKSessionCore *session, const ChiakiAudioSink *sink);

/**
 * Adds a source of contract C4, polled every CK_INPUT_POLL_INTERVAL_MS while
 * the session runs. Any time, from any thread but a source's own poll.
 * Fails with CHIAKI_ERR_OVERFLOW past CK_SESSION_INPUT_SOURCES_MAX.
 */
ChiakiErrorCode ck_session_add_input_source(CKSessionCore *session, const CKInputSource *source);
void ck_session_remove_all_input_sources(CKSessionCore *session);

/**
 * Connects in the background: the state goes to CONNECTING, to STREAMING
 * when the video starts and to STOPPED when it ends. A stopped session can
 * be started again, also one the console ended without ck_session_stop().
 * Fails with CHIAKI_ERR_INVALID_DATA on a bad registration or host, and
 * while the session is connecting or streaming.
 */
ChiakiErrorCode ck_session_start(CKSessionCore *session);

/**
 * Ends the stream and returns once the core's threads have joined; the state
 * is then STOPPED. Safe to call more than once, before ck_session_start() and
 * after the console ended the stream. Not from a callback.
 */
void ck_session_stop(CKSessionCore *session);

CKSessionCoreState ck_session_state(CKSessionCore *session);

/** The counters since the previous call, then resets them. */
void ck_session_take_stats(CKSessionCore *session, CKSessionStats *stats);

/**
 * Merges the sources as contract C4 says, with chiaki_controller_state_or().
 * Returns whether any of them reported; *state is idle when none did.
 */
bool ck_input_merge(const CKInputSource *sources, size_t count, ChiakiControllerState *state);

/** The process's resident memory in KB, for CKMetrics; 0 when unknown. */
uint32_t ck_resident_kb(void);

#ifdef __cplusplus
}
#endif

#endif // CK_SESSION_H
