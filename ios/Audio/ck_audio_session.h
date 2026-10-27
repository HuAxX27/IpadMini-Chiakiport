// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A4.2: the process's audio session (AVAudioSession), in C for the audio
// player and the ckaudio tool. The player sets it up and the app leaves it
// alone (agreed with A6). Not a contract.
//
// Category Playback: the sound plays with the silent switch on and stops
// other apps' sound. The session is one per process, so only one user at a
// time: the player while a stream lasts, or ckaudio.

#ifndef CK_AUDIO_SESSION_H
#define CK_AUDIO_SESSION_H

#include <chiaki/log.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ck_audio_session_event_t
{
	/** Another app, an alarm or Siri took the audio: iOS already paused the queue. */
	CK_AUDIO_SESSION_INTERRUPTION_BEGAN,
	/** The interruption is over and the session is active again: start the queue again. */
	CK_AUDIO_SESSION_INTERRUPTION_ENDED,
	/** The headphones were plugged in or out, or the output changed; the queue follows on its own. */
	CK_AUDIO_SESSION_ROUTE_CHANGED,
	/** The audio server restarted: every queue is gone and has to be made again. */
	CK_AUDIO_SESSION_MEDIA_RESET,
} CKAudioSessionEvent;

/** Runs on whatever thread iOS posts from; it must hop to its own thread before touching a queue. */
typedef void (*CKAudioSessionCallback)(CKAudioSessionEvent event, void *user);

typedef struct ck_audio_session_info_t
{
	/** What the hardware runs at; AudioQueue converts 48 kHz to it. */
	double sample_rate;
	/** How much audio the hardware asks for at a time, in seconds. */
	double io_buffer_s;
	/** From the hardware's buffer to the ear, in seconds. */
	double output_latency_s;
	/** The output port, such as "Speaker" or "Headphones". */
	char route[48];
} CKAudioSessionInfo;

/**
 * Sets the category to Playback, asks for preferred_rate and a hardware
 * buffer of preferred_io_buffer_s (either 0 to keep iOS's) and activates the
 * session. callback may be NULL. Returns false, after logging why, when iOS
 * refuses the category or the activation; a refused preference is only
 * logged.
 */
bool ck_audio_session_begin(ChiakiLog *log, double preferred_rate, double preferred_io_buffer_s,
	CKAudioSessionCallback callback, void *user);

/** Stops listening and deactivates the session, so other apps' sound comes back. */
void ck_audio_session_end(void);

void ck_audio_session_get_info(CKAudioSessionInfo *info);

#ifdef __cplusplus
}
#endif

#endif // CK_AUDIO_SESSION_H
