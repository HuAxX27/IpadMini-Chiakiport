// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A2.6: the plain C half of CKSession (ck_session.h).

#include "ck_session.h"
#include "ck_video_info.h"

#include <chiaki/thread.h>
#include <chiaki/time.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

struct ck_session_t
{
	ChiakiLog *log;
	char host[256];
	ChiakiRegisteredHost registered_host;
	CKSettings settings;
	CKSessionCallbacks callbacks;
	ChiakiVideoSampleCallback video_cb;
	void *video_cb_user;
	ChiakiAudioSink audio_sink;

	/** state and running; held while the callbacks run, so they arrive in order. */
	ChiakiMutex state_mutex;
	CKSessionCoreState state;
	/** Between a successful ck_session_start() and ck_session_stop(): session is initialized. */
	bool running;
	ChiakiSession session;

	ChiakiMutex sources_mutex;
	CKInputSource sources[CK_SESSION_INPUT_SOURCES_MAX];
	size_t sources_count;
	ChiakiBoolPredCond input_stop;
	ChiakiThread input_thread;

	ChiakiMutex stats_mutex;
	CKSessionStats stats;
	uint64_t stats_since_us;
};

void ck_session_video_profile(const CKSettings *settings, ChiakiConnectVideoProfile *profile)
{
	memset(profile, 0, sizeof(*profile));
	chiaki_connect_video_profile_preset(profile, settings->resolution, settings->fps);
	// The A5 has no HEVC decoder, whatever the preset says.
	profile->codec = CHIAKI_CODEC_H264;
	if(settings->bitrate_kbps)
		profile->bitrate = settings->bitrate_kbps;
}

bool ck_input_merge(const CKInputSource *sources, size_t count, ChiakiControllerState *state)
{
	chiaki_controller_state_set_idle(state);
	bool any = false;
	for(size_t i = 0; i < count; i++)
	{
		if(!sources[i].poll)
			continue;
		ChiakiControllerState polled;
		chiaki_controller_state_set_idle(&polled);
		if(!sources[i].poll(sources[i].user, &polled))
			continue;
		if(any)
			chiaki_controller_state_or(state, state, &polled);
		else
			*state = polled;
		any = true;
	}
	return any;
}

uint32_t ck_resident_kb(void)
{
#if defined(__APPLE__)
	struct task_basic_info info;
	mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;
	if(task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&info, &count) != KERN_SUCCESS)
		return 0;
	return (uint32_t)(info.resident_size / 1024);
#elif defined(__linux__)
	FILE *f = fopen("/proc/self/statm", "r");
	if(!f)
		return 0;
	unsigned long size = 0;
	unsigned long resident = 0;
	int n = fscanf(f, "%lu %lu", &size, &resident);
	fclose(f);
	long page = sysconf(_SC_PAGESIZE);
	if(n != 2 || page <= 0)
		return 0;
	return (uint32_t)((unsigned long long)resident * (unsigned long)page / 1024);
#else
	return 0;
#endif
}

// ---------------------------------------------------------------------------
// The core's callbacks
// ---------------------------------------------------------------------------

/** Changes the state and reports it, under state_mutex so reports never cross. */
static void set_state(CKSessionCore *session, CKSessionCoreState state)
{
	chiaki_mutex_lock(&session->state_mutex);
	if(session->state != state)
	{
		session->state = state;
		if(session->callbacks.state)
			session->callbacks.state(state, session->callbacks.user);
	}
	chiaki_mutex_unlock(&session->state_mutex);
}

static void event_cb(ChiakiEvent *event, void *user)
{
	CKSessionCore *session = user;
	switch(event->type)
	{
		case CHIAKI_EVENT_CONNECTED:
			CHIAKI_LOGI(session->log, "CKSession: streaming");
			set_state(session, CK_SESSION_CORE_STREAMING);
			break;
		case CHIAKI_EVENT_QUIT:
			CHIAKI_LOGI(session->log, "CKSession: quit, %s%s%s", chiaki_quit_reason_string(event->quit.reason),
				event->quit.reason_str ? ": " : "", event->quit.reason_str ? event->quit.reason_str : "");
			chiaki_mutex_lock(&session->state_mutex);
			if(session->callbacks.quit)
				session->callbacks.quit(event->quit.reason, event->quit.reason_str, session->callbacks.user);
			chiaki_mutex_unlock(&session->state_mutex);
			set_state(session, CK_SESSION_CORE_STOPPED);
			break;
		case CHIAKI_EVENT_LOGIN_PIN_REQUEST:
			// Only a console with a Remote Play PIN asks; the session waits until stopped.
			CHIAKI_LOGE(session->log, "CKSession: the console asks for its login PIN, which is not supported yet");
			break;
		default:
			break;
	}
}

/** Says in the log what the console sends, from the header that comes before the frames (A2.7). */
static void log_video_header(CKSessionCore *session, const CKVideoInfo *info)
{
	if(info->codec != CK_VIDEO_CODEC_H264)
	{
		CHIAKI_LOGE(session->log, "CKSession: the console sends H.265, which the A5 cannot decode");
		return;
	}
	CHIAKI_LOGI(session->log, "CKSession: the console sends H.264 %s, level %u.%u, %ux%u%s",
		ck_video_info_profile_name(info), info->level_idc / 10, info->level_idc % 10,
		(unsigned)info->width, (unsigned)info->height,
		ck_video_info_a5_decodes(info) ? "" : ", which the A5 cannot decode");
}

static bool video_cb(uint8_t *buf, size_t buf_size, int32_t frames_lost, bool frame_recovered, void *user)
{
	CKSessionCore *session = user;
	CKVideoInfo info;
	bool header = ck_video_info_parse(buf, buf_size, &info);
	if(header)
		log_video_header(session, &info);
	uint64_t start = chiaki_time_now_monotonic_us();
	bool accepted = session->video_cb
		? session->video_cb(buf, buf_size, frames_lost, frame_recovered, session->video_cb_user)
		: true;
	uint64_t spent = chiaki_time_now_monotonic_us() - start;
	chiaki_mutex_lock(&session->stats_mutex);
	// The header is not a frame.
	if(!header)
	{
		session->stats.video_frames++;
		if(accepted)
			session->stats.video_frames_accepted++;
	}
	if(frames_lost > 0)
		session->stats.video_frames_lost += (uint32_t)frames_lost;
	session->stats.video_bytes += buf_size;
	session->stats.video_callback_us += spent;
	chiaki_mutex_unlock(&session->stats_mutex);
	return accepted;
}

static void audio_header_cb(ChiakiAudioHeader *header, void *user)
{
	CKSessionCore *session = user;
	if(session->audio_sink.header_cb)
		session->audio_sink.header_cb(header, session->audio_sink.user);
}

static void audio_frame_cb(uint8_t *buf, size_t buf_size, void *user)
{
	CKSessionCore *session = user;
	if(session->audio_sink.frame_cb)
		session->audio_sink.frame_cb(buf, buf_size, session->audio_sink.user);
	chiaki_mutex_lock(&session->stats_mutex);
	session->stats.audio_frames++;
	if(!buf)
		session->stats.audio_frames_lost++;
	session->stats.audio_bytes += buf_size;
	chiaki_mutex_unlock(&session->stats_mutex);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

/** Polls the sources every CK_INPUT_POLL_INTERVAL_MS and hands the core what changed. */
static void *input_thread_func(void *user)
{
	CKSessionCore *session = user;
	ChiakiControllerState last;
	bool sent = false;
	uint64_t next = chiaki_time_now_monotonic_ms();
	chiaki_bool_pred_cond_lock(&session->input_stop);
	while(!session->input_stop.pred)
	{
		chiaki_bool_pred_cond_unlock(&session->input_stop);
		ChiakiControllerState state;
		chiaki_mutex_lock(&session->sources_mutex);
		ck_input_merge(session->sources, session->sources_count, &state);
		chiaki_mutex_unlock(&session->sources_mutex);
		// The core keeps the last state and sends it on its own schedule.
		if(!sent || !chiaki_controller_state_equals(&state, &last))
		{
			chiaki_session_set_controller_state(&session->session, &state);
			last = state;
			sent = true;
		}

		// On a fixed schedule, so the rate stays 125 Hz however long the polls take.
		next += CK_INPUT_POLL_INTERVAL_MS;
		uint64_t now = chiaki_time_now_monotonic_ms();
		if(now > next + CK_INPUT_POLL_INTERVAL_MS)
			next = now;
		chiaki_bool_pred_cond_lock(&session->input_stop);
		if(next > now && !session->input_stop.pred)
			chiaki_bool_pred_cond_timedwait(&session->input_stop, next - now);
	}
	chiaki_bool_pred_cond_unlock(&session->input_stop);
	return NULL;
}

ChiakiErrorCode ck_session_add_input_source(CKSessionCore *session, const CKInputSource *source)
{
	chiaki_mutex_lock(&session->sources_mutex);
	ChiakiErrorCode err = CHIAKI_ERR_OVERFLOW;
	if(session->sources_count < CK_SESSION_INPUT_SOURCES_MAX)
	{
		session->sources[session->sources_count++] = *source;
		err = CHIAKI_ERR_SUCCESS;
	}
	chiaki_mutex_unlock(&session->sources_mutex);
	return err;
}

void ck_session_remove_all_input_sources(CKSessionCore *session)
{
	chiaki_mutex_lock(&session->sources_mutex);
	session->sources_count = 0;
	chiaki_mutex_unlock(&session->sources_mutex);
}

// ---------------------------------------------------------------------------
// Life
// ---------------------------------------------------------------------------

CKSessionCore *ck_session_new(ChiakiLog *log, const char *host, const ChiakiRegisteredHost *registered_host,
	const CKSettings *settings, const CKSessionCallbacks *callbacks)
{
	CKSessionCore *session = calloc(1, sizeof(*session));
	if(!session)
		return NULL;
	session->log = log;
	snprintf(session->host, sizeof(session->host), "%s", host ? host : "");
	session->registered_host = *registered_host;
	session->settings = *settings;
	if(callbacks)
		session->callbacks = *callbacks;
	session->state = CK_SESSION_CORE_IDLE;
	if(chiaki_mutex_init(&session->state_mutex, false) != CHIAKI_ERR_SUCCESS)
		goto error;
	if(chiaki_mutex_init(&session->sources_mutex, false) != CHIAKI_ERR_SUCCESS)
		goto error_state_mutex;
	if(chiaki_mutex_init(&session->stats_mutex, false) != CHIAKI_ERR_SUCCESS)
		goto error_sources_mutex;
	if(chiaki_bool_pred_cond_init(&session->input_stop) != CHIAKI_ERR_SUCCESS)
		goto error_stats_mutex;
	return session;

error_stats_mutex:
	chiaki_mutex_fini(&session->stats_mutex);
error_sources_mutex:
	chiaki_mutex_fini(&session->sources_mutex);
error_state_mutex:
	chiaki_mutex_fini(&session->state_mutex);
error:
	free(session);
	return NULL;
}

void ck_session_free(CKSessionCore *session)
{
	if(!session)
		return;
	ck_session_stop(session);
	chiaki_bool_pred_cond_fini(&session->input_stop);
	chiaki_mutex_fini(&session->stats_mutex);
	chiaki_mutex_fini(&session->sources_mutex);
	chiaki_mutex_fini(&session->state_mutex);
	free(session);
}

void ck_session_set_video_cb(CKSessionCore *session, ChiakiVideoSampleCallback cb, void *user)
{
	session->video_cb = cb;
	session->video_cb_user = user;
}

void ck_session_set_audio_sink(CKSessionCore *session, const ChiakiAudioSink *sink)
{
	session->audio_sink = *sink;
}

ChiakiErrorCode ck_session_start(CKSessionCore *session)
{
	chiaki_mutex_lock(&session->state_mutex);
	bool running = session->running;
	bool ended = session->state == CK_SESSION_CORE_STOPPED;
	chiaki_mutex_unlock(&session->state_mutex);
	if(running && ended)
	{
		// The console ended the last stream and nobody joined it yet.
		ck_session_stop(session);
		running = false;
	}
	const ChiakiRegisteredHost *registered = &session->registered_host;
	if(running || !session->host[0] || !registered->rp_regist_key[0])
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiConnectInfo info;
	memset(&info, 0, sizeof(info));
	info.ps5 = chiaki_target_is_ps5(registered->target);
	info.host = session->host;
	memcpy(info.regist_key, registered->rp_regist_key, sizeof(info.regist_key));
	memcpy(info.morning, registered->rp_key, sizeof(info.morning));
	ck_session_video_profile(&session->settings, &info.video_profile);
	info.video_profile_auto_downgrade = true;
	chiaki_controller_state_set_idle(&info.cached_controller_state);

	// Resolves host right away: a bad address fails here, not on the session's thread.
	memset(&session->session, 0, sizeof(session->session));
	ChiakiErrorCode err = chiaki_session_init(&session->session, &info, session->log);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(session->log, "CKSession: could not set up the session with %s: %s", session->host,
			chiaki_error_string(err));
		return err;
	}
	chiaki_session_set_event_cb(&session->session, event_cb, session);
	chiaki_session_set_video_sample_cb(&session->session, video_cb, session);
	ChiakiAudioSink sink = { session, audio_header_cb, audio_frame_cb };
	chiaki_session_set_audio_sink(&session->session, &sink);

	chiaki_mutex_lock(&session->stats_mutex);
	memset(&session->stats, 0, sizeof(session->stats));
	session->stats_since_us = chiaki_time_now_monotonic_us();
	chiaki_mutex_unlock(&session->stats_mutex);

	chiaki_bool_pred_cond_lock(&session->input_stop);
	session->input_stop.pred = false;
	chiaki_bool_pred_cond_unlock(&session->input_stop);
	err = chiaki_thread_create(&session->input_thread, input_thread_func, session);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		chiaki_session_fini(&session->session);
		return err;
	}
	chiaki_thread_set_name(&session->input_thread, "CKSession input");

	// CONNECTING goes out before the session's thread can report anything.
	chiaki_mutex_lock(&session->state_mutex);
	err = chiaki_session_start(&session->session);
	if(err == CHIAKI_ERR_SUCCESS)
	{
		session->running = true;
		session->state = CK_SESSION_CORE_CONNECTING;
		if(session->callbacks.state)
			session->callbacks.state(CK_SESSION_CORE_CONNECTING, session->callbacks.user);
	}
	chiaki_mutex_unlock(&session->state_mutex);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(session->log, "CKSession: could not start the session: %s", chiaki_error_string(err));
		chiaki_bool_pred_cond_signal(&session->input_stop);
		chiaki_thread_join(&session->input_thread, NULL);
		chiaki_session_fini(&session->session);
		return err;
	}
	CHIAKI_LOGI(session->log, "CKSession: connecting to %s", session->host);
	return CHIAKI_ERR_SUCCESS;
}

void ck_session_stop(CKSessionCore *session)
{
	chiaki_mutex_lock(&session->state_mutex);
	bool running = session->running;
	chiaki_mutex_unlock(&session->state_mutex);
	if(!running)
		return;

	chiaki_bool_pred_cond_signal(&session->input_stop);
	chiaki_thread_join(&session->input_thread, NULL);

	chiaki_session_stop(&session->session);
	chiaki_session_join(&session->session);
	chiaki_session_fini(&session->session);

	chiaki_mutex_lock(&session->state_mutex);
	session->running = false;
	chiaki_mutex_unlock(&session->state_mutex);
	// The session's thread always reports its quit before it ends; this is only a safety net.
	set_state(session, CK_SESSION_CORE_STOPPED);
	CHIAKI_LOGI(session->log, "CKSession: stopped");
}

CKSessionCoreState ck_session_state(CKSessionCore *session)
{
	chiaki_mutex_lock(&session->state_mutex);
	CKSessionCoreState state = session->state;
	chiaki_mutex_unlock(&session->state_mutex);
	return state;
}

void ck_session_take_stats(CKSessionCore *session, CKSessionStats *stats)
{
	uint64_t now = chiaki_time_now_monotonic_us();
	chiaki_mutex_lock(&session->stats_mutex);
	*stats = session->stats;
	stats->elapsed_us = session->stats_since_us ? now - session->stats_since_us : 0;
	memset(&session->stats, 0, sizeof(session->stats));
	session->stats_since_us = now;
	chiaki_mutex_unlock(&session->stats_mutex);
}
