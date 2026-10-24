// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#import "CKVViewController.h"

#import <AVFoundation/AVFoundation.h>

#import "CKVCapturePlayer.h"
#import "CKVLiveSession.h"
#import "CKVVideoView.h"

#include "CKVideoSink.h"
#include "ck_video_sink_stats.h"

#include <mach/mach.h>

/** Where to look for a capture: next to the consoles.plist, then where the A2.8 test left it. */
static NSArray<NSString *> *CKVCapturePaths(void)
{
	return @[ @"/var/mobile/Library/Chiakiport/captura.h264", @"/var/mobile/captura.h264" ];
}

static uint32_t CKVResidentKB(void)
{
	struct task_basic_info info;
	mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;
	if(task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&info, &count) != KERN_SUCCESS)
		return 0;
	return (uint32_t)(info.resident_size / 1024);
}

@interface CKVViewController () <CKVLiveSessionDelegate>
@end

@implementation CKVViewController
{
	CKVVideoView *_videoView;
	CKVideoSink *_sink;
	UIView *_panel;
	UILabel *_statusLabel;
	UILabel *_statsLabel;
	CKVCapturePlayer *_player;
	/** A3.4: the PS5 live, instead of the capture. */
	CKVLiveSession *_live;
	CKMetrics _metrics;
	BOOL _haveMetrics;
	BOOL _firstFrameLogged;
	NSTimer *_timer;
	NSUInteger _seconds;
	/** Since the start of playback. */
	CKVideoSinkStats _total;
	uint32_t _startKB;
	uint32_t _maxKB;
}

- (void)dealloc
{
	[self stopAll];
	ck_video_sink_free(_sink);
}

- (BOOL)prefersStatusBarHidden
{
	return YES;
}

- (UIButton *)addButton:(NSString *)title action:(SEL)action frame:(CGRect)frame
{
	UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
	button.frame = frame;
	button.titleLabel.font = [UIFont boldSystemFontOfSize:20];
	button.backgroundColor = [UIColor colorWithWhite:1.0 alpha:0.15];
	button.layer.cornerRadius = 8;
	[button setTitle:title forState:UIControlStateNormal];
	[button setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
	[button addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];
	[_panel addSubview:button];
	return button;
}

- (UILabel *)addLabelWithFrame:(CGRect)frame font:(UIFont *)font
{
	UILabel *label = [[UILabel alloc] initWithFrame:frame];
	label.font = font;
	label.textColor = [UIColor whiteColor];
	label.numberOfLines = 0;
	[_panel addSubview:label];
	return label;
}

- (void)viewDidLoad
{
	[super viewDidLoad];
	self.view.backgroundColor = [UIColor blackColor];

	_videoView = [[CKVVideoView alloc] initWithFrame:self.view.bounds];
	_videoView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
	[self.view addSubview:_videoView];
	[_videoView addGestureRecognizer:[[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(togglePanel)]];
	_sink = ck_video_sink_new((__bridge void *)_videoView.displayLayer);

	_panel = [[UIView alloc] initWithFrame:CGRectMake(16, 16, 600, 420)];
	_panel.backgroundColor = [UIColor colorWithWhite:0.0 alpha:0.65];
	_panel.layer.cornerRadius = 12;
	[self.view addSubview:_panel];

	UIFont *mono = [UIFont fontWithName:@"Menlo-Regular" size:15] ?: [UIFont systemFontOfSize:15];
	UILabel *title = [self addLabelWithFrame:CGRectMake(16, 10, 568, 28) font:[UIFont boldSystemFontOfSize:22]];
	title.text = [NSString stringWithFormat:@"CK Video %@ (%s)",
		[NSBundle mainBundle].infoDictionary[@"CFBundleShortVersionString"], CK_GIT_COMMIT];
	_statusLabel = [self addLabelWithFrame:CGRectMake(16, 40, 568, 48) font:[UIFont systemFontOfSize:16]];
	_statsLabel = [self addLabelWithFrame:CGRectMake(16, 88, 568, 214) font:mono];
	[self addButton:@"Reproducir la captura" action:@selector(playCapture) frame:CGRectMake(16, 308, 250, 44)];
	[self addButton:@"Conectar a la PS5" action:@selector(connectToConsole) frame:CGRectMake(276, 308, 200, 44)];
	[self addButton:@"Detener" action:@selector(stopAll) frame:CGRectMake(486, 308, 98, 44)];
	// A3.3: the failures the sink must recover from, on the next frame.
	[self addButton:@"Simular pérdida" action:@selector(simulateLostFrame) frame:CGRectMake(16, 362, 250, 44)];
	[self addButton:@"Simular decodificador roto" action:@selector(simulateBrokenDecoder)
			  frame:CGRectMake(276, 362, 308, 44)];

	_statusLabel.text = _sink ? @"Toca el video para ocultar o mostrar este panel."
		: @"No pude crear el sumidero de video.";
}

- (void)togglePanel
{
	_panel.hidden = !_panel.hidden;
}

- (void)simulateLostFrame
{
	if(!_sink)
		return;
	ck_video_sink_simulate(_sink, CK_VIDEO_SIMULATE_LOST_FRAME);
	printf("%lu s: simulo un cuadro perdido\n", (unsigned long)_seconds);
}

- (void)simulateBrokenDecoder
{
	if(!_sink)
		return;
	ck_video_sink_simulate(_sink, CK_VIDEO_SIMULATE_BROKEN_DECODER);
	printf("%lu s: simulo un decodificador roto\n", (unsigned long)_seconds);
}

- (void)playCapture
{
	[self stopAll];
	if(!_sink)
		return;
	NSString *error = nil;
	for(NSString *path in CKVCapturePaths())
	{
		if(![[NSFileManager defaultManager] fileExistsAtPath:path])
			continue;
		_player = [[CKVCapturePlayer alloc] initWithPath:path error:&error];
		break;
	}
	if(!_player)
	{
		_statusLabel.text = error ?: [NSString stringWithFormat:@"No encontré la captura en %@.",
			[CKVCapturePaths() componentsJoinedByString:@" ni en "]];
		printf("%s\n", _statusLabel.text.UTF8String);
		return;
	}
	_statusLabel.text = [NSString stringWithFormat:@"%@: %lu cuadros, %.1f MB, a 30 cuadros/s.", _player.path,
		(unsigned long)_player.frameCount, _player.byteCount / 1048576.0];
	printf("Reproduzco %s\n", _statusLabel.text.UTF8String);
	ck_video_sink_reset(_sink);
	[self startCounting];
	[_player startWithSink:_sink];
}

- (void)connectToConsole
{
	[self stopAll];
	if(!_sink)
		return;
	NSString *error = nil;
	_live = [[CKVLiveSession alloc] initWithSink:_sink error:&error];
	if(!_live)
	{
		_statusLabel.text = error;
		printf("%s\n", error.UTF8String);
		return;
	}
	_live.delegate = self;
	[self startCounting];
	[_live start];
}

/** Clears the counters and shows them every second from now on. */
- (void)startCounting
{
	CKVideoSinkStats discard;
	ck_video_sink_take_all_stats(_sink, &discard);
	memset(&_total, 0, sizeof(_total));
	_seconds = 0;
	_haveMetrics = NO;
	_firstFrameLogged = NO;
	_startKB = _maxKB = CKVResidentKB();
	[UIApplication sharedApplication].idleTimerDisabled = YES;
	_timer = [NSTimer scheduledTimerWithTimeInterval:1.0 target:self selector:@selector(tick) userInfo:nil repeats:YES];
}

- (void)stopAll
{
	[_timer invalidate];
	_timer = nil;
	if(_player || _live)
	{
		[_player stop];
		// Returns once the core's threads have joined: nothing reaches the sink after it.
		[_live stop];
		[self tick];
		printf("Detenido tras %lu s: %u decodificados (%.1f por segundo), %u fallidos, %u tarde (más de %d ms), "
			   "%.0f ms máximo entre cuadros, %u esperas de keyframe, memoria de %u a %u KB (máximo %u)\n",
			(unsigned long)_seconds, _total.core.frames_decoded,
			_seconds ? (double)_total.core.frames_decoded / _seconds : 0.0, _total.core.frames_failed,
			_total.core.frames_late, CK_VIDEO_LATE_US / 1000, _total.core.interval_us_max / 1000.0,
			_total.core.keyframe_waits, _startKB, CKVResidentKB(), _maxKB);
		_player = nil;
		_live = nil;
	}
	[UIApplication sharedApplication].idleTimerDisabled = NO;
}

// CKVLiveSessionDelegate

- (void)liveSession:(CKVLiveSession *)live didSay:(NSString *)status
{
	if(live == _live)
		_statusLabel.text = status;
}

- (void)liveSession:(CKVLiveSession *)live didUpdateMetrics:(CKMetrics)metrics
{
	if(live != _live)
		return;
	_metrics = metrics;
	_haveMetrics = YES;
}

- (void)liveSessionDidEnd:(CKVLiveSession *)live
{
	// Out of the session's own call before letting go of it; the status stays on screen.
	dispatch_async(dispatch_get_main_queue(), ^{
		if(live != self->_live)
			return;
		NSString *status = self->_statusLabel.text;
		[self stopAll];
		self->_statusLabel.text = status;
	});
}

- (void)tick
{
	CKVideoSinkStats s;
	ck_video_sink_take_all_stats(_sink, &s);
	_seconds++;
	_total.core.frames_decoded += s.core.frames_decoded;
	_total.core.frames_failed += s.core.frames_failed;
	_total.core.frames_skipped += s.core.frames_skipped;
	_total.core.decoder_broken += s.core.decoder_broken;
	_total.core.configures += s.core.configures;
	_total.core.frames_waiting += s.core.frames_waiting;
	_total.core.keyframe_waits += s.core.keyframe_waits;
	if(s.core.keyframe_wait_us_max > _total.core.keyframe_wait_us_max)
		_total.core.keyframe_wait_us_max = s.core.keyframe_wait_us_max;
	_total.frames_shown += s.frames_shown;
	_total.layer_failures += s.layer_failures;
	if(s.core.decode_us_max > _total.core.decode_us_max)
		_total.core.decode_us_max = s.core.decode_us_max;
	_total.core.frames_late += s.core.frames_late;
	if(s.core.interval_us_max > _total.core.interval_us_max)
		_total.core.interval_us_max = s.core.interval_us_max;
	uint32_t kb = CKVResidentKB();
	if(kb > _maxKB)
		_maxKB = kb;
	// Where the layer puts the picture: letterboxed to 16:9 in the 4:3 screen.
	CGRect shown = CGRectZero;
	if(s.visible_width && s.visible_height)
		shown = AVMakeRectWithAspectRatioInsideRect(CGSizeMake(s.visible_width, s.visible_height), _videoView.bounds);

	uint32_t decodes = s.core.frames_decoded + s.core.frames_failed;
	double avg_ms = decodes ? s.core.decode_us / 1000.0 / decodes : 0.0;
	double first_ms = _live.firstFrameMs;
	if(first_ms > 0 && !_firstFrameLogged)
	{
		_firstFrameLogged = YES;
		printf("%lu s: primer cuadro decodificado a los %.0f ms de pedir la sesión\n", (unsigned long)_seconds, first_ms);
	}
	// The capture's loop and the keyframes its player sent, or what the core says of the PS5's stream.
	NSString *source;
	if(_live)
		source = [NSString stringWithFormat:@"PS5: %.1f cuadros/s, %u kbps, %u perdidos;\n  primer cuadro %@",
			_metrics.fps_decoded, _metrics.bitrate_kbps, _metrics.frames_lost,
			first_ms > 0 ? [NSString stringWithFormat:@"a los %.0f ms", first_ms] : @"aún no"];
	else
		source = [NSString stringWithFormat:@"Captura: vuelta %lu, keyframes enviados: %lu",
			(unsigned long)_player.loops + 1, (unsigned long)_player.keyframesSent];
	NSString *now = [NSString stringWithFormat:
		@"Este segundo: %u decodificados, %u mostrados\n"
		@"Decodificar: %.1f ms promedio, %.1f ms máximo\n"
		@"En total (%lu s): %u decodificados, %u fallidos,\n"
		@"  %u omitidos, %u rechazados por la capa\n"
		@"Decodificadores creados: %u, rotos: %u\n"
		@"Keyframe: %u esperando, %u esperas, máx %.0f ms\n"
		@"Ritmo: %u tarde (> %d ms), máx %.0f ms entre cuadros\n"
		@"Imagen %ux%u, a la vista %ux%u, en %.0fx%.0f\n"
		@"%@\n"
		@"Memoria: %u KB (al empezar %u, máx %u)",
		s.core.frames_decoded, s.frames_shown, avg_ms, s.core.decode_us_max / 1000.0,
		(unsigned long)_seconds, _total.core.frames_decoded, _total.core.frames_failed, _total.core.frames_skipped,
		_total.layer_failures, _total.core.configures, _total.core.decoder_broken, _total.core.frames_waiting,
		_total.core.keyframe_waits, _total.core.keyframe_wait_us_max / 1000.0, _total.core.frames_late,
		CK_VIDEO_LATE_US / 1000, _total.core.interval_us_max / 1000.0, s.image_width, s.image_height,
		s.visible_width, s.visible_height, shown.size.width, shown.size.height, source, kb, _startKB, _maxKB];
	_statsLabel.text = now;
	if(s.core.keyframe_waits)
		printf("%lu s: volvió la imagen tras esperar el keyframe %.0f ms (%u cuadros esperando en este segundo)\n",
			(unsigned long)_seconds, s.core.keyframe_wait_us_max / 1000.0, s.core.frames_waiting);
	if(_seconds % 10 == 0)
		printf("%lu s: %u/s, %.1f ms (máx %.1f), total %u decodificados, %u fallidos, %u omitidos, %u esperando, "
			   "%u rotos, capa %u, %u tarde (máx %.0f ms entre cuadros), %u KB\n",
			(unsigned long)_seconds, s.core.frames_decoded, avg_ms, _total.core.decode_us_max / 1000.0,
			_total.core.frames_decoded, _total.core.frames_failed, _total.core.frames_skipped,
			_total.core.frames_waiting, _total.core.decoder_broken, _total.layer_failures, _total.core.frames_late,
			_total.core.interval_us_max / 1000.0, kb);
	if(_seconds % 10 == 0 && _live && _haveMetrics)
		printf("%lu s: la PS5 manda %.1f cuadros/s, %u kbps, %u perdidos en la red; %.1f ms en el callback\n",
			(unsigned long)_seconds, _metrics.fps_decoded, _metrics.bitrate_kbps, _metrics.frames_lost,
			_metrics.decode_ms_avg);
}

@end
