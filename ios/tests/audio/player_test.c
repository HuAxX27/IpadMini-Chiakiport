// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A4.3's test: the audio player of contract C3 (ios/Audio/ck_audio_player.c,
// as it is, on fake_audio.c's output and session) under a simulated network.
//
//   player_test [captura.opus ...]
//
// Packets like the PS5's (or those of each capture given) come in 10 ms apart
// on the PS5's clock, each late by a random amount, some lost, as the core
// hands them over: in order, and for each gap one lost frame (buf NULL) per
// missing frame, five at most. The output pulls what RemoteIO would ask for
// on the iPad's clock: 512 frames of a 44.1 kHz hardware, 557 or 558 at
// 48 kHz. Each scenario checks:
//
//  - no clicks (audio_click.h): no second difference over twice the largest
//    one in the clean decode of the same packets. Opus concealment of a run
//    of lost frames can click by itself, more often the more is lost: the
//    count may never pass that of the same frames decoded one after the
//    other, without the buffer, and up to the 2 % A4.3 asks for it is 0;
//  - the level the output finds stays between 40 and 80 ms, every second;
//  - it runs dry only where the scenario stalls the network;
//  - every lost frame is counted as concealed, and the sound plays (its
//    level is close to the clean decode's).
//
// A naive player (silence for a lost frame, hard drops, no fades) goes through
// the same network to show that the click count would see the difference.
// Run by ios/tests/audio_tests.sh.

#include "Audio/ck_audio_decoder.h"
#include "Audio/ck_audio_player.h"
#include "Audio/ck_ogg_opus.h"
#include "audio_click.h"
#include "fake_audio.h"

#include <opus/opus.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond) do { \
	if(!(cond)) { \
		fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
		failures++; \
	} \
} while(0)

#define CHECK_EQ(a, b) do { \
	long long a_ = (long long)(a), b_ = (long long)(b); \
	if(a_ != b_) { \
		fprintf(stderr, "%s:%d: %s == %lld, expected %s == %lld\n", __FILE__, __LINE__, #a, a_, #b, b_); \
		failures++; \
	} \
} while(0)

#define RATE 48000
#define CH 2
#define FRAME 480
#define PI 3.14159265358979
#define SECONDS 30
/** The 10 ms packets of a run, and its frames. */
#define PACKETS (SECONDS * 100)
#define TOTAL ((size_t)PACKETS * FRAME)

static ChiakiLog test_log;
static int16_t out[TOTAL * CH];
static int16_t clean[TOTAL * CH];
/** The frames as the core handed them over, decoded one after the other with no buffer: what the losses alone sound like. */
static int16_t plain[TOTAL * CH];

/** A stream of Opus packets; a size of 0 is a frame lost in the capture. */
typedef struct packets_t
{
	const char *name;
	uint8_t *data;
	uint32_t *offset;
	uint16_t *size;
	uint32_t count;
} Packets;

static bool packets_alloc(Packets *p, const char *name, uint32_t count, size_t bytes)
{
	p->name = name;
	p->count = 0;
	p->data = malloc(bytes);
	p->offset = malloc(count * sizeof(uint32_t));
	p->size = malloc(count * sizeof(uint16_t));
	return p->data && p->offset && p->size;
}

static void packets_free(Packets *p)
{
	free(p->data);
	free(p->offset);
	free(p->size);
}

/** Like the PS5's (CELT, stereo, 10 ms, 64 kbps constant): two tones that slide and swell, as make_opus.c. */
static bool synthetic(Packets *p)
{
	enum { COUNT = PACKETS + 200 };
	if(!packets_alloc(p, "synthetic", COUNT, (size_t)COUNT * 200))
		return false;
	int err;
	OpusEncoder *enc = opus_encoder_create(RATE, CH, OPUS_APPLICATION_RESTRICTED_LOWDELAY, &err);
	if(err != OPUS_OK)
		return false;
	opus_encoder_ctl(enc, OPUS_SET_BITRATE(64000));
	opus_encoder_ctl(enc, OPUS_SET_VBR(0));
	double phase[CH] = { 0.0, 0.0 };
	size_t at = 0;
	for(uint32_t f = 0; f < COUNT; f++)
	{
		opus_int16 pcm[FRAME * CH];
		for(int i = 0; i < FRAME; i++)
		{
			double t = (double)(f * FRAME + i) / RATE;
			double hz = 600.0 - 300.0 * cos(2.0 * PI * t / 4.0);
			double gain = 6000.0 * (0.6 + 0.4 * sin(2.0 * PI * 2.0 * t));
			for(int c = 0; c < CH; c++)
			{
				phase[c] += 2.0 * PI * hz * (c ? 1.5 : 1.0) / RATE;
				pcm[i * CH + c] = (opus_int16)lrint(gain * sin(phase[c]));
			}
		}
		int size = opus_encode(enc, pcm, FRAME, p->data + at, 200);
		if(size <= 0)
			return false;
		p->offset[f] = (uint32_t)at;
		p->size[f] = (uint16_t)size;
		at += (size_t)size;
		p->count++;
	}
	opus_encoder_destroy(enc);
	return true;
}

/** The packets of a cksession -g capture, as the PS5 sent them. */
static bool capture(Packets *p, const char *path)
{
	FILE *f = fopen(path, "rb");
	if(!f)
		return false;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *file = n > 0 ? malloc((size_t)n) : NULL;
	bool ok = file && fread(file, 1, (size_t)n, f) == (size_t)n;
	fclose(f);
	CKOggOpus ogg;
	const char *bad = ok ? ck_ogg_opus_open(&ogg, file, (size_t)n) : "unreadable";
	if(bad || !packets_alloc(p, path, PACKETS + 200, (size_t)n))
	{
		fprintf(stderr, "player_test: %s: %s\n", path, bad ? bad : "no memory");
		free(file);
		return false;
	}
	const uint8_t *packet;
	size_t size;
	size_t at = 0;
	while(p->count < PACKETS + 200 && ck_ogg_opus_next(&ogg, &packet, &size) == 1)
	{
		bool lost = ck_ogg_opus_packet_is_lost(size);
		memcpy(p->data + at, packet, lost ? 0 : size);
		p->offset[p->count] = (uint32_t)at;
		p->size[p->count] = lost ? 0 : (uint16_t)size;
		at += lost ? 0 : size;
		p->count++;
	}
	free(file);
	if(p->count < PACKETS + 100)
	{
		fprintf(stderr, "player_test: %s lasts less than %d s\n", path, SECONDS + 1);
		return false;
	}
	return true;
}

/** The packets decoded one after the other, nothing lost but what the capture lost. */
static void decode_clean(const Packets *p, int16_t *pcm, uint32_t frames)
{
	CKAudioDecoder decoder;
	CHECK(ck_audio_decoder_init(&decoder, &test_log));
	CHECK(ck_audio_decoder_header(&decoder, CH, RATE, FRAME));
	for(uint32_t f = 0; f < p->count && (f + 1) * FRAME <= frames; f++)
		ck_audio_decoder_frame(&decoder, p->size[f] ? p->data + p->offset[f] : NULL, p->size[f],
			pcm + (size_t)f * FRAME * CH, FRAME);
	ck_audio_decoder_fini(&decoder);
}

static double rms(const int16_t *pcm, size_t samples)
{
	double sum = 0.0;
	for(size_t i = 0; i < samples; i++)
		sum += (double)pcm[i] * pcm[i];
	return sqrt(sum / (double)samples);
}

typedef struct scenario_t
{
	const char *name;
	/** Each frame comes late by a random 0 to jitter_ms. */
	double jitter_ms;
	/** Share of frames lost on the network. */
	double loss;
	/** How much faster the PS5's clock runs than the iPad's, in parts per million. */
	double skew_ppm;
	/** Nothing comes from stall_at for stall_ms; then everything held up comes at once. */
	double stall_at;
	double stall_ms;
	/** So many losses that Opus concealment clicks on its own: compared with decoding without the buffer. */
	bool loss_clicks;
	/** One second of every five, the output's thread comes late by a random 0 to late_ms. */
	double late_ms;
} Scenario;

/** The output the player is made with. */
typedef struct output_size_t
{
	CKAudioOutputKind kind;
	/** AudioQueue's buffers. */
	uint32_t buffer_ms;
	uint32_t buffers;
} OutputSize;

static const OutputSize player_output = { CK_AUDIO_PLAYER_OUTPUT, CK_AUDIO_PLAYER_OUTPUT_BUFFER_MS,
	CK_AUDIO_PLAYER_OUTPUT_BUFFERS };
static const OutputSize audioqueue_output = { CK_AUDIO_OUTPUT_AUDIOQUEUE, CK_AUDIO_PLAYER_OUTPUT_BUFFER_MS,
	CK_AUDIO_PLAYER_OUTPUT_BUFFERS };

/** What the output asks for in pull number n: RemoteIO's 512 frames at 44.1 kHz come to 557.3 at 48 kHz. */
static uint32_t pull_size(const OutputSize *size, uint32_t n)
{
	if(size->kind == CK_AUDIO_OUTPUT_AUDIOQUEUE)
		return RATE * size->buffer_ms / 1000;
	return (uint32_t)((uint64_t)(n + 1) * 512 * RATE / 44100 - (uint64_t)n * 512 * RATE / 44100);
}

typedef struct result_t
{
	uint32_t delivered_lost;
	uint32_t counted_lost;
	uint32_t underruns;
	uint32_t dropped;
	uint32_t requested;
	uint32_t level_min_ms;
	uint32_t level_max_ms;
	uint32_t bad_seconds;
} Result;

/** A small, fixed random sequence (a 32-bit LCG), so every host sees the same network. */
static double next_random(uint32_t *state)
{
	*state = *state * 1664525u + 1013904223u;
	return (double)(*state >> 8) / (double)(1u << 24);
}

/** The naive player: silence for a lost frame, drops without a crossfade, no fades. */
typedef struct naive_t
{
	CKAudioDecoder decoder;
	int16_t ring[RATE * CH];
	uint32_t write;
	uint32_t read;
	bool playing;
} Naive;

static void naive_frame(Naive *n, const uint8_t *buf, size_t size)
{
	int16_t pcm[FRAME * CH];
	if(buf)
		ck_audio_decoder_frame(&n->decoder, buf, size, pcm, FRAME);
	else
		memset(pcm, 0, sizeof(pcm));
	if(n->write - n->read + FRAME > RATE * 80 / 1000)
		return;
	for(uint32_t i = 0; i < FRAME * CH; i++)
		n->ring[(n->write * CH + i) % (RATE * CH)] = pcm[i];
	n->write += FRAME;
}

static void naive_pull(Naive *n, int16_t *pcm, uint32_t frames)
{
	uint32_t avail = n->write - n->read;
	if(!n->playing && avail >= RATE * 50 / 1000)
		n->playing = true;
	if(!n->playing || avail < frames)
	{
		n->playing = false;
		memset(pcm, 0, (size_t)frames * CH * sizeof(int16_t));
		return;
	}
	for(uint32_t i = 0; i < frames * CH; i++)
		pcm[i] = n->ring[(n->read * CH + i) % (RATE * CH)];
	n->read += frames;
}

/**
 * SECONDS of the scenario through the player (or the naive one) into out.
 * The first second and the three after a stall are not held to the level.
 */
static Result run(const Packets *p, const Scenario *s, bool naive, const OutputSize *size)
{
	Result r;
	memset(&r, 0, sizeof(r));
	r.level_min_ms = UINT32_MAX;
	CKAudioPlayer *player = NULL;
	ChiakiAudioSink sink;
	static Naive n;
	if(naive)
	{
		memset(&n, 0, sizeof(n));
		CHECK(ck_audio_decoder_init(&n.decoder, &test_log));
		CHECK(ck_audio_decoder_header(&n.decoder, CH, RATE, FRAME));
	}
	else
	{
		player = ck_audio_player_new_output(&test_log, size->kind, size->buffer_ms, size->buffers);
		CHECK(player != NULL);
		if(!player)
			return r;
		CHECK_EQ(fake_output_config().kind, size->kind);
		ck_audio_player_get_sink(player, &sink);
		ChiakiAudioHeader header = { CH, 16, RATE, FRAME, 0 };
		sink.header_cb(&header, sink.user);
	}

	CKAudioDecoder plain_decoder;
	CHECK(ck_audio_decoder_init(&plain_decoder, &test_log));
	CHECK(ck_audio_decoder_header(&plain_decoder, CH, RATE, FRAME));
	size_t plain_frames = 0;

	uint32_t seed = 12345;
	uint32_t k = 0, pulls = 0;
	int64_t last = -1;
	double arrival = 0.0;
	bool lost = false;
	bool next = true;
	size_t done = 0;
	uint32_t pull_frames = 0;
	double pull_at = 0.0;
	bool next_pull = true;
	while(done < TOTAL)
	{
		if(next_pull)
		{
			// The output asks for the next buffer once it played what it had, one call at a time.
			double nominal = (double)done / RATE;
			double late = 0.0;
			if(s->late_ms > 0 && fmod(nominal, 5.0) >= 2.0 && fmod(nominal, 5.0) < 3.0)
				late = next_random(&seed) * s->late_ms / 1000.0;
			pull_at = nominal + late > pull_at ? nominal + late : pull_at;
			pull_frames = pull_size(size, pulls);
			if(pull_frames > TOTAL - done)
				pull_frames = (uint32_t)(TOTAL - done);
			next_pull = false;
		}
		if(next)
		{
			// When packet k comes in, on the iPad's clock, after the one before it.
			double nominal = k * 0.010 / (1.0 + s->skew_ppm * 1e-6);
			double t = nominal + next_random(&seed) * s->jitter_ms / 1000.0;
			if(s->stall_ms > 0 && nominal >= s->stall_at && nominal < s->stall_at + s->stall_ms / 1000.0)
				t = s->stall_at + s->stall_ms / 1000.0;
			arrival = k >= p->count ? 1e9 : t > arrival ? t : arrival;
			lost = k < p->count && (!p->size[k] || next_random(&seed) < s->loss);
			next = false;
		}
		if(arrival <= pull_at)
		{
			next = true;
			if(!lost)
			{
				// The core: one lost frame per missing one, five at most, then the frame.
				int64_t missing = (int64_t)k - last - 1;
				for(int64_t m = 0; m <= missing && m <= CHIAKI_AUDIO_RECEIVER_CONCEAL_MAX_FRAMES; m++)
				{
					bool frame = m == missing || m == CHIAKI_AUDIO_RECEIVER_CONCEAL_MAX_FRAMES;
					uint8_t *buf = frame ? p->data + p->offset[k] : NULL;
					size_t size = frame ? p->size[k] : 0;
					r.delivered_lost += frame ? 0 : 1;
					if(naive)
						naive_frame(&n, buf, size);
					else
						sink.frame_cb(buf, size, sink.user);
					if(plain_frames + FRAME <= TOTAL)
						plain_frames += ck_audio_decoder_frame(&plain_decoder, buf, size, plain + plain_frames * CH, FRAME);
					if(frame)
						break;
				}
				last = k;
			}
			k++;
			continue;
		}

		int16_t *pcm = out + done * CH;
		if(naive)
			naive_pull(&n, pcm, pull_frames);
		else
			CHECK(fake_output_render(pcm, pull_frames));
		next_pull = true;
		pulls++;
		bool second = done / RATE != (done + pull_frames) / RATE;
		done += pull_frames;
		if(!second || naive)
			continue;
		CKAudioPlayerDetails d;
		ck_audio_player_take_details(player, &d);
		r.counted_lost += d.lost + d.refused;
		r.underruns += d.buffer.underruns;
		r.dropped += d.buffer.dropped;
		r.requested += d.buffer.requested;
		double t = (double)(done / RATE);
		bool after_stall = s->stall_ms > 0 && t > s->stall_at && t <= s->stall_at + 3.0;
		if(t <= 1.0 || after_stall)
			continue;
		if(d.buffer.level_ms < r.level_min_ms)
			r.level_min_ms = d.buffer.level_ms;
		if(d.buffer.level_ms > r.level_max_ms)
			r.level_max_ms = d.buffer.level_ms;
		if(d.buffer.level_ms < 40 || d.buffer.level_ms > 80)
			r.bad_seconds++;
	}
	ck_audio_decoder_fini(&plain_decoder);
	if(naive)
		ck_audio_decoder_fini(&n.decoder);
	else
		ck_audio_player_free(player);
	return r;
}

static void check_scenario(const Packets *p, const Scenario *s, double limit, double clean_rms, const OutputSize *size)
{
	Result r = run(p, s, false, size);
	size_t first = 0;
	uint32_t clicks = click_count(out, TOTAL, CH, RATE, limit, &first);
	// Opus concealment of several lost frames in a row can click by itself; the buffer must add none.
	uint32_t loss_clicks = click_count(plain, TOTAL, CH, RATE, limit, NULL);
	// The first second is mostly the wait for the buffer; the rest must sound.
	double level = rms(out + (size_t)RATE * CH, (size_t)(TOTAL - RATE) * CH);
	const char *kind = size->kind == CK_AUDIO_OUTPUT_AUDIOQUEUE ? ", AudioQueue" : "";
	printf("   %s, %s%s: %u clicks (%u decoding without the buffer, largest second difference %.0f), level %u-%u ms, %u lost and concealed, %u ran dry, %u dropped, %u made up, "
		"loudness %.2f of the clean decode\n", p->name, s->name, kind, clicks, loss_clicks,
		click_max_d2(out, TOTAL, CH), r.level_min_ms, r.level_max_ms,
		r.counted_lost, r.underruns, r.dropped, r.requested, level / clean_rms);
	if(clicks > loss_clicks)
		fprintf(stderr, "   the first click at %.3f s\n", (double)first / RATE);
	CHECK(clicks <= loss_clicks);
	// What A4.3 asks: none at all, with up to 2 % lost.
	if(!s->loss_clicks)
		CHECK_EQ(clicks, 0);
	CHECK_EQ(r.bad_seconds, 0);
	CHECK_EQ(r.counted_lost, r.delivered_lost);
	CHECK_EQ(r.underruns, s->stall_ms > 0 ? 1 : 0);
	CHECK(level > 0.85 * clean_rms);

	Result naive = run(p, s, true, size);
	uint32_t naive_clicks = click_count(out, TOTAL, CH, RATE, limit, NULL);
	printf("   %s, %s%s, naive player: %u clicks\n", p->name, s->name, kind, naive_clicks);
	CHECK(naive_clicks > loss_clicks);
	(void)naive;
}

static const Scenario scenarios[] = {
	// Home Wi-Fi as the PS5 sessions of 2026-10-22 saw it, and worse: 2 % lost, up to 10 ms late.
	{ "2% lost, jitter 10 ms", 10.0, 0.02, 100.0, 0.0, 0.0, false, 0.0 },
	// The clocks 0.1 % apart either way (real ones are closer): the level must hold.
	{ "PS5 clock 0.1% fast", 5.0, 0.02, 1000.0, 0.0, 0.0, false, 0.0 },
	{ "PS5 clock 0.1% slow", 5.0, 0.02, -1000.0, 0.0, 0.0, false, 0.0 },
	// The Wi-Fi stops for 300 ms: one fade-out, then the burst is cut back to the target.
	{ "300 ms stall", 10.0, 0.02, 100.0, 12.0, 300.0, false, 0.0 },
	// The output's thread up to 30 ms late for a second: the level goes up, the sound is no later.
	{ "output 30 ms late", 10.0, 0.02, 100.0, 0.0, 0.0, false, 30.0 },
	// Much worse than asked: Opus concealment clicks on long runs of losses, the buffer adds nothing.
	{ "5% lost, jitter 30 ms", 30.0, 0.05, 100.0, 0.0, 0.0, true, 0.0 },
	{ "10% lost, jitter 20 ms, 1 s stall", 20.0, 0.10, 100.0, 12.0, 1000.0, true, 0.0 },
};

static void check_packets(const Packets *p)
{
	decode_clean(p, clean, TOTAL);
	double limit = 2.0 * click_max_d2(clean, TOTAL, CH);
	double clean_rms = rms(clean, TOTAL * CH);
	printf("   %s: largest second difference of the clean decode %.0f, click limit %.0f\n", p->name, limit / 2.0, limit);
	for(size_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); i++)
		check_scenario(p, &scenarios[i], limit, clean_rms, &player_output);
	// The clocks: the fast one dropped frames, the slow one made some up.
	Result fast = run(p, &scenarios[1], false, &player_output);
	Result slow = run(p, &scenarios[2], false, &player_output);
	CHECK(fast.dropped > 0 && fast.requested == 0);
	CHECK(slow.requested > 0 && slow.dropped == 0);
	// With AudioQueue (ckaudio -o aq), pulls of 20 ms: the same.
	check_scenario(p, &scenarios[0], limit, clean_rms, &audioqueue_output);
	check_scenario(p, &scenarios[4], limit, clean_rms, &audioqueue_output);
}

/** The session starting over (the PS5 was busy) sends the header again: a fade-out, a fade-in, no click. */
static void test_restart(const Packets *p, double limit)
{
	CKAudioPlayer *player = ck_audio_player_new(&test_log);
	CHECK(player != NULL);
	ChiakiAudioSink sink;
	ck_audio_player_get_sink(player, &sink);
	ChiakiAudioHeader header = { CH, 16, RATE, FRAME, 0 };
	sink.header_cb(&header, sink.user);
	uint32_t f = 0;
	for(uint32_t pull = 0; pull < 400; pull++)
	{
		if(pull == 200)
			sink.header_cb(&header, sink.user);
		sink.frame_cb(p->data + p->offset[f], p->size[f], sink.user);
		f++;
		CHECK(fake_output_render(out + (size_t)pull * FRAME * CH, FRAME));
	}
	CHECK_EQ(click_count(out, 400 * FRAME, CH, RATE, limit, NULL), 0);
	// It plays again after the restart.
	CHECK(rms(out + (size_t)300 * FRAME * CH, 100 * FRAME * CH) > 500.0);

	// A header the player cannot play: silence and no crash, until a good one comes.
	ChiakiAudioHeader bad = { 6, 16, 44100, 441, 0 };
	sink.header_cb(&bad, sink.user);
	for(uint32_t pull = 0; pull < 20; pull++)
	{
		sink.frame_cb(p->data + p->offset[f], p->size[f], sink.user);
		f++;
		CHECK(fake_output_render(out, FRAME));
	}
	CHECK(rms(out, FRAME * CH) == 0.0);
	sink.header_cb(&header, sink.user);
	for(uint32_t pull = 0; pull < 20; pull++)
	{
		sink.frame_cb(p->data + p->offset[f], p->size[f], sink.user);
		f++;
		CHECK(fake_output_render(out, FRAME));
	}
	CHECK(rms(out, FRAME * CH) > 500.0);
	ck_audio_player_free(player);
}

/** What iOS may do to the audio while a stream plays. */
static void test_session_events(void)
{
	int made = fake_outputs_made();
	CKAudioPlayer *player = ck_audio_player_new(&test_log);
	CHECK(player != NULL);
	CHECK_EQ(fake_outputs_made(), made + 1);
	CHECK_EQ(fake_outputs_alive(), 1);
	CHECK(fake_session_active());
	CKAudioOutputConfig config = fake_output_config();
	CHECK_EQ(config.rate, RATE);
	CHECK_EQ(config.channels, CH);
	CHECK_EQ(config.kind, CK_AUDIO_OUTPUT_REMOTEIO);
	CHECK_EQ(fake_output_starts(), 1);

	// After an interruption the same output starts again.
	fake_session_send(CK_AUDIO_SESSION_INTERRUPTION_BEGAN);
	fake_session_send(CK_AUDIO_SESSION_INTERRUPTION_ENDED);
	CHECK_EQ(fake_output_starts(), 2);
	// After the audio server restarts, a new one.
	fake_session_send(CK_AUDIO_SESSION_MEDIA_RESET);
	CHECK_EQ(fake_outputs_made(), made + 2);
	CHECK_EQ(fake_outputs_alive(), 1);
	int16_t pcm[FRAME * CH];
	CHECK(fake_output_render(pcm, FRAME));
	CKAudioPlayerDetails d;
	ck_audio_player_take_details(player, &d);
	CHECK_EQ(d.output_resets, 1);
	uint32_t concealed = 99, buffered = 99;
	ck_audio_player_take_stats(player, &concealed, &buffered);
	CHECK_EQ(concealed, 0);
	CHECK_EQ(buffered, 0);

	ck_audio_player_free(player);
	CHECK_EQ(fake_outputs_alive(), 0);
	CHECK(!fake_session_active());
	// Nothing listens any more.
	fake_session_send(CK_AUDIO_SESSION_MEDIA_RESET);
	CHECK_EQ(fake_outputs_made(), made + 2);

	// AudioQueue, for ckaudio: within the limits only.
	const CKAudioOutputKind aq = CK_AUDIO_OUTPUT_AUDIOQUEUE;
	CHECK(ck_audio_player_new_output(&test_log, aq, CK_AUDIO_PLAYER_OUTPUT_BUFFER_MS_MAX + 1, 4) == NULL);
	CHECK(ck_audio_player_new_output(&test_log, aq, CK_AUDIO_PLAYER_OUTPUT_BUFFER_MS_MIN - 1, 4) == NULL);
	CHECK(ck_audio_player_new_output(&test_log, aq, 20, 1) == NULL);
	CHECK(ck_audio_player_new_output(&test_log, aq, 20, CK_AUDIO_OUTPUT_BUFFERS_MAX + 1) == NULL);
	CHECK(ck_audio_player_new_output(&test_log, (CKAudioOutputKind)7, 20, 4) == NULL);
	CHECK_EQ(fake_outputs_made(), made + 2);
	player = ck_audio_player_new_output(&test_log, aq, 10, 3);
	CHECK(player != NULL);
	config = fake_output_config();
	CHECK_EQ(config.kind, aq);
	CHECK_EQ(config.buffer_frames, FRAME);
	CHECK_EQ(config.buffer_count, 3);
	ck_audio_player_free(player);
	// RemoteIO takes no sizes: they are not looked at.
	player = ck_audio_player_new_output(&test_log, CK_AUDIO_OUTPUT_REMOTEIO, 0, 0);
	CHECK(player != NULL);
	ck_audio_player_free(player);
}

int main(int argc, char **argv)
{
	chiaki_log_init(&test_log, CHIAKI_LOG_ERROR, chiaki_log_cb_print, NULL);
	test_session_events();

	Packets p;
	memset(&p, 0, sizeof(p));
	CHECK(synthetic(&p));
	check_packets(&p);
	decode_clean(&p, clean, TOTAL);
	test_restart(&p, 2.0 * click_max_d2(clean, TOTAL, CH));
	packets_free(&p);

	for(int i = 1; i < argc; i++)
	{
		Packets c;
		memset(&c, 0, sizeof(c));
		if(capture(&c, argv[i]))
			check_packets(&c);
		else
			failures++;
		packets_free(&c);
	}

	if(failures)
	{
		fprintf(stderr, "player_test: %d failures\n", failures);
		return 1;
	}
	printf("player_test: ok\n");
	return 0;
}
