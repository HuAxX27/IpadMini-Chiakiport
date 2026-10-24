// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A4.2: see ck_audio_session.h. Built with ARC. Everything AVAudioSession
// offers here exists since iOS 6.

#import <AVFoundation/AVFoundation.h>

#include "ck_audio_session.h"

#include <pthread.h>
#include <string.h>

static pthread_mutex_t session_mutex = PTHREAD_MUTEX_INITIALIZER;
static CKAudioSessionCallback session_callback;
static void *session_user;
static ChiakiLog *session_log;
static NSArray *session_observers;

/** Calls the user's callback unless ck_audio_session_end() came first. */
static void notify(CKAudioSessionEvent event)
{
	pthread_mutex_lock(&session_mutex);
	if(session_callback)
		session_callback(event, session_user);
	pthread_mutex_unlock(&session_mutex);
}

static void log_error(ChiakiLog *log, const char *what, NSError *error)
{
	CHIAKI_LOGE(log, "Audio session: %s failed: %s", what,
		error ? error.localizedDescription.UTF8String : "no reason given");
}

bool ck_audio_session_begin(ChiakiLog *log, double preferred_rate, double preferred_io_buffer_s,
	CKAudioSessionCallback callback, void *user)
{
	AVAudioSession *session = [AVAudioSession sharedInstance];
	NSError *error = nil;
	if(![session setCategory:AVAudioSessionCategoryPlayback error:&error])
	{
		log_error(log, "setCategory Playback", error);
		return false;
	}
	// Preferences: iOS may not grant them, and the queue converts whatever it picks.
	if(preferred_rate > 0 && ![session setPreferredSampleRate:preferred_rate error:&error])
		CHIAKI_LOGW(log, "Audio session: preferred rate %.0f Hz refused: %s", preferred_rate,
			error.localizedDescription.UTF8String);
	if(preferred_io_buffer_s > 0 && ![session setPreferredIOBufferDuration:preferred_io_buffer_s error:&error])
		CHIAKI_LOGW(log, "Audio session: preferred buffer %.1f ms refused: %s", preferred_io_buffer_s * 1000.0,
			error.localizedDescription.UTF8String);

	pthread_mutex_lock(&session_mutex);
	session_callback = callback;
	session_user = user;
	session_log = log;
	pthread_mutex_unlock(&session_mutex);

	NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
	id interruption = [center addObserverForName:AVAudioSessionInterruptionNotification object:session queue:nil
		usingBlock:^(NSNotification *note) {
			NSUInteger type = [note.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue];
			if(type == AVAudioSessionInterruptionTypeBegan)
			{
				notify(CK_AUDIO_SESSION_INTERRUPTION_BEGAN);
				return;
			}
			// The session is inactive after an interruption: active again before the queue starts.
			NSError *activate_error = nil;
			if(![[AVAudioSession sharedInstance] setActive:YES error:&activate_error])
			{
				pthread_mutex_lock(&session_mutex);
				if(session_log)
					log_error(session_log, "setActive after the interruption", activate_error);
				pthread_mutex_unlock(&session_mutex);
				return;
			}
			notify(CK_AUDIO_SESSION_INTERRUPTION_ENDED);
		}];
	id route = [center addObserverForName:AVAudioSessionRouteChangeNotification object:session queue:nil
		usingBlock:^(NSNotification *note) {
			(void)note;
			notify(CK_AUDIO_SESSION_ROUTE_CHANGED);
		}];
	id reset = [center addObserverForName:AVAudioSessionMediaServicesWereResetNotification object:session queue:nil
		usingBlock:^(NSNotification *note) {
			(void)note;
			notify(CK_AUDIO_SESSION_MEDIA_RESET);
		}];
	session_observers = @[ interruption, route, reset ];

	if(![session setActive:YES error:&error])
	{
		log_error(log, "setActive", error);
		ck_audio_session_end();
		return false;
	}
	return true;
}

void ck_audio_session_end(void)
{
	pthread_mutex_lock(&session_mutex);
	session_callback = NULL;
	session_user = NULL;
	ChiakiLog *log = session_log;
	session_log = NULL;
	pthread_mutex_unlock(&session_mutex);

	NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
	for(id observer in session_observers)
		[center removeObserver:observer];
	session_observers = nil;

	NSError *error = nil;
	if(![[AVAudioSession sharedInstance] setActive:NO
		withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation error:&error] && log)
		log_error(log, "setActive NO", error);
}

void ck_audio_session_get_info(CKAudioSessionInfo *info)
{
	memset(info, 0, sizeof(*info));
	AVAudioSession *session = [AVAudioSession sharedInstance];
	info->sample_rate = session.sampleRate;
	info->io_buffer_s = session.IOBufferDuration;
	info->output_latency_s = session.outputLatency;
	AVAudioSessionPortDescription *port = session.currentRoute.outputs.firstObject;
	const char *name = port ? port.portType.UTF8String : "none";
	// portType is AVAudioSessionPortBuiltInSpeaker and the like: "Speaker", "Headphones".
	strncpy(info->route, name ? name : "?", sizeof(info->route) - 1);
}
