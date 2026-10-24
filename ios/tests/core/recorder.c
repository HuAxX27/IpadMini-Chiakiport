// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A2.8: Bridge/ck_recorder.h. Each test records into a folder of its own
// under the system's temporary folder; the Ogg pages are read back and checked
// here with a CRC of their own, and every packet is decoded with libopus.
// check_recording.py checks the same kind of files with ffmpeg.

// nftw() and mkdtemp(), for the temporary folders.
#define _XOPEN_SOURCE 700

#include <munit.h>

#include "Bridge/ck_recorder.h"
#include "opus_sample.h"

#include <errno.h>
#include <ftw.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>

typedef struct tmp_dir_t
{
	char path[256];
	char base[300];
	ChiakiLog log;
} TmpDir;

static void *setup_dir(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	TmpDir *dir = malloc(sizeof(*dir));
	munit_assert_not_null(dir);
	const char *tmp = getenv("TMPDIR");
	snprintf(dir->path, sizeof(dir->path), "%s/ck-recorder-XXXXXX", tmp && *tmp ? tmp : "/tmp");
	munit_assert_not_null(mkdtemp(dir->path));
	snprintf(dir->base, sizeof(dir->base), "%s/session", dir->path);
	// Quiet: the tests make the recorder log errors on purpose.
	chiaki_log_init(&dir->log, 0, chiaki_log_cb_print, NULL);
	return dir;
}

static int remove_one(const char *path, const struct stat *st, int type, struct FTW *ftw)
{
	(void)st;
	(void)type;
	(void)ftw;
	return remove(path);
}

static void teardown_dir(void *fixture)
{
	TmpDir *dir = fixture;
	nftw(dir->path, remove_one, 16, FTW_DEPTH | FTW_PHYS);
	free(dir);
}

/** Reads PATH + ext into a new buffer. */
static uint8_t *read_file(const TmpDir *dir, const char *ext, size_t *size)
{
	char path[320];
	snprintf(path, sizeof(path), "%s%s", dir->base, ext);
	FILE *f = fopen(path, "rb");
	munit_assert_not_null(f);
	munit_assert_int(fseek(f, 0, SEEK_END), ==, 0);
	long len = ftell(f);
	munit_assert_long(len, >=, 0);
	rewind(f);
	uint8_t *buf = malloc((size_t)len + 1);
	munit_assert_not_null(buf);
	munit_assert_size(fread(buf, 1, (size_t)len, f), ==, (size_t)len);
	fclose(f);
	*size = (size_t)len;
	return buf;
}

static void stereo_header(ChiakiAudioHeader *header)
{
	chiaki_audio_header_set(header, 2, 16, 48000, 480);
}

// ---------------------------------------------------------------------------
// An Ogg reader for the checks, apart from the writer
// ---------------------------------------------------------------------------

#define MAX_PACKETS 512
#define MAX_PAGES 64

typedef struct ogg_file_t
{
	size_t pages;
	uint8_t flags[MAX_PAGES];
	uint64_t granule[MAX_PAGES];
	/** Packets that end on each page. */
	size_t packets_on_page[MAX_PAGES];
	size_t packets;
	const uint8_t *packet[MAX_PACKETS];
	size_t packet_size[MAX_PACKETS];
} OggFile;

static uint32_t le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t le64(const uint8_t *p)
{
	return (uint64_t)le32(p) | (uint64_t)le32(p + 4) << 32;
}

/** Bit by bit, unlike the writer's table. */
static uint32_t ogg_crc(const uint8_t *data, size_t size)
{
	uint32_t crc = 0;
	for(size_t i = 0; i < size; i++)
	{
		crc ^= (uint32_t)data[i] << 24;
		for(int b = 0; b < 8; b++)
			crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04c11db7u : crc << 1;
	}
	return crc;
}

/** Checks every page of buf and splits it into packets, which point into buf. */
static void parse_ogg(uint8_t *buf, size_t size, OggFile *ogg)
{
	memset(ogg, 0, sizeof(*ogg));
	size_t pos = 0;
	uint32_t serial = 0;
	while(pos < size)
	{
		munit_assert_size(size - pos, >=, 27);
		uint8_t *page = buf + pos;
		munit_assert_memory_equal(4, page, "OggS");
		munit_assert_uint8(page[4], ==, 0);
		uint8_t flags = page[5];
		munit_assert_false(flags & 0x01); // no packet goes on from the page before
		size_t segments = page[26];
		munit_assert_size(size - pos, >=, 27 + segments);
		size_t body = 0;
		for(size_t i = 0; i < segments; i++)
			body += page[27 + i];
		size_t page_size = 27 + segments + body;
		munit_assert_size(size - pos, >=, page_size);
		if(ogg->pages == 0)
			serial = le32(page + 14);
		munit_assert_uint32(le32(page + 14), ==, serial);
		munit_assert_uint32(le32(page + 18), ==, ogg->pages);
		uint32_t crc = le32(page + 22);
		memset(page + 22, 0, 4);
		munit_assert_uint32(ogg_crc(page, page_size), ==, crc);

		munit_assert_size(ogg->pages, <, MAX_PAGES);
		ogg->flags[ogg->pages] = flags;
		ogg->granule[ogg->pages] = le64(page + 6);
		const uint8_t *data = page + 27 + segments;
		size_t packet_size = 0;
		for(size_t i = 0; i < segments; i++)
		{
			packet_size += page[27 + i];
			if(page[27 + i] < 255)
			{
				munit_assert_size(ogg->packets, <, MAX_PACKETS);
				ogg->packet[ogg->packets] = data;
				ogg->packet_size[ogg->packets] = packet_size;
				ogg->packets++;
				ogg->packets_on_page[ogg->pages]++;
				data += packet_size;
				packet_size = 0;
			}
		}
		munit_assert_size(packet_size, ==, 0); // and no packet goes on to the next page
		ogg->pages++;
		pos += page_size;
	}
	munit_assert_size(ogg->pages, >=, 2);
	for(size_t i = 0; i < ogg->pages; i++)
	{
		munit_assert(((ogg->flags[i] & 0x02) != 0) == (i == 0)); // beginning of stream
		munit_assert(((ogg->flags[i] & 0x04) != 0) == (i == ogg->pages - 1)); // end of stream
	}
	// OpusHead and OpusTags, each alone on a page with granule 0 (RFC 7845 section 3).
	munit_assert_size(ogg->packets_on_page[0], ==, 1);
	munit_assert_size(ogg->packets_on_page[1], ==, 1);
	munit_assert_uint64(ogg->granule[0], ==, 0);
	munit_assert_uint64(ogg->granule[1], ==, 0);
	munit_assert_size(ogg->packet_size[0], ==, 19);
	munit_assert_memory_equal(8, ogg->packet[0], "OpusHead");
	munit_assert_uint8(ogg->packet[0][8], ==, 1);
	munit_assert_uint8(ogg->packet[0][18], ==, 0);
	munit_assert_memory_equal(8, ogg->packet[1], "OpusTags");
}

/** Decodes the audio packets, checks each page's granule, and returns the samples. */
static uint64_t decode_ogg(const OggFile *ogg, size_t *lost_packets)
{
	int err;
	OpusDecoder *decoder = opus_decoder_create(OPUS_SAMPLE_RATE, ogg->packet[0][9], &err);
	munit_assert_int(err, ==, OPUS_OK);
	static opus_int16 pcm[5760 * 2];
	uint64_t samples = 0;
	size_t packet = 2;
	*lost_packets = 0;
	for(size_t page = 2; page < ogg->pages; page++)
	{
		for(size_t i = 0; i < ogg->packets_on_page[page]; i++, packet++)
		{
			int n = opus_decode(decoder, ogg->packet[packet], (opus_int32)ogg->packet_size[packet], pcm, 5760, 0);
			munit_assert_int(n, >, 0);
			munit_assert_int(n, ==, opus_packet_get_nb_samples(ogg->packet[packet],
				(opus_int32)ogg->packet_size[packet], OPUS_SAMPLE_RATE));
			samples += (uint64_t)n;
			if(ogg->packet_size[packet] <= 2)
				(*lost_packets)++;
		}
		munit_assert_uint64(ogg->granule[page], ==, samples);
	}
	opus_decoder_destroy(decoder);
	return samples;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static MunitResult test_video(const MunitParameter params[], void *user)
{
	(void)params;
	TmpDir *dir = user;
	CKRecorder *recorder = ck_recorder_open(dir->base, &dir->log);
	munit_assert_not_null(recorder);
	static const uint8_t header[] = { 0, 0, 0, 1, 0x67, 0x64, 0x00, 0x1f, 0, 0, 0, 1, 0x68, 0xeb };
	static const uint8_t idr[] = { 0, 0, 0, 1, 0x65, 0x88, 0x84, 0x21 };
	static const uint8_t slice[] = { 0, 0, 0, 1, 0x41, 0x9a, 0x02 };
	ck_recorder_video(recorder, header, sizeof(header));
	ck_recorder_video(recorder, idr, sizeof(idr));
	ck_recorder_video(recorder, slice, 0);
	ck_recorder_video(recorder, slice, sizeof(slice));
	CKRecorderStats stats;
	ck_recorder_close(recorder, &stats);
	munit_assert_uint32(stats.video_buffers, ==, 3);
	munit_assert_uint64(stats.video_bytes, ==, sizeof(header) + sizeof(idr) + sizeof(slice));
	munit_assert_false(stats.video_error);

	size_t size;
	uint8_t *file = read_file(dir, ".h264", &size);
	munit_assert_size(size, ==, sizeof(header) + sizeof(idr) + sizeof(slice));
	munit_assert_memory_equal(sizeof(header), file, header);
	munit_assert_memory_equal(sizeof(idr), file + sizeof(header), idr);
	munit_assert_memory_equal(sizeof(slice), file + sizeof(header) + sizeof(idr), slice);
	free(file);
	// No header came, so the audio file stays empty.
	file = read_file(dir, ".opus", &size);
	munit_assert_size(size, ==, 0);
	free(file);
	return MUNIT_OK;
}

static MunitResult test_ogg_opus(const MunitParameter params[], void *user)
{
	(void)params;
	TmpDir *dir = user;
	CKRecorder *recorder = ck_recorder_open(dir->base, &dir->log);
	munit_assert_not_null(recorder);
	ChiakiAudioSink sink;
	ck_recorder_get_audio_sink(recorder, &sink);
	ChiakiAudioHeader header;
	stereo_header(&header);
	sink.header_cb(&header, sink.user);

	OpusSample sample;
	munit_assert_int(opus_sample_init(&sample, 0), ==, 0);
	uint8_t packet[OPUS_SAMPLE_PACKET_MAX];
	const int frames = 120;
	int lost = 0;
	for(int i = 0; i < frames; i++)
	{
		int size = opus_sample_next(&sample, packet, sizeof(packet));
		munit_assert_int(size, >, 0);
		if(i == 10 || i == 11 || i == 50)
		{
			sink.frame_cb(NULL, 0, sink.user);
			lost++;
		}
		else
			sink.frame_cb(packet, (size_t)size, sink.user);
	}
	opus_sample_fini(&sample);
	CKRecorderStats stats;
	ck_recorder_close(recorder, &stats);
	munit_assert_uint32(stats.audio_packets, ==, frames);
	munit_assert_uint32(stats.audio_lost, ==, lost);
	munit_assert_uint32(stats.audio_dropped, ==, 0);
	munit_assert_uint64(stats.audio_samples, ==, (uint64_t)frames * OPUS_SAMPLE_FRAME);
	munit_assert_false(stats.audio_error);

	size_t size;
	uint8_t *file = read_file(dir, ".opus", &size);
	OggFile ogg;
	parse_ogg(file, size, &ogg);
	munit_assert_uint8(ogg.packet[0][9], ==, 2);
	munit_assert_uint32(le32(ogg.packet[0] + 12), ==, 48000);
	munit_assert_size(ogg.packets, ==, 2 + (size_t)frames);
	// 50 packets a page: 120 make three pages after the two of the header.
	munit_assert_size(ogg.pages, ==, 5);
	size_t lost_packets;
	munit_assert_uint64(decode_ogg(&ogg, &lost_packets), ==, (uint64_t)frames * OPUS_SAMPLE_FRAME);
	munit_assert_size(lost_packets, ==, (size_t)lost);
	munit_assert_size(ogg.packet_size[2 + 10], ==, 1);
	free(file);
	return MUNIT_OK;
}

static MunitResult test_big_and_joined_packets(const MunitParameter params[], void *user)
{
	(void)params;
	TmpDir *dir = user;
	CKRecorder *recorder = ck_recorder_open(dir->base, &dir->log);
	munit_assert_not_null(recorder);
	ChiakiAudioHeader header;
	stereo_header(&header);
	ck_recorder_audio_header(recorder, &header);

	// At 510 kbps a 10 ms packet is over 255 bytes, so it spans lacing segments.
	OpusSample sample;
	munit_assert_int(opus_sample_init(&sample, 510000), ==, 0);
	uint8_t a[OPUS_SAMPLE_PACKET_MAX], b[OPUS_SAMPLE_PACKET_MAX], joined[2 * OPUS_SAMPLE_PACKET_MAX];
	for(int i = 0; i < 30; i++)
	{
		int size = opus_sample_next(&sample, a, sizeof(a));
		munit_assert_int(size, >, 255);
		ck_recorder_audio_frame(recorder, a, (size_t)size);
	}
	// Two frames in one packet, then a lost one: it must last 20 ms too.
	int a_size = opus_sample_next(&sample, a, sizeof(a));
	int b_size = opus_sample_next(&sample, b, sizeof(b));
	OpusRepacketizer *rp = opus_repacketizer_create();
	munit_assert_int(opus_repacketizer_cat(rp, a, a_size), ==, OPUS_OK);
	munit_assert_int(opus_repacketizer_cat(rp, b, b_size), ==, OPUS_OK);
	int joined_size = opus_repacketizer_out(rp, joined, sizeof(joined));
	munit_assert_int(joined_size, >, 0);
	opus_repacketizer_destroy(rp);
	ck_recorder_audio_frame(recorder, joined, (size_t)joined_size);
	ck_recorder_audio_frame(recorder, NULL, 0);
	opus_sample_fini(&sample);
	CKRecorderStats stats;
	ck_recorder_close(recorder, &stats);
	munit_assert_uint32(stats.audio_packets, ==, 32);
	munit_assert_uint64(stats.audio_samples, ==, 30 * 480 + 960 + 960);

	size_t size;
	uint8_t *file = read_file(dir, ".opus", &size);
	OggFile ogg;
	parse_ogg(file, size, &ogg);
	munit_assert_size(ogg.packets, ==, 2 + 32);
	munit_assert_size(ogg.packet_size[2 + 31], ==, 2);
	munit_assert_uint8(ogg.packet[2 + 31][0] & 3, ==, 3);
	munit_assert_uint8(ogg.packet[2 + 31][1], ==, 2);
	size_t lost_packets;
	munit_assert_uint64(decode_ogg(&ogg, &lost_packets), ==, 30 * 480 + 960 + 960);
	// Big packets fill a page before 50 of them do.
	munit_assert_size(ogg.pages, >, 3);
	free(file);
	return MUNIT_OK;
}

static MunitResult test_audio_edges(const MunitParameter params[], void *user)
{
	(void)params;
	TmpDir *dir = user;
	OpusSample sample;
	munit_assert_int(opus_sample_init(&sample, 0), ==, 0);
	uint8_t packet[OPUS_SAMPLE_PACKET_MAX];
	int size = opus_sample_next(&sample, packet, sizeof(packet));
	munit_assert_int(size, >, 0);
	ChiakiAudioHeader header;
	stereo_header(&header);
	CKRecorderStats stats;

	// A frame before the header, a lost one before any real one, one too big
	// and one whose TOC says 0 frames: all left out. Then a header with the
	// same format again, which changes nothing.
	CKRecorder *recorder = ck_recorder_open(dir->base, &dir->log);
	munit_assert_not_null(recorder);
	ck_recorder_audio_frame(recorder, packet, (size_t)size);
	ck_recorder_audio_header(recorder, &header);
	ck_recorder_audio_frame(recorder, NULL, 0);
	static uint8_t too_big[CK_RECORDER_OPUS_PACKET_MAX + 1];
	too_big[0] = packet[0];
	ck_recorder_audio_frame(recorder, too_big, sizeof(too_big));
	static const uint8_t no_frames[] = { 0xfb, 0x00 };
	ck_recorder_audio_frame(recorder, no_frames, sizeof(no_frames));
	ck_recorder_audio_header(recorder, &header);
	ck_recorder_audio_frame(recorder, packet, (size_t)size);
	// Then mono: one Ogg Opus stream cannot change, so the recording ends.
	ChiakiAudioHeader mono;
	chiaki_audio_header_set(&mono, 1, 16, 48000, 480);
	ck_recorder_audio_header(recorder, &mono);
	ck_recorder_audio_frame(recorder, packet, (size_t)size);
	ck_recorder_close(recorder, &stats);
	munit_assert_uint32(stats.audio_packets, ==, 1);
	munit_assert_uint32(stats.audio_dropped, ==, 5);
	size_t file_size;
	uint8_t *file = read_file(dir, ".opus", &file_size);
	OggFile ogg;
	parse_ogg(file, file_size, &ogg);
	munit_assert_size(ogg.packets, ==, 3);
	free(file);

	// A header and nothing else: OpusTags ends the stream.
	recorder = ck_recorder_open(dir->base, &dir->log);
	munit_assert_not_null(recorder);
	ck_recorder_audio_header(recorder, &header);
	ck_recorder_close(recorder, &stats);
	file = read_file(dir, ".opus", &file_size);
	parse_ogg(file, file_size, &ogg);
	munit_assert_size(ogg.pages, ==, 2);
	munit_assert_size(ogg.packets, ==, 2);
	free(file);

	// Six channels need a mapping family the recorder does not write: no audio at all.
	recorder = ck_recorder_open(dir->base, &dir->log);
	munit_assert_not_null(recorder);
	ChiakiAudioHeader surround;
	chiaki_audio_header_set(&surround, 6, 16, 48000, 480);
	ck_recorder_audio_header(recorder, &surround);
	ck_recorder_audio_frame(recorder, packet, (size_t)size);
	ck_recorder_close(recorder, &stats);
	munit_assert_uint32(stats.audio_packets, ==, 0);
	munit_assert_uint32(stats.audio_dropped, ==, 1);
	file = read_file(dir, ".opus", &file_size);
	munit_assert_size(file_size, ==, 0);
	free(file);

	opus_sample_fini(&sample);
	return MUNIT_OK;
}

static MunitResult test_open_errors(const MunitParameter params[], void *user)
{
	(void)params;
	TmpDir *dir = user;
	char path[400];
	snprintf(path, sizeof(path), "%s/missing/session", dir->path);
	munit_assert_null(ck_recorder_open(path, &dir->log));
	static char long_path[1100];
	memset(long_path, 'a', sizeof(long_path) - 1);
	munit_assert_null(ck_recorder_open(long_path, &dir->log));
	ck_recorder_close(NULL, NULL);
	return MUNIT_OK;
}

static MunitResult test_full_disk(const MunitParameter params[], void *user)
{
	(void)params;
	TmpDir *dir = user;
	// A file size limit stands in for a full disk: writes past it fail with EFBIG.
	struct rlimit old_limit, limit;
	munit_assert_int(getrlimit(RLIMIT_FSIZE, &old_limit), ==, 0);
	void (*old_handler)(int) = signal(SIGXFSZ, SIG_IGN);
	limit = old_limit;
	limit.rlim_cur = 64 * 1024;
	munit_assert_int(setrlimit(RLIMIT_FSIZE, &limit), ==, 0);

	CKRecorder *recorder = ck_recorder_open(dir->base, &dir->log);
	munit_assert_not_null(recorder);
	static uint8_t frame[32 * 1024];
	frame[3] = 1;
	frame[4] = 0x41;
	for(int i = 0; i < 32; i++)
		ck_recorder_video(recorder, frame, sizeof(frame));
	ChiakiAudioHeader header;
	stereo_header(&header);
	ck_recorder_audio_header(recorder, &header);
	OpusSample sample;
	munit_assert_int(opus_sample_init(&sample, 510000), ==, 0);
	uint8_t packet[OPUS_SAMPLE_PACKET_MAX];
	for(int i = 0; i < 300; i++)
	{
		int size = opus_sample_next(&sample, packet, sizeof(packet));
		ck_recorder_audio_frame(recorder, packet, (size_t)size);
	}
	opus_sample_fini(&sample);
	CKRecorderStats stats;
	ck_recorder_close(recorder, &stats);

	setrlimit(RLIMIT_FSIZE, &old_limit);
	signal(SIGXFSZ, old_handler);
	munit_assert_true(stats.video_error);
	munit_assert_true(stats.audio_error);
	munit_assert_uint32(stats.video_buffers, <, 32);
	return MUNIT_OK;
}

#define DIR_TEST(name, fn) { name, fn, setup_dir, teardown_dir, MUNIT_TEST_OPTION_NONE, NULL }

static MunitTest tests[] = {
	DIR_TEST("/video", test_video),
	DIR_TEST("/ogg_opus", test_ogg_opus),
	DIR_TEST("/big_and_joined_packets", test_big_and_joined_packets),
	DIR_TEST("/audio_edges", test_audio_edges),
	DIR_TEST("/open_errors", test_open_errors),
	DIR_TEST("/full_disk", test_full_disk),
	{ NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
};

static const MunitSuite suite = { "/recorder", tests, NULL, 1, MUNIT_SUITE_OPTION_NONE };

int main(int argc, char *argv[])
{
	return munit_suite_main(&suite, NULL, argc, argv);
}
