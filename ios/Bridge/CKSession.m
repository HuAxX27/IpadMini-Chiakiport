// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A2.6: contract C1 (CKSession.h) on top of ck_session.c, which does the work
// and is tested on Linux. This file only turns its callbacks into delegate
// calls on the main thread and fills CKMetrics once per second. Built with
// ARC. Every method runs on the main thread.
//
// CKMetrics is measured at the session's edge: fps_decoded counts the frames
// the video callback took, decode_ms_avg the time spent in it, and
// audio_buffered_ms stays 0 because only the audio player knows it.

#import "CKSession.h"

#include "ck_session.h"

#include <stdlib.h>

_Static_assert((NSInteger)CKSessionStateIdle == CK_SESSION_CORE_IDLE, "CKSessionState and ck_session differ");
_Static_assert((NSInteger)CKSessionStateConnecting == CK_SESSION_CORE_CONNECTING, "CKSessionState and ck_session differ");
_Static_assert((NSInteger)CKSessionStateStreaming == CK_SESSION_CORE_STREAMING, "CKSessionState and ck_session differ");
_Static_assert((NSInteger)CKSessionStateStopped == CK_SESSION_CORE_STOPPED, "CKSessionState and ck_session differ");

static NSString *const CKSessionErrorDomain = @"CKSessionErrorDomain";

@interface CKSession ()
@property (nonatomic, readwrite) CKSessionState state;
- (void)coreChangedState:(CKSessionState)state generation:(NSUInteger)generation;
- (void)coreQuitWithReason:(ChiakiQuitReason)reason message:(NSString *)message generation:(NSUInteger)generation;
@end

@implementation CKSession
{
	ChiakiLog _log;
	CKSessionCore *_core;
	/** Counts the starts, so news from a previous run that arrives late is dropped. */
	NSUInteger _generation;
	/** _generation as the core's threads read it. */
	volatile NSUInteger _coreGeneration;
	dispatch_source_t _metricsTimer;
}

// The core's threads: copy what is needed and hop to the main thread. A
// session being freed joins these threads first, and a weak reference taken
// from an object in dealloc is nil, so a late block finds nothing to call.

static void core_state_cb(CKSessionCoreState state, void *user)
{
	CKSession *session = (__bridge CKSession *)user;
	__weak CKSession *weak = session;
	NSUInteger generation = session->_coreGeneration;
	dispatch_async(dispatch_get_main_queue(), ^{
		[weak coreChangedState:(CKSessionState)state generation:generation];
	});
}

static void core_quit_cb(ChiakiQuitReason reason, const char *reason_str, void *user)
{
	CKSession *session = (__bridge CKSession *)user;
	__weak CKSession *weak = session;
	NSUInteger generation = session->_coreGeneration;
	NSString *message = reason_str ? [NSString stringWithUTF8String:reason_str] : nil;
	dispatch_async(dispatch_get_main_queue(), ^{
		[weak coreQuitWithReason:reason message:message generation:generation];
	});
}

- (instancetype)initWithHost:(NSString *)host
              registeredHost:(const ChiakiRegisteredHost *)registeredHost
                    settings:(CKSettings)settings
{
	self = [super init];
	if(!self)
		return nil;
	// CK_LOG_VERBOSE=1 in the environment shows everything the core says.
	const char *verbose = getenv("CK_LOG_VERBOSE");
	uint32_t mask = verbose && verbose[0] == '1' ? CHIAKI_LOG_ALL : CHIAKI_LOG_INFO | CHIAKI_LOG_WARNING | CHIAKI_LOG_ERROR;
	chiaki_log_init(&_log, mask, chiaki_log_cb_print, NULL);
	CKSessionCallbacks callbacks = { core_state_cb, core_quit_cb, (__bridge void *)self };
	_core = ck_session_new(&_log, host.UTF8String, registeredHost, &settings, &callbacks);
	if(!_core)
		return nil;
	_state = CKSessionStateIdle;
	return self;
}

- (void)dealloc
{
	[self stopMetrics];
	ck_session_free(_core);
}

- (void)setVideoSampleCallback:(ChiakiVideoSampleCallback)callback user:(void *)user
{
	ck_session_set_video_cb(_core, callback, user);
}

- (void)setAudioSink:(ChiakiAudioSink)sink
{
	ck_session_set_audio_sink(_core, &sink);
}

- (void)addInputSource:(CKInputSource)source
{
	if(ck_session_add_input_source(_core, &source) != CHIAKI_ERR_SUCCESS)
		CHIAKI_LOGE(&_log, "CKSession: no room for the input source %s", source.name ? source.name : "?");
}

- (void)removeAllInputSources
{
	ck_session_remove_all_input_sources(_core);
}

- (BOOL)start:(NSError **)error
{
	CKSessionCoreState core = ck_session_state(_core);
	ChiakiErrorCode err = CHIAKI_ERR_INVALID_DATA;
	if(core != CK_SESSION_CORE_CONNECTING && core != CK_SESSION_CORE_STREAMING)
	{
		// What a previous run still has on its way to the main thread is dropped from here on.
		_generation++;
		_coreGeneration = _generation;
		err = ck_session_start(_core);
	}
	if(err != CHIAKI_ERR_SUCCESS)
	{
		if(error)
			*error = [NSError errorWithDomain:CKSessionErrorDomain code:err
				userInfo:@{ NSLocalizedDescriptionKey: @(chiaki_error_string(err)) }];
		return NO;
	}
	[self changeState:CKSessionStateConnecting];
	return YES;
}

- (void)stop
{
	[self stopMetrics];
	ck_session_stop(_core);
	// The quit and STOPPED the core reported are on their way to the main thread.
}

// ---------------------------------------------------------------------------
// The main thread's half
// ---------------------------------------------------------------------------

- (void)changeState:(CKSessionState)state
{
	if(state == _state)
		return;
	self.state = state;
	if(state == CKSessionStateStreaming)
		[self startMetrics];
	else
		[self stopMetrics];
	[self.delegate session:self didChangeState:state];
}

- (void)coreChangedState:(CKSessionState)state generation:(NSUInteger)generation
{
	if(generation != _generation)
		return;
	if(state == CKSessionStateStopped)
	{
		// The console ended the stream: join the core's threads, which are finishing.
		[self stopMetrics];
		ck_session_stop(_core);
	}
	[self changeState:state];
}

- (void)coreQuitWithReason:(ChiakiQuitReason)reason message:(NSString *)message generation:(NSUInteger)generation
{
	if(generation != _generation)
		return;
	[self.delegate session:self didQuitWithReason:reason message:message];
}

- (void)startMetrics
{
	if(_metricsTimer || ![self.delegate respondsToSelector:@selector(session:didUpdateMetrics:)])
		return;
	CKSessionStats stats;
	ck_session_take_stats(_core, &stats);
	_metricsTimer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
	dispatch_source_set_timer(_metricsTimer, dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), NSEC_PER_SEC,
		NSEC_PER_SEC / 10);
	__weak CKSession *weak = self;
	dispatch_source_set_event_handler(_metricsTimer, ^{
		[weak reportMetrics];
	});
	dispatch_resume(_metricsTimer);
}

- (void)stopMetrics
{
	if(!_metricsTimer)
		return;
	dispatch_source_cancel(_metricsTimer);
	_metricsTimer = nil;
}

- (void)reportMetrics
{
	CKSessionStats stats;
	ck_session_take_stats(_core, &stats);
	double seconds = stats.elapsed_us / 1e6;
	if(seconds <= 0)
		return;
	CKMetrics metrics;
	memset(&metrics, 0, sizeof(metrics));
	metrics.fps_decoded = (float)(stats.video_frames_accepted / seconds);
	metrics.decode_ms_avg = stats.video_frames ? (float)(stats.video_callback_us / 1000.0 / stats.video_frames) : 0.0f;
	metrics.frames_lost = stats.video_frames_lost;
	metrics.audio_frames_concealed = stats.audio_frames_lost;
	metrics.bitrate_kbps = (uint32_t)((stats.video_bytes + stats.audio_bytes) * 8 / 1000.0 / seconds);
	metrics.resident_kb = ck_resident_kb();
	[self.delegate session:self didUpdateMetrics:metrics];
}

@end
