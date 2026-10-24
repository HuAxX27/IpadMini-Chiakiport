// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A2.8: see ck_recorder.h. The Ogg pages follow RFC 3533, the Opus mapping
// RFC 7845 and the packet lengths RFC 6716 (clause 3.2).

#include "ck_recorder.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PATH_MAX_LEN 1000

/** At 6 Mbps the video file is written about three times a second, the audio one every few seconds. */
#define VIDEO_FILE_BUFFER (256 * 1024)
#define AUDIO_FILE_BUFFER (16 * 1024)

/** A page holds whole packets: up to this many bytes, or OGG_PAGE_PACKETS of them (half a second of the PS5's 10 ms frames). */
#define OGG_PAGE_BODY_MAX 8192
#define OGG_PAGE_PACKETS 50
#define OGG_LACING_MAX 255
#define OGG_HEADER_SIZE 27
#define OGG_FLAG_BOS 0x02
#define OGG_FLAG_EOS 0x04

/** RFC 6716: a packet lasts 120 ms at most. Durations are counted at 48 kHz, as Ogg Opus does. */
#define OPUS_MAX_SAMPLES 5760

static const char opus_vendor[] = "Chiakiport";

struct ck_recorder_t
{
	ChiakiLog *log;

	FILE *video;
	char *video_buffer;
	bool video_error;
	uint32_t video_buffers;
	uint64_t video_bytes;

	FILE *audio;
	char *audio_buffer;
	bool audio_error;
	/** The header came and OpusHead is written. */
	bool audio_started;
	/** The Ogg stream has its last page; nothing more is written. */
	bool audio_finished;
	uint8_t channels;
	uint32_t rate;
	bool tags_written;
	uint32_t serial;
	uint32_t page_sequence;
	/** Samples of every packet handed to a page so far: the granule position of the open page. */
	uint64_t granule;
	/** The last real packet's first byte and frame count, to write a lost frame the same length. */
	uint8_t last_toc;
	uint32_t last_frames;
	bool have_toc;
	uint32_t audio_packets;
	uint32_t audio_lost;
	uint32_t audio_dropped;

	/** The open page: its packets are written only once the next one does not fit, or at the end. */
	uint8_t lacing[OGG_LACING_MAX];
	size_t lacing_count;
	size_t page_packets;
	uint8_t body[OGG_PAGE_BODY_MAX];
	size_t body_size;

	uint32_t crc_table[256];
};

static void put_le16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void put_le32(uint8_t *p, uint32_t v)
{
	for(int i = 0; i < 4; i++)
		p[i] = (uint8_t)(v >> (8 * i));
}

static void put_le64(uint8_t *p, uint64_t v)
{
	for(int i = 0; i < 8; i++)
		p[i] = (uint8_t)(v >> (8 * i));
}

/** Ogg's CRC-32: polynomial 0x04c11db7, not reflected, starting at 0, no final XOR. */
static void crc_init(uint32_t *table)
{
	for(uint32_t i = 0; i < 256; i++)
	{
		uint32_t r = i << 24;
		for(int j = 0; j < 8; j++)
			r = (r & 0x80000000u) ? (r << 1) ^ 0x04c11db7u : r << 1;
		table[i] = r;
	}
}

static uint32_t crc_update(const uint32_t *table, uint32_t crc, const uint8_t *data, size_t size)
{
	for(size_t i = 0; i < size; i++)
		crc = (crc << 8) ^ table[((crc >> 24) ^ data[i]) & 0xff];
	return crc;
}

/**
 * Samples a packet lasts, at 48 kHz, from its TOC byte (RFC 6716 clause 3.1)
 * and its frame count; 0 for a packet that is too short or too long.
 */
static uint32_t opus_packet_samples(const uint8_t *data, size_t size, uint32_t *frames_out)
{
	static const uint32_t silk[] = { 480, 960, 1920, 2880 };
	static const uint32_t celt[] = { 120, 240, 480, 960 };
	if(!size)
		return 0;
	unsigned config = data[0] >> 3;
	uint32_t frame;
	if(config < 12)
		frame = silk[config & 3];
	else if(config < 16)
		frame = (config & 1) ? 960 : 480;
	else
		frame = celt[config & 3];
	uint32_t frames;
	switch(data[0] & 3)
	{
		case 0:
			frames = 1;
			break;
		case 1:
		case 2:
			frames = 2;
			break;
		default:
			if(size < 2)
				return 0;
			frames = data[1] & 0x3f;
			break;
	}
	if(!frames || frames * frame > OPUS_MAX_SAMPLES)
		return 0;
	*frames_out = frames;
	return frames * frame;
}

static void audio_failed(CKRecorder *recorder)
{
	if(!recorder->audio_error)
		CHIAKI_LOGE(recorder->log, "CKRecorder: writing the audio failed: %s", strerror(errno));
	recorder->audio_error = true;
	recorder->audio_finished = true;
}

static void write_page(CKRecorder *recorder, uint8_t flags, uint64_t granule, const uint8_t *lacing, size_t lacing_count,
	const uint8_t *body, size_t body_size)
{
	if(recorder->audio_error)
		return;
	uint8_t header[OGG_HEADER_SIZE];
	memcpy(header, "OggS", 4);
	header[4] = 0; // stream_structure_version
	header[5] = flags;
	put_le64(header + 6, granule);
	put_le32(header + 14, recorder->serial);
	put_le32(header + 18, recorder->page_sequence++);
	put_le32(header + 22, 0);
	header[26] = (uint8_t)lacing_count;
	uint32_t crc = crc_update(recorder->crc_table, 0, header, sizeof(header));
	crc = crc_update(recorder->crc_table, crc, lacing, lacing_count);
	crc = crc_update(recorder->crc_table, crc, body, body_size);
	put_le32(header + 22, crc);
	if(fwrite(header, 1, sizeof(header), recorder->audio) != sizeof(header)
		|| fwrite(lacing, 1, lacing_count, recorder->audio) != lacing_count
		|| fwrite(body, 1, body_size, recorder->audio) != body_size)
		audio_failed(recorder);
}

/** A page with a single packet under 255 bytes: the two header packets. */
static void write_header_page(CKRecorder *recorder, uint8_t flags, const uint8_t *packet, size_t size)
{
	uint8_t lacing = (uint8_t)size;
	write_page(recorder, flags, 0, &lacing, 1, packet, size);
}

static void write_tags(CKRecorder *recorder, uint8_t flags)
{
	uint8_t tags[8 + 4 + sizeof(opus_vendor) - 1 + 4];
	memcpy(tags, "OpusTags", 8);
	put_le32(tags + 8, sizeof(opus_vendor) - 1);
	memcpy(tags + 12, opus_vendor, sizeof(opus_vendor) - 1);
	put_le32(tags + 12 + sizeof(opus_vendor) - 1, 0); // no user comments
	write_header_page(recorder, flags, tags, sizeof(tags));
	recorder->tags_written = true;
}

static void flush_page(CKRecorder *recorder, uint8_t flags)
{
	write_page(recorder, flags, recorder->granule, recorder->lacing, recorder->lacing_count, recorder->body,
		recorder->body_size);
	recorder->lacing_count = 0;
	recorder->body_size = 0;
	recorder->page_packets = 0;
}

/** Writes the last page, with the end-of-stream flag. */
static void finish_stream(CKRecorder *recorder)
{
	if(!recorder->audio_started || recorder->audio_finished)
		return;
	if(!recorder->tags_written)
		write_tags(recorder, OGG_FLAG_EOS);
	else if(recorder->page_packets)
		flush_page(recorder, OGG_FLAG_EOS);
	recorder->audio_finished = true;
}

static void add_packet(CKRecorder *recorder, const uint8_t *data, size_t size, uint32_t samples)
{
	size_t segments = size / 255 + 1;
	if(recorder->page_packets
		&& (recorder->page_packets >= OGG_PAGE_PACKETS || recorder->lacing_count + segments > OGG_LACING_MAX
			|| recorder->body_size + size > OGG_PAGE_BODY_MAX))
		flush_page(recorder, 0);
	// A packet of 255 bytes or more spans segments of 255 and ends with a shorter one, maybe 0.
	size_t left = size;
	while(left >= 255)
	{
		recorder->lacing[recorder->lacing_count++] = 255;
		left -= 255;
	}
	recorder->lacing[recorder->lacing_count++] = (uint8_t)left;
	memcpy(recorder->body + recorder->body_size, data, size);
	recorder->body_size += size;
	recorder->page_packets++;
	recorder->granule += samples;
}

CKRecorder *ck_recorder_open(const char *path, ChiakiLog *log)
{
	if(strlen(path) > PATH_MAX_LEN)
	{
		CHIAKI_LOGE(log, "CKRecorder: the path is too long");
		return NULL;
	}
	CKRecorder *recorder = calloc(1, sizeof(*recorder));
	if(!recorder)
		return NULL;
	recorder->log = log;
	recorder->video_buffer = malloc(VIDEO_FILE_BUFFER);
	recorder->audio_buffer = malloc(AUDIO_FILE_BUFFER);
	if(!recorder->video_buffer || !recorder->audio_buffer)
	{
		ck_recorder_close(recorder, NULL);
		return NULL;
	}

	char file[PATH_MAX_LEN + 8];
	snprintf(file, sizeof(file), "%s.h264", path);
	recorder->video = fopen(file, "wb");
	if(!recorder->video)
	{
		CHIAKI_LOGE(log, "CKRecorder: could not open %s: %s", file, strerror(errno));
		ck_recorder_close(recorder, NULL);
		return NULL;
	}
	setvbuf(recorder->video, recorder->video_buffer, _IOFBF, VIDEO_FILE_BUFFER);
	snprintf(file, sizeof(file), "%s.opus", path);
	recorder->audio = fopen(file, "wb");
	if(!recorder->audio)
	{
		CHIAKI_LOGE(log, "CKRecorder: could not open %s: %s", file, strerror(errno));
		ck_recorder_close(recorder, NULL);
		return NULL;
	}
	setvbuf(recorder->audio, recorder->audio_buffer, _IOFBF, AUDIO_FILE_BUFFER);

	crc_init(recorder->crc_table);
	// Any number works for a file with one stream; this one differs between recordings.
	recorder->serial = (uint32_t)time(NULL) ^ (uint32_t)(uintptr_t)recorder;
	return recorder;
}

void ck_recorder_close(CKRecorder *recorder, CKRecorderStats *stats)
{
	if(stats)
		memset(stats, 0, sizeof(*stats));
	if(!recorder)
		return;
	if(recorder->audio)
	{
		finish_stream(recorder);
		if(fclose(recorder->audio) != 0)
			audio_failed(recorder);
	}
	if(recorder->video && fclose(recorder->video) != 0)
	{
		if(!recorder->video_error)
			CHIAKI_LOGE(recorder->log, "CKRecorder: writing the video failed: %s", strerror(errno));
		recorder->video_error = true;
	}
	if(stats)
		ck_recorder_stats(recorder, stats);
	free(recorder->video_buffer);
	free(recorder->audio_buffer);
	free(recorder);
}

void ck_recorder_video(CKRecorder *recorder, const uint8_t *buf, size_t buf_size)
{
	if(recorder->video_error || !buf_size)
		return;
	if(fwrite(buf, 1, buf_size, recorder->video) != buf_size)
	{
		CHIAKI_LOGE(recorder->log, "CKRecorder: writing the video failed: %s", strerror(errno));
		recorder->video_error = true;
		return;
	}
	recorder->video_buffers++;
	recorder->video_bytes += buf_size;
}

void ck_recorder_audio_header(CKRecorder *recorder, const ChiakiAudioHeader *header)
{
	if(recorder->audio_finished)
		return;
	if(recorder->audio_started)
	{
		if(header->channels == recorder->channels && header->rate == recorder->rate)
			return;
		CHIAKI_LOGE(recorder->log, "CKRecorder: the audio changed to %u channels at %u Hz; the recording stops there",
			(unsigned)header->channels, (unsigned)header->rate);
		finish_stream(recorder);
		return;
	}
	// Channel mapping family 0 covers mono and stereo, which is what the PS5 sends.
	if(header->channels < 1 || header->channels > 2)
	{
		CHIAKI_LOGE(recorder->log, "CKRecorder: cannot record %u audio channels", (unsigned)header->channels);
		recorder->audio_finished = true;
		return;
	}
	recorder->channels = header->channels;
	recorder->rate = header->rate;
	uint8_t head[19];
	memcpy(head, "OpusHead", 8);
	head[8] = 1; // version
	head[9] = header->channels;
	put_le16(head + 10, 0); // pre-skip: keep every decoded sample
	put_le32(head + 12, header->rate);
	put_le16(head + 16, 0); // output gain
	head[18] = 0; // channel mapping family
	write_header_page(recorder, OGG_FLAG_BOS, head, sizeof(head));
	recorder->audio_started = true;
}

void ck_recorder_audio_frame(CKRecorder *recorder, const uint8_t *buf, size_t buf_size)
{
	if(!recorder->audio_started || recorder->audio_finished)
	{
		recorder->audio_dropped++;
		return;
	}
	uint8_t lost[2];
	const uint8_t *data = buf;
	size_t size = buf_size;
	bool is_lost = !buf || !buf_size;
	if(is_lost)
	{
		// Frames of no bytes tell the decoder to conceal them (RFC 6716 clause
		// 3.2.1): one frame with code 0, several with code 3 and no padding.
		if(!recorder->have_toc)
		{
			recorder->audio_dropped++;
			return;
		}
		lost[0] = (uint8_t)(recorder->last_toc & 0xfc);
		size = 1;
		if(recorder->last_frames > 1)
		{
			lost[0] |= 3;
			lost[1] = (uint8_t)recorder->last_frames;
			size = 2;
		}
		data = lost;
	}
	else if(buf_size > CK_RECORDER_OPUS_PACKET_MAX)
	{
		recorder->audio_dropped++;
		return;
	}
	uint32_t frames;
	uint32_t samples = opus_packet_samples(data, size, &frames);
	if(!samples)
	{
		recorder->audio_dropped++;
		return;
	}
	if(!is_lost)
	{
		recorder->last_toc = buf[0];
		recorder->last_frames = frames;
		recorder->have_toc = true;
	}
	if(!recorder->tags_written)
		write_tags(recorder, 0);
	add_packet(recorder, data, size, samples);
	recorder->audio_packets++;
	if(is_lost)
		recorder->audio_lost++;
}

static void sink_header(ChiakiAudioHeader *header, void *user)
{
	ck_recorder_audio_header(user, header);
}

static void sink_frame(uint8_t *buf, size_t buf_size, void *user)
{
	ck_recorder_audio_frame(user, buf, buf_size);
}

void ck_recorder_get_audio_sink(CKRecorder *recorder, ChiakiAudioSink *sink)
{
	sink->user = recorder;
	sink->header_cb = sink_header;
	sink->frame_cb = sink_frame;
}

void ck_recorder_stats(const CKRecorder *recorder, CKRecorderStats *stats)
{
	memset(stats, 0, sizeof(*stats));
	stats->video_buffers = recorder->video_buffers;
	stats->video_bytes = recorder->video_bytes;
	stats->audio_packets = recorder->audio_packets;
	stats->audio_lost = recorder->audio_lost;
	stats->audio_dropped = recorder->audio_dropped;
	stats->audio_samples = recorder->granule;
	stats->video_error = recorder->video_error;
	stats->audio_error = recorder->audio_error;
}
