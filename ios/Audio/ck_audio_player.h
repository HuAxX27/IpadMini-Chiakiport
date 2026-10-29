// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A4.3: more about what the audio player (CKAudioSink.h, contract C3) did
// than ck_audio_player_take_stats() gives, for the ckaudio tool and A7. Not a
// contract.

#ifndef CK_AUDIO_PLAYER_H
#define CK_AUDIO_PLAYER_H

#include "CKAudioSink.h"
#include "ck_audio_buffer.h"
#include "ck_audio_output.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The format the player always plays: the Opus decoder makes it from any stream. */
#define CK_AUDIO_PLAYER_RATE 48000
#define CK_AUDIO_PLAYER_CHANNELS 2
/**
 * C3's buffer: 40 to 80 ms, 50 on average. When the output's thread comes
 * late, the buffer fills up by as much and the output empties by as much, so
 * the sound is no later: the cap leaves room for the 60 ms an AudioQueue of 4
 * buffers of 20 ms takes (below) and one frame, and only a burst from the
 * network goes over it.
 */
#define CK_AUDIO_PLAYER_TARGET_MS 50
#define CK_AUDIO_PLAYER_MAX_MS 120

/**
 * The output: RemoteIO, whose callback runs on the audio system's real-time
 * thread. AudioQueue's thread is not real-time: while the iPad rotated its
 * cuts were heard with 3 buffers of 10 ms and with 3 of 40 ms (2026-10-27).
 */
#define CK_AUDIO_PLAYER_OUTPUT CK_AUDIO_OUTPUT_REMOTEIO
/**
 * With AudioQueue (ckaudio -o aq): 4 buffers of 20 ms. Pulls of 20 ms leave
 * the buffer 25 ms for the network's jitter, where pulls of 40 ms would leave
 * it 5; longer buffers are refused.
 */
#define CK_AUDIO_PLAYER_OUTPUT_BUFFER_MS 20
#define CK_AUDIO_PLAYER_OUTPUT_BUFFERS 4
#define CK_AUDIO_PLAYER_OUTPUT_BUFFER_MS_MIN 5
#define CK_AUDIO_PLAYER_OUTPUT_BUFFER_MS_MAX 25

typedef struct ck_audio_player_details_t
{
	/** Frames decoded, lost on the network and refused by libopus (both concealed), since the previous call. */
	uint32_t decoded;
	uint32_t lost;
	uint32_t refused;
	/** The jitter buffer: frames dropped, frames made up for the clock (requested), running dry, level. */
	CKAudioBufferStats buffer;
	/** The output. */
	CKAudioOutputStats output;
	/** Times the output was made again after the audio server restarted, since the previous call. */
	uint32_t output_resets;
} CKAudioPlayerDetails;

/**
 * ck_audio_player_new() with another output, for ckaudio to compare them on
 * the iPad. With AudioQueue, buffers of CK_AUDIO_PLAYER_OUTPUT_BUFFER_MS_MIN
 * to _MAX ms, 2 to CK_AUDIO_OUTPUT_BUFFERS_MAX of them, NULL for other
 * sizes; RemoteIO takes no sizes, the two are not looked at.
 */
CKAudioPlayer *ck_audio_player_new_output(ChiakiLog *log, CKAudioOutputKind kind, uint32_t buffer_ms, uint32_t buffer_count);

/**
 * Like ck_audio_player_take_stats() and resets the same counts: a user calls
 * one or the other. Any thread.
 */
void ck_audio_player_take_details(CKAudioPlayer *player, CKAudioPlayerDetails *details);

#ifdef __cplusplus
}
#endif

#endif // CK_AUDIO_PLAYER_H
