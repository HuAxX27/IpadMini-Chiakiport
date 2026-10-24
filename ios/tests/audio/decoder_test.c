// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Tests for the Opus decoder and the Ogg Opus reader (A4.1, ios/Audio/
// ck_audio_decoder.c and ck_ogg_opus.c), with packets libopus makes like the
// PS5's. check_decode.py compares whole files with ffmpeg; this checks the
// edges: headers, lost and broken frames, small buffers, broken files.
// Run by ios/tests/audio_tests.sh, which also makes RECORDING with make_opus.

#include "Audio/ck_audio_decoder.h"
#include "Audio/ck_ogg_opus.h"

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

#define FRAME 480

static ChiakiLog test_log;
static int16_t pcm[CK_AUDIO_DECODER_MAX_FRAMES * CK_AUDIO_DECODER_MAX_CHANNELS];

/** Packets like the PS5's: CELT, stereo, 10 ms, 64 kbps constant, of a 440 Hz tone. */
static int make_packets(uint8_t packets[][200], int count)
{
	int err;
	OpusEncoder *enc = opus_encoder_create(48000, 2, OPUS_APPLICATION_RESTRICTED_LOWDELAY, &err);
	opus_encoder_ctl(enc, OPUS_SET_BITRATE(64000));
	opus_encoder_ctl(enc, OPUS_SET_VBR(0));
	int size = 0;
	for(int p = 0; p < count; p++)
	{
		opus_int16 in[FRAME * 2];
		for(int i = 0; i < FRAME; i++)
			in[2 * i] = in[2 * i + 1] = (opus_int16)(8000.0 * sin(2.0 * 3.14159265358979 * 440.0 * (p * FRAME + i) / 48000.0));
		size = opus_encode(enc, in, FRAME, packets[p], 200);
	}
	opus_encoder_destroy(enc);
	return size;
}

static double rms(const int16_t *s, size_t n)
{
	double sum = 0;
	for(size_t i = 0; i < n; i++)
		sum += (double)s[i] * s[i];
	return sqrt(sum / n);
}

static void test_headers(void)
{
	CKAudioDecoder dec;
	CHECK(ck_audio_decoder_init(&dec, &test_log));
	uint8_t packets[1][200];
	int size = make_packets(packets, 1);
	CHECK_EQ(size, 80);
	CHECK_EQ(packets[0][0], 0xf4);

	// Nothing comes out before a header.
	CHECK_EQ(ck_audio_decoder_frame(&dec, packets[0], (size_t)size, pcm, CK_AUDIO_DECODER_MAX_FRAMES), 0);
	CHECK_EQ(ck_audio_decoder_frame(&dec, NULL, 0, pcm, CK_AUDIO_DECODER_MAX_FRAMES), 0);

	// Formats Opus does not decode.
	CHECK(!ck_audio_decoder_header(&dec, 2, 44100, 441));
	CHECK(!ck_audio_decoder_header(&dec, 3, 48000, FRAME));
	CHECK(!ck_audio_decoder_header(&dec, 0, 48000, FRAME));
	CHECK(!ck_audio_decoder_header(&dec, 2, 48000, 500));
	CHECK(!ck_audio_decoder_header(&dec, 2, 48000, 0));
	CHECK(!ck_audio_decoder_header(&dec, 2, 48000, 6000));
	CHECK(!dec.ready);
	CHECK_EQ(ck_audio_decoder_frame(&dec, packets[0], (size_t)size, pcm, CK_AUDIO_DECODER_MAX_FRAMES), 0);

	// What Opus decodes: 2.5 to 120 ms at its five rates.
	CHECK(ck_audio_decoder_header(&dec, 1, 8000, 20));
	CHECK(ck_audio_decoder_header(&dec, 2, 24000, 2880));
	CHECK(ck_audio_decoder_header(&dec, 2, 48000, 5760));
	CHECK(ck_audio_decoder_header(&dec, 2, 48000, FRAME));
	CHECK(dec.ready);
	CHECK_EQ(ck_audio_decoder_frame(&dec, packets[0], (size_t)size, pcm, CK_AUDIO_DECODER_MAX_FRAMES), FRAME);
	CHECK_EQ(dec.stats.decoded, 1);
	ck_audio_decoder_fini(&dec);
}

static void test_lost_and_broken(void)
{
	CKAudioDecoder dec;
	CHECK(ck_audio_decoder_init(&dec, &test_log));
	CHECK(ck_audio_decoder_header(&dec, 2, 48000, FRAME));
	static uint8_t packets[60][200];
	int size = make_packets(packets, 60);
	for(int p = 0; p < 50; p++)
		CHECK_EQ(ck_audio_decoder_frame(&dec, packets[p], (size_t)size, pcm, CK_AUDIO_DECODER_MAX_FRAMES), FRAME);
	double level = rms(pcm, FRAME * 2);
	CHECK(level > 3000.0);

	// A lost frame: one frame of filler that continues the tone, not silence.
	CHECK_EQ(ck_audio_decoder_frame(&dec, NULL, 0, pcm, CK_AUDIO_DECODER_MAX_FRAMES), FRAME);
	CHECK(rms(pcm, FRAME * 2) > level * 0.5);
	CHECK_EQ(dec.stats.concealed, 1);

	// A packet libopus refuses (a frame count code with no frames) is concealed too.
	uint8_t broken[3] = { 0xf7, 0x00, 0x00 };
	CHECK_EQ(ck_audio_decoder_frame(&dec, broken, sizeof(broken), pcm, CK_AUDIO_DECODER_MAX_FRAMES), FRAME);
	CHECK_EQ(dec.stats.errors, 1);

	// Too little room: nothing is written and nothing is counted.
	CHECK_EQ(ck_audio_decoder_frame(&dec, packets[52], (size_t)size, pcm, FRAME - 1), 0);
	CHECK_EQ(ck_audio_decoder_frame(&dec, NULL, 0, pcm, FRAME - 1), 0);
	CHECK_EQ(dec.stats.concealed, 1);

	// And the decoder goes on.
	for(int p = 53; p < 60; p++)
		CHECK_EQ(ck_audio_decoder_frame(&dec, packets[p], (size_t)size, pcm, CK_AUDIO_DECODER_MAX_FRAMES), FRAME);
	CHECK(rms(pcm, FRAME * 2) > 3000.0);
	CHECK_EQ(dec.stats.decoded, 57);
	ck_audio_decoder_fini(&dec);
}

static uint8_t *read_file(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	if(!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *data = n > 0 ? malloc((size_t)n) : NULL;
	if(data && fread(data, 1, (size_t)n, f) != (size_t)n)
	{
		free(data);
		data = NULL;
	}
	fclose(f);
	*size = (size_t)n;
	return data;
}

/** RECORDING is make_opus's: 3 s, every 50th frame lost. */
static void test_ogg(const char *path)
{
	size_t size;
	uint8_t *data = read_file(path, &size);
	CHECK(data != NULL);
	if(!data)
		return;
	CKOggOpus ogg;
	CHECK(ck_ogg_opus_open(&ogg, data, size) == NULL);
	CHECK_EQ(ogg.channels, 2);
	CHECK_EQ(ogg.pre_skip, 0);
	CHECK_EQ(ogg.input_rate, 48000);
	CHECK_EQ(ogg.end_granule, 3 * 48000);
	const uint8_t *packet;
	size_t packet_size;
	int packets = 0, lost = 0, r;
	while((r = ck_ogg_opus_next(&ogg, &packet, &packet_size)) == 1)
	{
		packets++;
		if(ck_ogg_opus_packet_is_lost(packet_size))
			lost++;
		else
			CHECK_EQ(packet_size, 80);
	}
	CHECK_EQ(r, 0);
	CHECK_EQ(packets, 300);
	CHECK_EQ(lost, 6);

	// One byte changed anywhere in a page breaks its CRC; a cut file is refused as well.
	data[size / 2] ^= 0x40;
	CHECK(ck_ogg_opus_open(&ogg, data, size) != NULL);
	data[size / 2] ^= 0x40;
	CHECK(ck_ogg_opus_open(&ogg, data, size - 10) != NULL);
	CHECK(ck_ogg_opus_open(&ogg, data, 0) != NULL);
	// Not Ogg at all.
	CHECK(ck_ogg_opus_open(&ogg, (const uint8_t *)"RIFF....WAVEfmt ", 16) != NULL);
	CHECK(ck_ogg_opus_open(&ogg, data, size) == NULL);
	free(data);
}

int main(int argc, char **argv)
{
	chiaki_log_init(&test_log, 0, chiaki_log_cb_print, NULL);
	test_headers();
	test_lost_and_broken();
	if(argc > 1)
		test_ogg(argv[1]);
	else
	{
		fprintf(stderr, "decoder_test: needs the recording make_opus wrote\n");
		failures++;
	}
	if(failures)
	{
		fprintf(stderr, "decoder_test: %d failure(s)\n", failures);
		return 1;
	}
	printf("decoder_test: ok\n");
	return 0;
}
