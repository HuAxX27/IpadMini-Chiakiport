// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#import "CKVLiveSession.h"

#include "Bridge/ck_registry.h"
#include "ck_h264.h"

#include <chiaki/common.h>
#include <chiaki/session.h>

#include <mach/mach_time.h>
#include <string.h>

/** Right after a session closes, the PS5 answers "in use" for several seconds (A2.8). */
#define CKV_BUSY_RETRY_SECONDS 2
#define CKV_BUSY_RETRIES 10

/** What the core's video thread touches: the sink, and when the first frame decoded. */
typedef struct ckv_live_video_t
{
	CKVideoSink *sink;
	/** Written once by the video thread before first_frame_seen is set. */
	uint64_t first_frame_abs;
	uint32_t first_frame_seen;
} CKVLiveVideo;

// Contract C2 through the sink, noting when the first frame was decoded.
static bool live_video_cb(uint8_t *buf, size_t buf_size, int32_t frames_lost, bool frame_recovered, void *user)
{
	CKVLiveVideo *video = user;
	bool taken = ck_video_sink_sample_cb(buf, buf_size, frames_lost, frame_recovered, video->sink);
	if(taken && !__atomic_load_n(&video->first_frame_seen, __ATOMIC_RELAXED))
	{
		// The profile header alone is taken too: only a frame with slices counts.
		CKH264FrameInfo info;
		ck_h264_annexb_to_avcc(buf, buf_size, 0, NULL, 0, &info);
		if(info.nal_types & (CK_H264_NAL_BIT(CK_H264_NAL_SLICE) | CK_H264_NAL_BIT(CK_H264_NAL_IDR)))
		{
			video->first_frame_abs = mach_absolute_time();
			__atomic_store_n(&video->first_frame_seen, 1, __ATOMIC_RELEASE);
		}
	}
	return taken;
}

@interface CKVLiveSession () <CKSessionDelegate>
@end

@implementation CKVLiveSession
{
	CKVideoSink *_sink;
	ChiakiRegisteredHost _registeredHost;
	CKSettings _settings;
	CKSession *_session;
	CKVLiveVideo _video;
	/** Counts the starts and stops, so a retry planned before the last one is dropped. */
	NSUInteger _generation;
	int _busy;
	BOOL _streamed;
	/** When the current attempt asked for the session. */
	uint64_t _attemptAbs;
	mach_timebase_info_data_t _timebase;
}

- (instancetype)initWithSink:(CKVideoSink *)sink error:(NSString **)error
{
	self = [super init];
	if(!self)
		return nil;
	static ChiakiErrorCode lib_err;
	static dispatch_once_t once;
	dispatch_once(&once, ^{
		lib_err = chiaki_lib_init();
	});
	if(lib_err != CHIAKI_ERR_SUCCESS)
	{
		if(error)
			*error = [NSString stringWithFormat:@"No se pudo iniciar el core: %s.", chiaki_error_string(lib_err)];
		return nil;
	}

	CKRegistry registry;
	if(ck_registry_load(&registry, CK_REGISTRY_DEFAULT_PATH) != CHIAKI_ERR_SUCCESS)
	{
		if(error)
			*error = [NSString stringWithFormat:@"No pude leer los registros de %s.", CK_REGISTRY_DEFAULT_PATH];
		return nil;
	}
	if(!registry.count)
	{
		if(error)
			*error = @"No hay ninguna PS5 registrada: regístrala con ckctl regist.";
		return nil;
	}
	const CKRegistryEntry *entry = &registry.entries[0];
	if(!entry->last_host[0])
	{
		if(error)
			*error = @"No sé la IP de la PS5: búscala con ckctl discover.";
		return nil;
	}
	_registeredHost = entry->host;
	_host = @(entry->last_host);
	const char *nickname = entry->host.server_nickname;
	_consoleName = [[NSString alloc] initWithBytes:nickname length:strnlen(nickname, sizeof(entry->host.server_nickname))
										  encoding:NSUTF8StringEncoding] ?: @"PS5";
	if(registry.count > 1)
		printf("CKVLiveSession: hay %zu consolas registradas; uso la primera, %s\n", registry.count,
			_consoleName.UTF8String);
	memset(&registry, 0, sizeof(registry));

	ck_settings_set_default(&_settings);
	_sink = sink;
	_video.sink = sink;
	mach_timebase_info(&_timebase);
	return self;
}

- (void)dealloc
{
	[self stop];
	memset(&_registeredHost, 0, sizeof(_registeredHost));
}

- (BOOL)streaming
{
	return _session.state == CKSessionStateStreaming;
}

- (double)firstFrameMs
{
	if(!__atomic_load_n(&_video.first_frame_seen, __ATOMIC_ACQUIRE))
		return 0.0;
	uint64_t ticks = _video.first_frame_abs - _attemptAbs;
	return (double)ticks * _timebase.numer / _timebase.denom / 1e6;
}

- (void)say:(NSString *)status
{
	printf("%s\n", status.UTF8String);
	[self.delegate liveSession:self didSay:status];
}

- (void)endSaying:(NSString *)status
{
	[self say:status];
	[self.delegate liveSessionDidEnd:self];
}

- (void)start
{
	[self stop];
	_generation++;
	_busy = 0;
	_streamed = NO;
	[self attempt];
}

- (void)attempt
{
	// Nothing reaches the sink between sessions: it starts clean.
	ck_video_sink_reset(_sink);
	__atomic_store_n(&_video.first_frame_seen, 0, __ATOMIC_RELAXED);
	_session = [[CKSession alloc] initWithHost:_host registeredHost:&_registeredHost settings:_settings];
	if(!_session)
	{
		[self endSaying:@"No pude crear la sesión."];
		return;
	}
	_session.delegate = self;
	[_session setVideoSampleCallback:live_video_cb user:&_video];
	_attemptAbs = mach_absolute_time();
	NSError *error = nil;
	if(![_session start:&error])
	{
		_session = nil;
		[self endSaying:[NSString stringWithFormat:@"No arrancó la sesión: %@.", error.localizedDescription]];
		return;
	}
	[self say:[NSString stringWithFormat:@"Conectando con %@ en %@%@…", _consoleName, _host,
		_busy ? [NSString stringWithFormat:@" (intento %d)", _busy + 1] : @""]];
}

- (void)stop
{
	_generation++;
	if(!_session)
		return;
	[_session stop];
	_session.delegate = nil;
	_session = nil;
}

// CKSessionDelegate, on the main thread.

- (void)session:(CKSession *)session didChangeState:(CKSessionState)state
{
	if(session != _session || state != CKSessionStateStreaming)
		return;
	_streamed = YES;
	[self say:[NSString stringWithFormat:@"Transmitiendo desde %@ en %@, 540p a 30 cuadros/s.", _consoleName, _host]];
}

- (void)session:(CKSession *)session didQuitWithReason:(ChiakiQuitReason)reason message:(NSString *)message
{
	if(session != _session)
		return;
	[_session stop];
	_session.delegate = nil;
	_session = nil;
	if(!_streamed && reason == CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_IN_USE && _busy < CKV_BUSY_RETRIES)
	{
		_busy++;
		[self say:[NSString stringWithFormat:@"La PS5 sigue ocupada con la sesión anterior; otro intento en %d s.",
			CKV_BUSY_RETRY_SECONDS]];
		NSUInteger generation = _generation;
		__weak CKVLiveSession *weak = self;
		dispatch_after(dispatch_time(DISPATCH_TIME_NOW, CKV_BUSY_RETRY_SECONDS * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
			CKVLiveSession *live = weak;
			if(live && live->_generation == generation)
				[live attempt];
		});
		return;
	}
	[self endSaying:[NSString stringWithFormat:@"La sesión terminó: %s%@%@.", chiaki_quit_reason_string(reason),
		message ? @": " : @"", message ?: @""]];
}

- (void)session:(CKSession *)session didUpdateMetrics:(CKMetrics)metrics
{
	if(session == _session)
		[self.delegate liveSession:self didUpdateMetrics:metrics];
}

@end
