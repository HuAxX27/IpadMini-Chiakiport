// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A2.8: Bridge/ck_recorder.c from the command line, for check_recording.py.
//   ck-recorder-tool PATH SAMPLE.h264 SECONDS
// records SAMPLE.h264 to PATH.h264 in two pieces, and SECONDS of a tone in
// the PS5's Opus frames to PATH.opus with every 37th frame lost, then prints
// how many samples and lost frames the audio has.

#include "Bridge/ck_recorder.h"
#include "opus_sample.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
	if(argc != 4)
	{
		fprintf(stderr, "usage: ck-recorder-tool PATH SAMPLE.h264 SECONDS\n");
		return 2;
	}
	ChiakiLog log;
	chiaki_log_init(&log, CHIAKI_LOG_ERROR | CHIAKI_LOG_WARNING, chiaki_log_cb_print, NULL);

	FILE *f = fopen(argv[2], "rb");
	if(!f)
	{
		perror(argv[2]);
		return 1;
	}
	static uint8_t video[1 << 20];
	size_t video_size = fread(video, 1, sizeof(video), f);
	fclose(f);

	CKRecorder *recorder = ck_recorder_open(argv[1], &log);
	if(!recorder)
		return 1;
	ck_recorder_video(recorder, video, video_size / 2);
	ck_recorder_video(recorder, video + video_size / 2, video_size - video_size / 2);

	ChiakiAudioSink sink;
	ck_recorder_get_audio_sink(recorder, &sink);
	ChiakiAudioHeader header;
	chiaki_audio_header_set(&header, 2, 16, 48000, 480);
	sink.header_cb(&header, sink.user);
	OpusSample sample;
	if(opus_sample_init(&sample, 0) != 0)
		return 1;
	int frames = atoi(argv[3]) * 100;
	uint8_t packet[OPUS_SAMPLE_PACKET_MAX];
	for(int i = 0; i < frames; i++)
	{
		int size = opus_sample_next(&sample, packet, sizeof(packet));
		if(size <= 0)
			return 1;
		if(i % 37 == 36)
			sink.frame_cb(NULL, 0, sink.user);
		else
			sink.frame_cb(packet, (size_t)size, sink.user);
	}
	opus_sample_fini(&sample);

	CKRecorderStats stats;
	ck_recorder_close(recorder, &stats);
	if(stats.video_error || stats.audio_error)
		return 1;
	printf("samples %llu\nlost %u\n", (unsigned long long)stats.audio_samples, stats.audio_lost);
	return 0;
}
