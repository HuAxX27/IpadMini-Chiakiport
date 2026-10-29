// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A4.1: from the PS5's Opus frames to 16-bit PCM, with libopus. Portable C,
// tested on Linux against ffmpeg with real captures (ios/tests/audio).
//
// The PS5 sends stereo at 48 kHz in 10 ms frames (480 samples). A frame lost
// on the network (contract C3: buf NULL, size 0) is concealed with Opus
// packet-loss concealment, and so is one libopus cannot decode, so every
// frame in gives one frame of sound out and the timing never slips.
//
// Everything is allocated in ck_audio_decoder_init(), for the largest stream
// Opus allows; a header or a frame never allocates. One thread at a time.

#ifndef CK_AUDIO_DECODER_H
#define CK_AUDIO_DECODER_H

#include <chiaki/log.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** RFC 6716: an Opus packet lasts 120 ms at most, 5760 samples at 48 kHz. */
#define CK_AUDIO_DECODER_MAX_FRAMES 5760
#define CK_AUDIO_DECODER_MAX_CHANNELS 2

typedef struct ck_audio_decoder_stats_t
{
	/** Frames decoded from a packet. */
	uint32_t decoded;
	/** Frames lost on the network that were concealed. */
	uint32_t concealed;
	/** Packets libopus refused, concealed instead. */
	uint32_t errors;
} CKAudioDecoderStats;

typedef struct ck_audio_decoder_t
{
	ChiakiLog *log;
	/** libopus's state, sized for stereo, and whether a header set it up. */
	struct OpusDecoder *opus;
	bool ready;
	uint32_t rate;
	uint32_t channels;
	/** Samples per channel in one frame of the stream: what a concealed frame lasts. */
	uint32_t frame_size;
	CKAudioDecoderStats stats;
} CKAudioDecoder;

/** Returns false, after logging why, when the memory is not there. */
bool ck_audio_decoder_init(CKAudioDecoder *decoder, ChiakiLog *log);
void ck_audio_decoder_fini(CKAudioDecoder *decoder);

/**
 * The stream's header (ChiakiAudioHeader). Starts the decoder over, also when
 * the stream is the same, since the frames before it belong to another
 * stream. Returns false, after logging why, for a format Opus does not
 * decode (rates 8, 12, 16, 24 and 48 kHz, one or two channels, frames of
 * 2.5 to 120 ms); frames are then ignored until a good header comes.
 */
bool ck_audio_decoder_header(CKAudioDecoder *decoder, uint32_t channels, uint32_t rate, uint32_t frame_size);

/**
 * Decodes one frame into pcm, interleaved, room for max_frames frames.
 * buf NULL with buf_size 0 is a frame lost on the network. Returns the frames
 * written: the packet's length, or frame_size when it was concealed; 0 before
 * a good header or when max_frames is too small for the packet.
 */
uint32_t ck_audio_decoder_frame(CKAudioDecoder *decoder, const uint8_t *buf, size_t buf_size,
	int16_t *pcm, uint32_t max_frames);

#ifdef __cplusplus
}
#endif

#endif // CK_AUDIO_DECODER_H
