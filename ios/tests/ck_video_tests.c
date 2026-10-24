// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Linux tests for the C half of CKVideoSink, ios/Video/ck_video_core.c
// (A3.2), against a fake decoder that checks what it is given. Run them with
// ./ios/tests/ck_h264_tests.sh, which also builds them for armv7 under qemu
// and has ffmpeg decode what the fake decoder received. Two modes:
//
//   ck_video_tests                     unit tests
//   ck_video_tests replay IN.h264 OUT  sends a capture through the sink frame
//                                      by frame, as the core would, and writes
//                                      what reached the decoder as Annex B

#include "Video/ck_h264.h"
#include "Video/ck_video_core.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond) \
	do \
	{ \
		if(!(cond)) \
		{ \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
			failures++; \
		} \
	} while(0)

// The PS5's profile header (ck_h264_tests.c has its origin): SPS and PPS.
static const uint8_t PS5_HEADER[] = {
	0x00, 0x00, 0x00, 0x01, 0x67, 0x4d, 0x40, 0x20, 0x91, 0x8a, 0x03, 0xc0, 0x45, 0xfb,
	0xc0, 0x5a, 0x80, 0x80, 0x80, 0xa0, 0x00, 0x00, 0xfa, 0x40, 0x00, 0x75, 0x30, 0x11,
	0x34, 0x38, 0x74, 0x54, 0x00, 0x00, 0x00, 0x01, 0x68, 0xee, 0x3c, 0x80};
#define PS5_SPS_SIZE 28
#define PS5_PPS_SIZE 4

// The same header with another level_idc, as after a switch to another profile.
static const uint8_t OTHER_HEADER[] = {
	0x00, 0x00, 0x00, 0x01, 0x67, 0x4d, 0x40, 0x1f, 0x91, 0x8a, 0x03, 0xc0, 0x45, 0xfb,
	0x00, 0x00, 0x00, 0x01, 0x68, 0xee, 0x3c, 0x80};

// Frames shaped like the PS5's: slices only. The payload bytes are made up.
static const uint8_t IDR_FRAME[] = { 0x00, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x21, 0xa0 };
static const uint8_t P_FRAME[] = { 0x00, 0x00, 0x00, 0x01, 0x41, 0x9a, 0x02, 0x04, 0x3c, 0x00, 0x00, 0x01, 0x41, 0x40, 0x7c };

// --- Fake decoder -----------------------------------------------------------

#define FAKE_SCRIPT_MAX 32

typedef struct
{
	int configures;
	int decodes;
	int drops;
	/** Made and not dropped since. */
	bool held;
	/** The next configure() fails. */
	bool configure_fails;
	/** What decode() returns on each call; OK past the end. */
	CKVideoDecodeResult script[FAKE_SCRIPT_MAX];
	int script_count;
	uint8_t sps[CK_H264_PARAM_SET_MAX_SIZE];
	size_t sps_size;
	uint8_t pps[CK_H264_PARAM_SET_MAX_SIZE];
	size_t pps_size;
	/** The last frame, as received. */
	uint8_t frame[64];
	size_t frame_size;
	/** Frames that were not well-formed AVCC, or carried units the decoder must not get. */
	int bad_frames;
	/** Frames decoded with no decoder made, or after it was dropped. */
	int frames_without_decoder;
	uint64_t clock_us;
	uint32_t decode_cost_us;
	/** In replay mode, what the decoder got, written back as Annex B. */
	FILE *out;
} Fake;

static void write_nal(FILE *out, const uint8_t *nal, size_t size)
{
	static const uint8_t start_code[] = { 0x00, 0x00, 0x00, 0x01 };
	fwrite(start_code, 1, sizeof(start_code), out);
	fwrite(nal, 1, size, out);
}

static bool fake_configure(void *user, const uint8_t *sps, size_t sps_size, const uint8_t *pps, size_t pps_size)
{
	Fake *fake = user;
	fake->configures++;
	if(fake->configure_fails)
	{
		fake->configure_fails = false;
		fake->held = false;
		return false;
	}
	CHECK(sps_size > 0 && sps_size <= sizeof(fake->sps) && CK_H264_NAL_TYPE(sps[0]) == CK_H264_NAL_SPS);
	CHECK(pps_size > 0 && pps_size <= sizeof(fake->pps) && CK_H264_NAL_TYPE(pps[0]) == CK_H264_NAL_PPS);
	if(sps_size <= sizeof(fake->sps) && pps_size <= sizeof(fake->pps))
	{
		memcpy(fake->sps, sps, sps_size);
		fake->sps_size = sps_size;
		memcpy(fake->pps, pps, pps_size);
		fake->pps_size = pps_size;
	}
	if(fake->out)
	{
		write_nal(fake->out, sps, sps_size);
		write_nal(fake->out, pps, pps_size);
	}
	fake->held = true;
	return true;
}

static CKVideoDecodeResult fake_decode(void *user, const uint8_t *avcc, size_t avcc_size)
{
	Fake *fake = user;
	int call = fake->decodes++;
	fake->clock_us += fake->decode_cost_us;
	if(!fake->held)
		fake->frames_without_decoder++;
	if(avcc_size <= sizeof(fake->frame))
	{
		memcpy(fake->frame, avcc, avcc_size);
		fake->frame_size = avcc_size;
	}
	else
		fake->frame_size = 0;

	// Each NAL unit after a 4-byte big-endian length, filling the buffer exactly.
	size_t pos = 0;
	bool ok = avcc_size > 0;
	while(ok && pos < avcc_size)
	{
		if(avcc_size - pos < CK_H264_AVCC_LENGTH_SIZE)
		{
			ok = false;
			break;
		}
		size_t len = ((size_t)avcc[pos] << 24) | ((size_t)avcc[pos + 1] << 16) | ((size_t)avcc[pos + 2] << 8) | avcc[pos + 3];
		pos += CK_H264_AVCC_LENGTH_SIZE;
		if(len == 0 || len > avcc_size - pos || (CK_H264_NAL_BIT(CK_H264_NAL_TYPE(avcc[pos])) & CK_H264_DROP_FOR_VIDEOTOOLBOX))
		{
			ok = false;
			break;
		}
		if(fake->out)
			write_nal(fake->out, avcc + pos, len);
		pos += len;
	}
	if(!ok)
		fake->bad_frames++;
	return call < fake->script_count ? fake->script[call] : CK_VIDEO_DECODE_OK;
}

static void fake_drop(void *user)
{
	Fake *fake = user;
	fake->drops++;
	fake->held = false;
}

static uint64_t fake_now_us(void *user)
{
	return ((Fake *)user)->clock_us;
}

static CKVideoCore *fake_core(Fake *fake)
{
	memset(fake, 0, sizeof(*fake));
	CKVideoDecoder decoder = { fake_configure, fake_decode, fake_drop, fake_now_us, fake };
	CKVideoCore *core = ck_video_core_new(&decoder);
	CHECK(core != NULL);
	if(!core)
		exit(2);
	return core;
}

static bool sample(CKVideoCore *core, const uint8_t *buf, size_t size)
{
	return ck_video_core_sample(core, buf, size, 0, false);
}

static CKVideoCoreStats stats_of(CKVideoCore *core)
{
	CKVideoCoreStats stats;
	ck_video_core_take_stats(core, &stats);
	return stats;
}

// --- Unit tests ---------------------------------------------------------------

static void test_header_then_frames(void)
{
	Fake fake;
	CKVideoCore *core = fake_core(&fake);

	// The header alone: true, nothing made or decoded yet.
	CHECK(sample(core, PS5_HEADER, sizeof(PS5_HEADER)));
	CHECK(fake.configures == 0 && fake.decodes == 0);

	// The first frame makes the decoder from that SPS and PPS, then decodes.
	CHECK(sample(core, IDR_FRAME, sizeof(IDR_FRAME)));
	CHECK(fake.configures == 1 && fake.decodes == 1);
	CHECK(fake.sps_size == PS5_SPS_SIZE && memcmp(fake.sps, PS5_HEADER + 4, PS5_SPS_SIZE) == 0);
	CHECK(fake.pps_size == PS5_PPS_SIZE && memcmp(fake.pps, PS5_HEADER + 4 + PS5_SPS_SIZE + 4, PS5_PPS_SIZE) == 0);
	static const uint8_t idr_avcc[] = { 0x00, 0x00, 0x00, 0x05, 0x65, 0x88, 0x84, 0x21, 0xa0 };
	CHECK(fake.frame_size == sizeof(idr_avcc) && memcmp(fake.frame, idr_avcc, sizeof(idr_avcc)) == 0);

	// Later frames reuse it: two slices, two lengths.
	CHECK(sample(core, P_FRAME, sizeof(P_FRAME)));
	static const uint8_t p_avcc[] = { 0x00, 0x00, 0x00, 0x05, 0x41, 0x9a, 0x02, 0x04, 0x3c,
		0x00, 0x00, 0x00, 0x03, 0x41, 0x40, 0x7c };
	CHECK(fake.frame_size == sizeof(p_avcc) && memcmp(fake.frame, p_avcc, sizeof(p_avcc)) == 0);
	CHECK(fake.configures == 1 && fake.decodes == 2 && fake.bad_frames == 0);

	CKVideoCoreStats stats = stats_of(core);
	CHECK(stats.frames_decoded == 2 && stats.frames_failed == 0 && stats.frames_skipped == 0);
	CHECK(stats.configures == 1 && stats.configure_failures == 0 && stats.decoder_broken == 0);

	ck_video_core_free(core);
	CHECK(fake.drops == 1);
}

static void test_header_with_frame(void)
{
	// SPS, PPS and the frame in one buffer, as a replayed capture starts.
	uint8_t buf[sizeof(PS5_HEADER) + sizeof(IDR_FRAME)];
	memcpy(buf, PS5_HEADER, sizeof(PS5_HEADER));
	memcpy(buf + sizeof(PS5_HEADER), IDR_FRAME, sizeof(IDR_FRAME));
	Fake fake;
	CKVideoCore *core = fake_core(&fake);
	CHECK(sample(core, buf, sizeof(buf)));
	CHECK(fake.configures == 1 && fake.decodes == 1 && fake.bad_frames == 0);
	// The 4-byte start code became the 4-byte length.
	CHECK(fake.frame_size == sizeof(IDR_FRAME));
	ck_video_core_free(core);
}

static void test_frame_before_header(void)
{
	Fake fake;
	CKVideoCore *core = fake_core(&fake);
	CHECK(!sample(core, P_FRAME, sizeof(P_FRAME)));
	CHECK(fake.configures == 0 && fake.decodes == 0);
	CKVideoCoreStats stats = stats_of(core);
	CHECK(stats.frames_skipped == 1 && stats.frames_decoded == 0);
	// The header arrives: from then on frames decode.
	CHECK(sample(core, PS5_HEADER, sizeof(PS5_HEADER)));
	CHECK(sample(core, IDR_FRAME, sizeof(IDR_FRAME)));
	CHECK(fake.decodes == 1);
	ck_video_core_free(core);
}

static void test_no_nal_unit(void)
{
	static const uint8_t junk[] = { 0x12, 0x34, 0x56 };
	Fake fake;
	CKVideoCore *core = fake_core(&fake);
	CHECK(sample(core, PS5_HEADER, sizeof(PS5_HEADER)));
	CHECK(!sample(core, junk, sizeof(junk)));
	CHECK(!sample(core, junk, 0));
	CHECK(fake.decodes == 0);
	CHECK(stats_of(core).frames_skipped == 2);
	ck_video_core_free(core);
}

static void test_profile_switch(void)
{
	Fake fake;
	CKVideoCore *core = fake_core(&fake);
	CHECK(sample(core, PS5_HEADER, sizeof(PS5_HEADER)));
	CHECK(sample(core, IDR_FRAME, sizeof(IDR_FRAME)));
	// The same header again changes nothing.
	CHECK(sample(core, PS5_HEADER, sizeof(PS5_HEADER)));
	CHECK(sample(core, P_FRAME, sizeof(P_FRAME)));
	CHECK(fake.configures == 1);
	// Another one remakes the decoder before the next frame.
	CHECK(sample(core, OTHER_HEADER, sizeof(OTHER_HEADER)));
	CHECK(fake.configures == 1);
	CHECK(sample(core, IDR_FRAME, sizeof(IDR_FRAME)));
	CHECK(fake.configures == 2 && fake.sps[3] == 0x1f && fake.decodes == 3);
	ck_video_core_free(core);
}

static void test_decode_failures(void)
{
	Fake fake;
	CKVideoCore *core = fake_core(&fake);
	fake.script[1] = CK_VIDEO_DECODE_FAILED;
	fake.script[3] = CK_VIDEO_DECODE_BROKEN;
	fake.script_count = 4;
	CHECK(sample(core, PS5_HEADER, sizeof(PS5_HEADER)));
	CHECK(sample(core, IDR_FRAME, sizeof(IDR_FRAME)));
	// A rejected frame is lost for the core, and the decoder stays.
	CHECK(!sample(core, P_FRAME, sizeof(P_FRAME)));
	CHECK(sample(core, P_FRAME, sizeof(P_FRAME)));
	CHECK(fake.configures == 1 && fake.drops == 0);
	// A broken decoder is dropped, and made again for the next frame.
	CHECK(!sample(core, P_FRAME, sizeof(P_FRAME)));
	CHECK(fake.drops == 1 && !fake.held);
	CHECK(sample(core, IDR_FRAME, sizeof(IDR_FRAME)));
	CHECK(fake.configures == 2 && fake.frames_without_decoder == 0);
	CKVideoCoreStats stats = stats_of(core);
	CHECK(stats.frames_decoded == 3 && stats.frames_failed == 2 && stats.decoder_broken == 1 && stats.configures == 2);
	ck_video_core_free(core);
}

static void test_configure_failure(void)
{
	Fake fake;
	CKVideoCore *core = fake_core(&fake);
	fake.configure_fails = true;
	CHECK(sample(core, PS5_HEADER, sizeof(PS5_HEADER)));
	CHECK(!sample(core, IDR_FRAME, sizeof(IDR_FRAME)));
	CHECK(fake.configures == 1 && fake.decodes == 0);
	// The next frame tries again.
	CHECK(sample(core, IDR_FRAME, sizeof(IDR_FRAME)));
	CHECK(fake.configures == 2 && fake.decodes == 1);
	CKVideoCoreStats stats = stats_of(core);
	CHECK(stats.configures == 2 && stats.configure_failures == 1 && stats.frames_skipped == 1 && stats.frames_decoded == 1);
	ck_video_core_free(core);
}

static void test_large_frame(void)
{
	// Twice the initial buffer: it grows once and the frame arrives whole.
	size_t payload = 2 * CK_VIDEO_CORE_INITIAL_FRAME_SIZE;
	uint8_t *frame = malloc(4 + payload);
	CHECK(frame != NULL);
	if(!frame)
		return;
	frame[0] = 0x00;
	frame[1] = 0x00;
	frame[2] = 0x00;
	frame[3] = 0x01;
	frame[4] = 0x65;
	for(size_t i = 5; i < 4 + payload; i++)
		frame[i] = (uint8_t)(0x10 + i % 200);
	Fake fake;
	CKVideoCore *core = fake_core(&fake);
	CHECK(sample(core, PS5_HEADER, sizeof(PS5_HEADER)));
	CHECK(sample(core, frame, 4 + payload));
	CHECK(fake.decodes == 1 && fake.bad_frames == 0);
	CHECK(sample(core, IDR_FRAME, sizeof(IDR_FRAME)));
	CHECK(fake.bad_frames == 0);
	ck_video_core_free(core);
	free(frame);
}

static void test_stats(void)
{
	Fake fake;
	CKVideoCore *core = fake_core(&fake);
	CHECK(sample(core, PS5_HEADER, sizeof(PS5_HEADER)));
	fake.decode_cost_us = 4000;
	CHECK(sample(core, IDR_FRAME, sizeof(IDR_FRAME)));
	fake.decode_cost_us = 9000;
	CHECK(ck_video_core_sample(core, P_FRAME, sizeof(P_FRAME), 3, true));
	CKVideoCoreStats stats = stats_of(core);
	CHECK(stats.frames_decoded == 2 && stats.decode_us == 13000 && stats.decode_us_max == 9000 && stats.frames_lost == 3);
	// Taking them starts over.
	stats = stats_of(core);
	CHECK(stats.frames_decoded == 0 && stats.decode_us == 0 && stats.decode_us_max == 0 && stats.frames_lost == 0);
	ck_video_core_free(core);
}

static void test_reset(void)
{
	Fake fake;
	CKVideoCore *core = fake_core(&fake);
	CHECK(sample(core, PS5_HEADER, sizeof(PS5_HEADER)));
	CHECK(sample(core, IDR_FRAME, sizeof(IDR_FRAME)));
	ck_video_core_reset(core);
	CHECK(fake.drops == 1 && !fake.held);
	// The SPS and PPS are gone too: a frame needs a new header.
	CHECK(!sample(core, P_FRAME, sizeof(P_FRAME)));
	CHECK(sample(core, PS5_HEADER, sizeof(PS5_HEADER)));
	CHECK(sample(core, IDR_FRAME, sizeof(IDR_FRAME)));
	CHECK(fake.configures == 2 && fake.frames_without_decoder == 0);
	ck_video_core_free(core);
	CHECK(fake.drops == 2);
	ck_video_core_free(NULL);
}

// Random and cut buffers after a real header: nothing may crash, and the decoder only gets well-formed AVCC.
static void test_garbage(void)
{
	Fake fake;
	CKVideoCore *core = fake_core(&fake);
	CHECK(sample(core, PS5_HEADER, sizeof(PS5_HEADER)));
	uint8_t buf[512];
	unsigned seed = 12345;
	for(int round = 0; round < 2000; round++)
	{
		size_t size = (size_t)(round % (int)sizeof(buf));
		for(size_t i = 0; i < size; i++)
		{
			seed = seed * 1103515245u + 12345u;
			uint8_t b = (uint8_t)(seed >> 16);
			// Plenty of zeros and ones, so start codes show up.
			buf[i] = (b & 3) == 0 ? 0 : (b & 3) == 1 ? 1 : b;
		}
		ck_video_core_sample(core, buf, size, (int32_t)(seed % 3) - 1, (seed & 4) != 0);
	}
	CHECK(fake.bad_frames == 0);
	ck_video_core_free(core);
}

typedef struct
{
	CKVideoCore *core;
	int rounds;
} Feeder;

static void *feed(void *arg)
{
	Feeder *feeder = arg;
	for(int i = 0; i < feeder->rounds; i++)
	{
		ck_video_core_sample(feeder->core, PS5_HEADER, sizeof(PS5_HEADER), 0, false);
		ck_video_core_sample(feeder->core, IDR_FRAME, sizeof(IDR_FRAME), 0, false);
		ck_video_core_sample(feeder->core, P_FRAME, sizeof(P_FRAME), 1, false);
	}
	return NULL;
}

// The video thread feeds frames while the main thread takes the stats and resets.
static void test_threads(void)
{
	Fake fake;
	CKVideoCore *core = fake_core(&fake);
	Feeder feeder = { core, 3000 };
	pthread_t thread;
	CHECK(pthread_create(&thread, NULL, feed, &feeder) == 0);
	uint64_t decoded = 0;
	for(int i = 0; i < 300; i++)
	{
		decoded += stats_of(core).frames_decoded;
		if(i % 50 == 0)
			ck_video_core_reset(core);
	}
	pthread_join(thread, NULL);
	decoded += stats_of(core).frames_decoded;
	CHECK(decoded == (uint64_t)fake.decodes);
	CHECK(fake.bad_frames == 0 && fake.frames_without_decoder == 0);
	ck_video_core_free(core);
}

// --- Replay -------------------------------------------------------------------

static uint8_t *read_file(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	if(!f)
		return NULL;
	size_t capacity = 1 << 20;
	size_t len = 0;
	uint8_t *buf = malloc(capacity);
	while(buf)
	{
		len += fread(buf + len, 1, capacity - len, f);
		if(len < capacity)
			break;
		capacity *= 2;
		uint8_t *bigger = realloc(buf, capacity);
		if(!bigger)
			free(buf);
		buf = bigger;
	}
	bool ok = buf && !ferror(f);
	fclose(f);
	if(!ok)
	{
		free(buf);
		return NULL;
	}
	*size = len;
	return buf;
}

// Every frame of the capture must reach the decoder, whole and once.
static int replay(const char *in_path, const char *out_path)
{
	size_t size = 0;
	uint8_t *buf = read_file(in_path, &size);
	if(!buf)
	{
		fprintf(stderr, "%s: cannot read\n", in_path);
		return 1;
	}
	Fake fake;
	CKVideoCore *core = fake_core(&fake);
	fake.out = fopen(out_path, "wb");
	CHECK(fake.out != NULL);
	size_t pos = 0, frames = 0, refused = 0;
	const uint8_t *au;
	size_t au_size;
	while(fake.out && ck_h264_next_access_unit(buf, size, &pos, &au, &au_size))
	{
		CKH264FrameInfo info;
		ck_h264_annexb_to_avcc(au, au_size, 0, NULL, 0, &info);
		bool has_slice = (info.nal_types & (CK_H264_NAL_BIT(CK_H264_NAL_SLICE) | CK_H264_NAL_BIT(CK_H264_NAL_IDR))) != 0;
		frames += has_slice;
		refused += !ck_video_core_sample(core, au, au_size, 0, false);
	}
	CKVideoCoreStats stats = stats_of(core);
	ck_video_core_free(core);
	if(fake.out)
		CHECK(fclose(fake.out) == 0);
	free(buf);
	printf("%s: %zu frames, %u decoded, %zu refused, %u decoder(s) made, %d malformed\n", in_path, frames,
		stats.frames_decoded, refused, stats.configures, fake.bad_frames);
	CHECK(frames > 0 && stats.frames_decoded == frames && refused == 0);
	CHECK(stats.frames_failed == 0 && stats.frames_skipped == 0 && fake.bad_frames == 0);
	return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
	if(argc == 4 && strcmp(argv[1], "replay") == 0)
		return replay(argv[2], argv[3]);
	if(argc != 1)
	{
		fprintf(stderr, "usage: %s [replay IN.h264 OUT.h264]\n", argv[0]);
		return 2;
	}

	test_header_then_frames();
	test_header_with_frame();
	test_frame_before_header();
	test_no_nal_unit();
	test_profile_switch();
	test_decode_failures();
	test_configure_failure();
	test_large_frame();
	test_stats();
	test_reset();
	test_garbage();
	test_threads();

	if(failures)
	{
		fprintf(stderr, "ck_video_tests: %d check(s) failed\n", failures);
		return 1;
	}
	printf("ck_video_tests: all passed\n");
	return 0;
}
