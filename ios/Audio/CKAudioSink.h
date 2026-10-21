// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Contract C3 (A2 <-> A4): Opus out of the core, PCM into the iPad's speaker.
// Owner: A4. A change to this file goes in its own PR, approved by A0.

#ifndef CK_AUDIO_SINK_H
#define CK_AUDIO_SINK_H

#include <chiaki/audioreceiver.h>
#include <chiaki/log.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ck_audio_player_t CKAudioPlayer;

CKAudioPlayer *ck_audio_player_new(ChiakiLog *log);
void ck_audio_player_free(CKAudioPlayer *player);

/**
 * Fills a core ChiakiAudioSink that feeds this player; hand it to
 * chiaki_session_set_audio_sink().
 *
 * header_cb runs once per stream with the channel count and sample rate.
 * frame_cb runs once per Opus frame in playback order; buf == NULL with
 * buf_size == 0 is a frame lost on the network, which the player conceals
 * with Opus packet-loss concealment. Both run on the core's audio thread and
 * must not block on the audio output.
 */
void ck_audio_player_get_sink(CKAudioPlayer *player, ChiakiAudioSink *sink_out);

/** Frames concealed since the previous call and current buffer depth, for CKMetrics. */
void ck_audio_player_take_stats(CKAudioPlayer *player, uint32_t *frames_concealed, uint32_t *buffered_ms);

#ifdef __cplusplus
}
#endif

#endif // CK_AUDIO_SINK_H
