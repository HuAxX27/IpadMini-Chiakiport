// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A4.3: see fake_audio.h. One thread: the test's.

#include "fake_audio.h"

#include <stdlib.h>
#include <string.h>

struct ck_audio_output_t
{
	CKAudioOutputConfig config;
	CKAudioOutputPull pull;
	void *user;
	bool running;
	int starts;
};

static CKAudioOutput *newest;
static int made;
static int alive;

static CKAudioSessionCallback session_callback;
static void *session_user;
static bool session_active;

CKAudioOutput *ck_audio_output_new(ChiakiLog *log, const CKAudioOutputConfig *config, CKAudioOutputPull pull, void *user)
{
	(void)log;
	CKAudioOutput *output = calloc(1, sizeof(*output));
	if(!output)
		return NULL;
	output->config = *config;
	output->pull = pull;
	output->user = user;
	newest = output;
	made++;
	alive++;
	return output;
}

void ck_audio_output_free(CKAudioOutput *output)
{
	if(!output)
		return;
	if(newest == output)
		newest = NULL;
	alive--;
	free(output);
}

bool ck_audio_output_start(CKAudioOutput *output)
{
	output->running = true;
	output->starts++;
	return true;
}

void ck_audio_output_stop(CKAudioOutput *output)
{
	output->running = false;
}

void ck_audio_output_take_stats(CKAudioOutput *output, CKAudioOutputStats *stats)
{
	memset(stats, 0, sizeof(*stats));
	stats->running = output->running;
}

int fake_outputs_made(void)
{
	return made;
}

int fake_outputs_alive(void)
{
	return alive;
}

bool fake_output_render(int16_t *pcm, uint32_t frames)
{
	if(!newest || !newest->running)
		return false;
	newest->pull(pcm, frames, newest->user);
	return true;
}

CKAudioOutputConfig fake_output_config(void)
{
	CKAudioOutputConfig none;
	memset(&none, 0, sizeof(none));
	return newest ? newest->config : none;
}

int fake_output_starts(void)
{
	return newest ? newest->starts : 0;
}

bool ck_audio_session_begin(ChiakiLog *log, double preferred_rate, double preferred_io_buffer_s,
	CKAudioSessionCallback callback, void *user)
{
	(void)log;
	(void)preferred_rate;
	(void)preferred_io_buffer_s;
	session_callback = callback;
	session_user = user;
	session_active = true;
	return true;
}

void ck_audio_session_end(void)
{
	session_callback = NULL;
	session_user = NULL;
	session_active = false;
}

void ck_audio_session_get_info(CKAudioSessionInfo *info)
{
	memset(info, 0, sizeof(*info));
}

bool fake_session_active(void)
{
	return session_active;
}

void fake_session_send(CKAudioSessionEvent event)
{
	if(session_callback)
		session_callback(event, session_user);
}
