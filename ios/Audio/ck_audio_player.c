// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A4.3: the audio player of contract C3 (CKAudioSink.h): the Opus decoder
// (A4.1), the jitter buffer (ck_audio_buffer.h) and the RemoteIO output
// with its audio session (A4.2). Portable C: on the iPad it runs on
// ck_audio_output_ios.c and ck_audio_session.m; the Linux tests
// (ios/tests/audio/player_test.c) give it a fake output and session and play
// both threads' part by hand.
//
// Threads: the core's audio thread calls header_cb and frame_cb, one at a
// time under the audio receiver's mutex; it decodes and writes to the buffer.
// The output's thread reads the buffer. The session's events come on
// whatever thread iOS posts them from and may make the output again, so the
// output is only touched under output_mutex. new and free run on the main
// thread, free once the session no longer calls the sink.

#include "ck_audio_player.h"
#include "ck_audio_decoder.h"
#include "ck_audio_session.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/** The hardware buffer asked of iOS, as ckaudio tono asked for it in P2-5. */
#define SESSION_IO_MS 10
/**
 * Frames decoded after a concealed one before the clock correction may make
 * one up: Opus concealment right after concealing clicks on some sounds.
 */
#define MAKE_UP_AFTER_FRAMES 4

struct ck_audio_player_t
{
	ChiakiLog *log;
	CKAudioBuffer *buffer;
	bool session;
	CKAudioOutputKind output_kind;
	uint32_t output_frames;
	uint32_t output_count;

	pthread_mutex_t output_mutex;
	CKAudioOutput *output;
	/** free started: a session event no longer makes an output. */
	bool closing;

	// The core's audio thread's.
	CKAudioDecoder decoder;
	/** Frames decoded since the last one concealed, up to MAKE_UP_AFTER_FRAMES. */
	uint32_t since_concealed;
	int16_t pcm[CK_AUDIO_DECODER_MAX_FRAMES * CK_AUDIO_DECODER_MAX_CHANNELS];

	atomic_uint decoded;
	atomic_uint lost;
	atomic_uint refused;
	atomic_uint output_resets;
};

/** Makes the output and starts it; until the first frames come it plays silence. Under output_mutex. */
static CKAudioOutput *make_output(CKAudioPlayer *player)
{
	CKAudioOutputConfig config;
	config.kind = player->output_kind;
	config.rate = CK_AUDIO_PLAYER_RATE;
	config.channels = CK_AUDIO_PLAYER_CHANNELS;
	config.buffer_frames = player->output_frames;
	config.buffer_count = player->output_count;
	CKAudioOutput *output = ck_audio_output_new(player->log, &config, ck_audio_buffer_pull, player->buffer);
	if(output && !ck_audio_output_start(output))
	{
		ck_audio_output_free(output);
		output = NULL;
	}
	return output;
}

static void session_event(CKAudioSessionEvent event, void *user)
{
	CKAudioPlayer *player = user;
	pthread_mutex_lock(&player->output_mutex);
	// iOS pauses the queue when an interruption begins, and moves it to a new route, on its own.
	if(!player->closing && event == CK_AUDIO_SESSION_INTERRUPTION_ENDED && player->output)
	{
		if(!ck_audio_output_start(player->output))
		{
			// The queue did not take up where it was: start it over.
			ck_audio_output_stop(player->output);
			if(!ck_audio_output_start(player->output))
				CHIAKI_LOGE(player->log, "Audio player: the output did not start again after the interruption");
		}
	}
	else if(!player->closing && event == CK_AUDIO_SESSION_MEDIA_RESET)
	{
		// Every queue is gone; the session set itself up again before telling.
		ck_audio_output_free(player->output);
		player->output = make_output(player);
		atomic_fetch_add_explicit(&player->output_resets, 1, memory_order_relaxed);
		CHIAKI_LOGW(player->log, "Audio player: the audio server restarted, output made again: %s",
			player->output ? "ok" : "failed");
	}
	pthread_mutex_unlock(&player->output_mutex);
}

CKAudioPlayer *ck_audio_player_new(ChiakiLog *log)
{
	return ck_audio_player_new_output(log, CK_AUDIO_PLAYER_OUTPUT, CK_AUDIO_PLAYER_OUTPUT_BUFFER_MS,
		CK_AUDIO_PLAYER_OUTPUT_BUFFERS);
}

CKAudioPlayer *ck_audio_player_new_output(ChiakiLog *log, CKAudioOutputKind kind, uint32_t buffer_ms, uint32_t buffer_count)
{
	if(kind == CK_AUDIO_OUTPUT_REMOTEIO)
	{
		// What a callback asks for once the session has its 10 ms: only the Linux tests' fake output uses it.
		buffer_ms = SESSION_IO_MS;
		buffer_count = 2;
	}
	else if(kind != CK_AUDIO_OUTPUT_AUDIOQUEUE || buffer_ms < CK_AUDIO_PLAYER_OUTPUT_BUFFER_MS_MIN
		|| buffer_ms > CK_AUDIO_PLAYER_OUTPUT_BUFFER_MS_MAX || buffer_count < 2 || buffer_count > CK_AUDIO_OUTPUT_BUFFERS_MAX)
	{
		CHIAKI_LOGE(log, "Audio player: refused an output of kind %d, %u buffers of %u ms", (int)kind, buffer_count, buffer_ms);
		return NULL;
	}
	CKAudioPlayer *player = calloc(1, sizeof(*player));
	if(!player)
		return NULL;
	player->log = log;
	player->output_kind = kind;
	player->output_frames = CK_AUDIO_PLAYER_RATE * buffer_ms / 1000;
	player->output_count = buffer_count;
	atomic_init(&player->decoded, 0);
	atomic_init(&player->lost, 0);
	atomic_init(&player->refused, 0);
	atomic_init(&player->output_resets, 0);
	if(pthread_mutex_init(&player->output_mutex, NULL) != 0)
	{
		free(player);
		return NULL;
	}

	CKAudioBufferConfig config;
	config.rate = CK_AUDIO_PLAYER_RATE;
	config.channels = CK_AUDIO_PLAYER_CHANNELS;
	config.target_ms = CK_AUDIO_PLAYER_TARGET_MS;
	config.max_ms = CK_AUDIO_PLAYER_MAX_MS;
	if(!ck_audio_decoder_init(&player->decoder, log) || !(player->buffer = ck_audio_buffer_new(log, &config)))
	{
		ck_audio_player_free(player);
		return NULL;
	}

	// Without the session the queue still plays, in iOS's default category (quiet with the silent switch).
	player->session = ck_audio_session_begin(log, CK_AUDIO_PLAYER_RATE, SESSION_IO_MS / 1000.0, session_event, player);
	if(!player->session)
		CHIAKI_LOGW(log, "Audio player: playing without the audio session");
	pthread_mutex_lock(&player->output_mutex);
	// A session event may have made it already.
	if(!player->output)
		player->output = make_output(player);
	bool ok = player->output != NULL;
	pthread_mutex_unlock(&player->output_mutex);
	if(!ok)
	{
		CHIAKI_LOGE(log, "Audio player: no audio output");
		ck_audio_player_free(player);
		return NULL;
	}
	return player;
}

void ck_audio_player_free(CKAudioPlayer *player)
{
	if(!player)
		return;
	// The output stops before the session goes inactive, which iOS refuses while a queue plays.
	pthread_mutex_lock(&player->output_mutex);
	player->closing = true;
	ck_audio_output_free(player->output);
	player->output = NULL;
	pthread_mutex_unlock(&player->output_mutex);
	// Returns once no session event runs or will.
	if(player->session)
		ck_audio_session_end();
	pthread_mutex_destroy(&player->output_mutex);
	ck_audio_buffer_free(player->buffer);
	ck_audio_decoder_fini(&player->decoder);
	free(player);
}

static void header_cb(ChiakiAudioHeader *header, void *user)
{
	CKAudioPlayer *player = user;
	uint32_t rate = header->rate;
	CHIAKI_LOGI(player->log, "Audio player: stream of %u channels at %u Hz in frames of %u samples",
		(unsigned)header->channels, rate, header->frame_size);
	bool rate_ok = rate == 8000 || rate == 12000 || rate == 16000 || rate == 24000 || rate == 48000;
	if(!rate_ok || header->channels < 1 || header->channels > 2)
	{
		CHIAKI_LOGE(player->log, "Audio player: not an Opus stream it can play, no sound");
		player->decoder.ready = false;
	}
	else
	{
		// libopus decodes any stream to 48 kHz stereo, so the output never changes; a frame lasts as long.
		uint32_t frame = (uint32_t)((uint64_t)header->frame_size * CK_AUDIO_PLAYER_RATE / rate);
		ck_audio_decoder_header(&player->decoder, CK_AUDIO_PLAYER_CHANNELS, CK_AUDIO_PLAYER_RATE, frame);
		player->since_concealed = 0;
	}
	// Whatever was buffered belongs to the stream before.
	ck_audio_buffer_flush(player->buffer);
}

static void decode_and_push(CKAudioPlayer *player, const uint8_t *buf, size_t buf_size)
{
	uint32_t frames = ck_audio_decoder_frame(&player->decoder, buf, buf_size, player->pcm, CK_AUDIO_DECODER_MAX_FRAMES);
	if(frames)
		ck_audio_buffer_push(player->buffer, player->pcm, frames);
}

static void frame_cb(uint8_t *buf, size_t buf_size, void *user)
{
	CKAudioPlayer *player = user;
	CKAudioDecoder *decoder = &player->decoder;
	if(!decoder->ready)
		return;
	// The clock correction wants one frame more: Opus concealment makes one that follows from the last.
	if(player->since_concealed >= MAKE_UP_AFTER_FRAMES && ck_audio_buffer_take_request(player->buffer))
		decode_and_push(player, NULL, 0);
	CKAudioDecoderStats before = decoder->stats;
	decode_and_push(player, buf, buf_size);
	uint32_t lost = decoder->stats.concealed - before.concealed;
	uint32_t refused = decoder->stats.errors - before.errors;
	if(lost || refused)
		player->since_concealed = 0;
	else if(player->since_concealed < MAKE_UP_AFTER_FRAMES)
		player->since_concealed++;
	atomic_fetch_add_explicit(&player->decoded, decoder->stats.decoded - before.decoded, memory_order_relaxed);
	atomic_fetch_add_explicit(&player->lost, lost, memory_order_relaxed);
	atomic_fetch_add_explicit(&player->refused, refused, memory_order_relaxed);
}

void ck_audio_player_get_sink(CKAudioPlayer *player, ChiakiAudioSink *sink_out)
{
	memset(sink_out, 0, sizeof(*sink_out));
	sink_out->user = player;
	sink_out->header_cb = header_cb;
	sink_out->frame_cb = frame_cb;
}

void ck_audio_player_take_details(CKAudioPlayer *player, CKAudioPlayerDetails *details)
{
	memset(details, 0, sizeof(*details));
	details->decoded = atomic_exchange(&player->decoded, 0);
	details->lost = atomic_exchange(&player->lost, 0);
	details->refused = atomic_exchange(&player->refused, 0);
	details->output_resets = atomic_exchange(&player->output_resets, 0);
	ck_audio_buffer_take_stats(player->buffer, &details->buffer);
	pthread_mutex_lock(&player->output_mutex);
	if(player->output)
		ck_audio_output_take_stats(player->output, &details->output);
	pthread_mutex_unlock(&player->output_mutex);
}

void ck_audio_player_take_stats(CKAudioPlayer *player, uint32_t *frames_concealed, uint32_t *buffered_ms)
{
	CKAudioPlayerDetails details;
	ck_audio_player_take_details(player, &details);
	if(frames_concealed)
		*frames_concealed = details.lost + details.refused;
	if(buffered_ms)
		*buffered_ms = details.buffer.level_ms;
}
