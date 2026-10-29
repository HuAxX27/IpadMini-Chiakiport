// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A4.1: decodes an Ogg Opus file with the iPad's decoder (ios/Audio/
// ck_audio_decoder.c) and writes it as a WAV, for check_decode.py, which
// compares it with ffmpeg's.
//
//   opus_wav IN.opus OUT.wav [LOSS_PERCENT [SEED]]
//
// A packet ck_recorder marked as lost goes to the decoder as contract C3 hands
// it over (NULL, size 0). LOSS_PERCENT drops that share of the other packets
// too, picked by a fixed pseudo-random sequence from SEED, the way a network
// would. The pre-skip and the end of the stream are trimmed as RFC 7845 says.
// Prints two lines: packets, lost ones, frames concealed and samples written,
// then where each concealed frame starts in the WAV, in frames.

#include "Audio/ck_audio_decoder.h"
#include "Audio/ck_ogg_opus.h"

#include <opus/opus.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *read_file(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	if(!f)
		return NULL;
	uint8_t *data = NULL;
	if(fseek(f, 0, SEEK_END) == 0)
	{
		long n = ftell(f);
		if(n > 0 && fseek(f, 0, SEEK_SET) == 0 && (data = malloc((size_t)n)) && fread(data, 1, (size_t)n, f) != (size_t)n)
		{
			free(data);
			data = NULL;
		}
		*size = n > 0 ? (size_t)n : 0;
	}
	fclose(f);
	return data;
}

static void put_le16(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void put_le32(uint8_t *p, uint32_t v)
{
	put_le16(p, v);
	put_le16(p + 2, v >> 16);
}

/** A canonical 44-byte WAV header: PCM, 16 bits. */
static bool write_wav(const char *path, const int16_t *pcm, uint64_t frames, uint32_t channels, uint32_t rate)
{
	uint32_t data_size = (uint32_t)(frames * channels * 2);
	uint8_t h[44];
	memcpy(h, "RIFF", 4);
	put_le32(h + 4, 36 + data_size);
	memcpy(h + 8, "WAVEfmt ", 8);
	put_le32(h + 16, 16);
	put_le16(h + 20, 1);
	put_le16(h + 22, channels);
	put_le32(h + 24, rate);
	put_le32(h + 28, rate * channels * 2);
	put_le16(h + 32, channels * 2);
	put_le16(h + 34, 16);
	memcpy(h + 36, "data", 4);
	put_le32(h + 40, data_size);
	FILE *f = fopen(path, "wb");
	if(!f)
		return false;
	// The A5 and every test host are little-endian, the WAV's order.
	bool ok = fwrite(h, 1, sizeof(h), f) == sizeof(h) && fwrite(pcm, 2, (size_t)frames * channels, f) == (size_t)frames * channels;
	return fclose(f) == 0 && ok;
}

/** A fixed sequence (a 32-bit LCG), so a loss pattern is the same on every host. */
static uint32_t next_random(uint32_t *state)
{
	*state = *state * 1664525u + 1013904223u;
	return *state >> 8;
}

int main(int argc, char **argv)
{
	if(argc < 3 || argc > 5)
	{
		fprintf(stderr, "usage: opus_wav IN.opus OUT.wav [LOSS_PERCENT [SEED]]\n");
		return 2;
	}
	double loss = argc > 3 ? atof(argv[3]) : 0.0;
	uint32_t seed = argc > 4 ? (uint32_t)strtoul(argv[4], NULL, 10) : 1;

	ChiakiLog log;
	chiaki_log_init(&log, CHIAKI_LOG_ERROR | CHIAKI_LOG_WARNING, chiaki_log_cb_print, NULL);

	// Every way out goes through done, which frees what was made.
	int status = 1;
	int16_t *out = NULL;
	uint64_t *lost_at = NULL;
	CKAudioDecoder decoder;
	bool decoder_made = false;
	size_t size = 0;
	uint8_t *data = read_file(argv[1], &size);
	if(!data)
	{
		fprintf(stderr, "opus_wav: no pude leer %s\n", argv[1]);
		goto done;
	}
	CKOggOpus ogg;
	const char *bad = ck_ogg_opus_open(&ogg, data, size);
	if(bad)
	{
		fprintf(stderr, "opus_wav: %s %s\n", argv[1], bad);
		goto done;
	}
	uint64_t total = ogg.end_granule > ogg.pre_skip ? ogg.end_granule - ogg.pre_skip : 0;
	out = malloc((size_t)(total + 1) * ogg.channels * sizeof(int16_t));
	// A packet lasts 2.5 ms (120 samples) at least, so this many hold every one.
	size_t lost_max = (size_t)(ogg.end_granule / 120) + 16;
	lost_at = malloc(lost_max * sizeof(uint64_t));
	size_t lost_count = 0;
	if(!out || !lost_at || !(decoder_made = ck_audio_decoder_init(&decoder, &log)))
		goto done;

	static int16_t pcm[CK_AUDIO_DECODER_MAX_FRAMES * CK_AUDIO_DECODER_MAX_CHANNELS];
	uint64_t position = 0, written = 0;
	uint32_t packets = 0, marked_lost = 0, dropped = 0;
	const uint8_t *packet;
	size_t packet_size;
	int r;
	while((r = ck_ogg_opus_next(&ogg, &packet, &packet_size)) == 1)
	{
		packets++;
		bool lost = ck_ogg_opus_packet_is_lost(packet_size);
		if(lost)
			marked_lost++;
		if(!decoder.ready)
		{
			// The stream's frame length, from its first real packet: what a concealed frame lasts.
			int frame = lost ? -1 : opus_packet_get_nb_samples(packet, (opus_int32)packet_size, 48000);
			if(frame <= 0 || !ck_audio_decoder_header(&decoder, ogg.channels, 48000, (uint32_t)frame))
			{
				fprintf(stderr, "opus_wav: el paquete %u no sirve para empezar\n", packets);
				goto done;
			}
		}
		if(!lost && loss > 0 && next_random(&seed) % 1000000u < (uint32_t)(loss * 10000.0))
		{
			lost = true;
			dropped++;
		}
		if(lost && lost_count < lost_max)
			lost_at[lost_count++] = written;
		uint32_t frames = lost ? ck_audio_decoder_frame(&decoder, NULL, 0, pcm, CK_AUDIO_DECODER_MAX_FRAMES)
			: ck_audio_decoder_frame(&decoder, packet, packet_size, pcm, CK_AUDIO_DECODER_MAX_FRAMES);
		// Keep [pre_skip, end_granule) of the stream's samples.
		for(uint32_t i = 0; i < frames; i++, position++)
		{
			if(position < ogg.pre_skip || written >= total)
				continue;
			memcpy(out + written * ogg.channels, pcm + (size_t)i * ogg.channels, ogg.channels * sizeof(int16_t));
			written++;
		}
	}
	if(r < 0)
	{
		fprintf(stderr, "opus_wav: %s está dañado después del paquete %u\n", argv[1], packets);
		goto done;
	}
	if(!write_wav(argv[2], out, written, ogg.channels, 48000))
	{
		fprintf(stderr, "opus_wav: no pude escribir %s\n", argv[2]);
		goto done;
	}
	printf("packets=%u marked_lost=%u dropped=%u decoded=%u concealed=%u errors=%u frames=%llu channels=%u\n",
		packets, marked_lost, dropped, decoder.stats.decoded, decoder.stats.concealed, decoder.stats.errors,
		(unsigned long long)written, ogg.channels);
	printf("lost_at");
	for(size_t i = 0; i < lost_count; i++)
		printf("%c%llu", i ? ',' : '=', (unsigned long long)lost_at[i]);
	printf("\n");
	status = 0;

done:
	if(decoder_made)
		ck_audio_decoder_fini(&decoder);
	free(lost_at);
	free(out);
	free(data);
	return status;
}
