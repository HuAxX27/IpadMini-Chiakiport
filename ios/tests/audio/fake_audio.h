// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A4.3: an AudioQueue output and an audio session for Linux, so the tests run
// the audio player (ios/Audio/ck_audio_player.c) as it is. They implement
// ck_audio_output.h and ck_audio_session.h; the test plays AudioQueue's part
// by pulling the output by hand, and iOS's by sending session events.

#ifndef CK_FAKE_AUDIO_H
#define CK_FAKE_AUDIO_H

#include "Audio/ck_audio_output.h"
#include "Audio/ck_audio_session.h"

#include <stdbool.h>
#include <stdint.h>

/** Outputs made so far, and how many are not freed. */
int fake_outputs_made(void);
int fake_outputs_alive(void);

/** Pulls one buffer from the newest output, as AudioQueue would. False when there is none or it is stopped. */
bool fake_output_render(int16_t *pcm, uint32_t frames);

/** The newest output's config. */
CKAudioOutputConfig fake_output_config(void);

/** Times ck_audio_output_start() was called on the newest output. */
int fake_output_starts(void);

/** Whether the session is active, and sends it an event as iOS would. */
bool fake_session_active(void);
void fake_session_send(CKAudioSessionEvent event);

#endif // CK_FAKE_AUDIO_H
