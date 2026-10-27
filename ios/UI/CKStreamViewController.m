// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#import "CKStreamViewController.h"

#import <AVFoundation/AVFoundation.h>
#import <QuartzCore/QuartzCore.h>

#import "Bridge/CKSession.h"
#import "CKTheme.h"
#import "Input/CKTouchControlsView.h"

#include "Input/ck_ds5_shared.h"
#include "Input/ck_ds5_source.h"
#include "Input/ck_pad_map.h"
#include "Video/CKVideoSink.h"
#include "ck_ui_stream.h"

#include <chiaki/common.h>

/** The console's picture, scaled to fit, on black (contract C2: the sink draws into its layer). */
@interface CKStreamVideoView : UIView
@end

@implementation CKStreamVideoView

+ (Class)layerClass
{
	return [AVSampleBufferDisplayLayer class];
}

- (instancetype)initWithFrame:(CGRect)frame
{
	self = [super initWithFrame:frame];
	if(!self)
		return nil;
	self.backgroundColor = [UIColor blackColor];
	self.userInteractionEnabled = NO;
	((AVSampleBufferDisplayLayer *)self.layer).videoGravity = AVLayerVideoGravityResizeAspect;
	return self;
}

@end

@interface CKStreamViewController () <CKSessionDelegate, UIGestureRecognizerDelegate>
@end

/** When the last session that streamed ended, by CACurrentMediaTime(); negative before any. */
static CFTimeInterval CKLastSessionEnd = -1;

@implementation CKStreamViewController
{
	id<CKConsoleService> _service;
	CKConsoleItem *_console;
	CKSettings _settings;
	CKSession *_session;
	/** What the session's threads use: alive until the session is stopped. */
	CKDS5Source _ds5;
	CKPadMapSource _pad;
	CKVideoSink *_sink;
	CKStreamVideoView *_videoView;
	CKTouchControlsView *_touchControls;
	/** The session reached streaming since its last start. */
	BOOL _streamed;
	/** Starts again after "already in use", since the screen opened or the user asked again. */
	unsigned _retries;
	/** The session ended and the screen says why. */
	BOOL _ended;
	/** Leaving: the session's end is ours, nothing to show. */
	BOOL _leaving;
	BOOL _opened;
	BOOL _showMetrics;
	UIActivityIndicatorView *_spinner;
	UILabel *_statusLabel;
	UIButton *_retryButton;
	UIButton *_backButton;
	UILabel *_metricsLabel;
	UILabel *_hintLabel;
}

- (instancetype)initWithService:(id<CKConsoleService>)service
                        console:(CKConsoleItem *)console
                       settings:(CKSettings)settings
{
	self = [super initWithNibName:nil bundle:nil];
	if(!self)
		return nil;
	_service = service;
	_console = console;
	_settings = settings;
	self.modalPresentationStyle = UIModalPresentationFullScreen;
	self.modalTransitionStyle = UIModalTransitionStyleCrossDissolve;
	return self;
}

- (void)dealloc
{
	[[NSNotificationCenter defaultCenter] removeObserver:self];
	[self closeSession];
	// Only once the session's threads have joined: the video thread feeds it.
	if(_sink)
		ck_video_sink_free(_sink);
}

- (UILabel *)labelWithSize:(CGFloat)size
{
	UILabel *label = [[UILabel alloc] init];
	label.textColor = CKThemeSecondaryText();
	label.font = [UIFont systemFontOfSize:size];
	label.textAlignment = NSTextAlignmentCenter;
	label.numberOfLines = 0;
	[self.view addSubview:label];
	return label;
}

- (UIButton *)buttonWithTitle:(NSString *)title action:(SEL)action
{
	UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
	[button setTitle:title forState:UIControlStateNormal];
	button.titleLabel.font = [UIFont systemFontOfSize:22];
	button.tintColor = CKThemeAccent();
	button.hidden = YES;
	[button addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];
	[self.view addSubview:button];
	return button;
}

- (void)viewDidLoad
{
	[super viewDidLoad];
	self.view.backgroundColor = [UIColor blackColor];
	self.view.multipleTouchEnabled = YES;

	// From the back: the picture, the touch controls, then what the screen says.
	_videoView = [[CKStreamVideoView alloc] initWithFrame:self.view.bounds];
	_videoView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
	[self.view addSubview:_videoView];
	if(!_service.simulated)
		_sink = ck_video_sink_new((__bridge void *)_videoView.layer);
	// Shown while streaming with no DualSense on the iPad; they take only the touches on a control.
	_touchControls = [[CKTouchControlsView alloc] initWithFrame:self.view.bounds];
	_touchControls.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
	_touchControls.hidden = YES;
	[self.view addSubview:_touchControls];

	_spinner = [[UIActivityIndicatorView alloc] initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleWhiteLarge];
	_spinner.hidesWhenStopped = YES;
	[self.view addSubview:_spinner];
	_statusLabel = [self labelWithSize:24];
	_retryButton = [self buttonWithTitle:@"Volver a intentar" action:@selector(retryTapped)];
	_backButton = [self buttonWithTitle:@"Volver a la lista" action:@selector(leave)];
	_hintLabel = [self labelWithSize:17];
	_hintLabel.text = @"Toca con tres dedos para abrir el menú";

	_metricsLabel = [self labelWithSize:14];
	_metricsLabel.textColor = CKThemeText();
	_metricsLabel.textAlignment = NSTextAlignmentLeft;
	_metricsLabel.numberOfLines = 1;
	_metricsLabel.backgroundColor = [UIColor colorWithWhite:0 alpha:0.6];
	_metricsLabel.hidden = YES;

	UITapGestureRecognizer *menu = [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(menuGesture:)];
	menu.numberOfTouchesRequired = 3;
	menu.delegate = self;
	[self.view addGestureRecognizer:menu];

	if(_service.simulated)
		[self showStatus:[NSString stringWithFormat:@"%@\n\nConsola simulada: aquí iría su video.", _console.name]
			busy:NO];
	[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(appDidEnterBackground:)
		name:UIApplicationDidEnterBackgroundNotification object:nil];
}

- (void)viewDidLayoutSubviews
{
	[super viewDidLayoutSubviews];
	CGRect bounds = self.view.bounds;
	CGFloat width = bounds.size.width;
	CGFloat height = bounds.size.height;
	_spinner.center = CGPointMake(width / 2, height / 2 - 120);
	_statusLabel.frame = CGRectMake(80, height / 2 - 90, width - 160, 180);
	_retryButton.frame = CGRectMake(width / 2 - 230, height / 2 + 110, 220, 48);
	_backButton.frame = CGRectMake(width / 2 + 10, height / 2 + 110, 220, 48);
	_hintLabel.frame = CGRectMake(0, height - 64, width, 32);
	_metricsLabel.frame = CGRectMake(0, 0, width, 24);
}

- (void)viewDidAppear:(BOOL)animated
{
	[super viewDidAppear:animated];
	// No dimming or locking while playing with the pad alone.
	[UIApplication sharedApplication].idleTimerDisabled = YES;
	// The hint fades once it has been read.
	[UIView animateWithDuration:1.0 delay:4.0 options:0 animations:^{
		self->_hintLabel.alpha = 0;
	} completion:nil];
	if(_opened || _service.simulated)
		return;
	_opened = YES;
	[self openSession];
}

- (void)viewWillDisappear:(BOOL)animated
{
	[super viewWillDisappear:animated];
	[UIApplication sharedApplication].idleTimerDisabled = NO;
}

- (BOOL)prefersStatusBarHidden
{
	return YES;
}

// ---------------------------------------------------------------------------
// What the screen says
// ---------------------------------------------------------------------------

- (void)showStatus:(NSString *)status busy:(BOOL)busy
{
	_statusLabel.text = status;
	if(busy)
		[_spinner startAnimating];
	else
		[_spinner stopAnimating];
	_retryButton.hidden = !_ended;
	_backButton.hidden = !_ended;
}

- (void)showEnd:(NSString *)reason
{
	[self closeSession];
	_ended = YES;
	// The last picture would stay behind the words.
	_videoView.hidden = YES;
	_touchControls.hidden = YES;
	[self showStatus:reason busy:NO];
}

- (void)showStreaming
{
	[self showStatus:@"" busy:NO];
	// Polled by the session's input thread; read here once a second, with the metrics. Hidden, the
	// touch controls press nothing.
	_touchControls.hidden = ck_ds5_source_connected(&_ds5);
}

// ---------------------------------------------------------------------------
// The session
// ---------------------------------------------------------------------------

- (void)openSession
{
	_ended = NO;
	_retries = 0;
	static ChiakiErrorCode lib_err;
	static dispatch_once_t once;
	dispatch_once(&once, ^{
		lib_err = chiaki_lib_init();
	});
	if(lib_err != CHIAKI_ERR_SUCCESS)
	{
		[self showEnd:[NSString stringWithFormat:@"No pude iniciar el core (%s).", chiaki_error_string(lib_err)]];
		return;
	}
	if(!_sink)
	{
		[self showEnd:@"No pude preparar el video."];
		return;
	}
	ChiakiRegisteredHost host;
	if(![_service getRegisteredHost:&host forConsole:_console])
	{
		[self showEnd:@"Esta consola no está registrada en este iPad."];
		return;
	}
	NSString *address = _console.address;
	if(!address.length)
	{
		[self showEnd:@"No sé la IP de la consola. Vuelve a la lista y despiértala."];
		return;
	}
	_session = [[CKSession alloc] initWithHost:address registeredHost:&host settings:_settings];
	memset(&host, 0, sizeof(host));
	_session.delegate = self;
	[_session setVideoSampleCallback:ck_video_sink_sample_cb user:_sink];
	ck_ds5_source_init(&_ds5, CK_DS5_SHARED_PATH);
	CKPadMap map;
	ck_pad_map_set_default(&map);
	[_session addInputSource:ck_pad_map_source_input(&_pad, ck_ds5_source_input(&_ds5), &map)];
	[_session addInputSource:_touchControls.inputSource];
	[self startSession];
}

- (void)startSession
{
	_streamed = NO;
	// Joins what is left of a refused attempt, so nothing feeds the sink while it starts clean.
	[_session stop];
	ck_video_sink_reset(_sink);
	_videoView.hidden = NO;
	[self showStatus:[NSString stringWithFormat:@"Conectando con %@…", _console.name] busy:YES];
	NSError *error;
	if(![_session start:&error])
		[self showEnd:[NSString stringWithFormat:@"No pude abrir la sesión (%@).", error.localizedDescription]];
}

/** Stops the session, which joins its threads, and only then lets go of what they read. */
- (void)closeSession
{
	if(!_session)
		return;
	// The console takes a while to close it: the next start may be refused at first.
	if(_streamed)
		CKLastSessionEnd = CACurrentMediaTime();
	_session.delegate = nil;
	[_session stop];
	[_session removeAllInputSources];
	_session = nil;
	ck_ds5_source_fini(&_ds5);
}

- (void)session:(CKSession *)session didChangeState:(CKSessionState)state
{
	if(session != _session)
		return;
	if(state == CKSessionStateStreaming)
	{
		_streamed = YES;
		[self showStreaming];
	}
}

- (void)session:(CKSession *)session didQuitWithReason:(ChiakiQuitReason)reason message:(NSString *)message
{
	if(session != _session || _leaving)
		return;
	double since = CKLastSessionEnd < 0 ? -1 : CACurrentMediaTime() - CKLastSessionEnd;
	if(ck_ui_session_should_retry(reason, _streamed, _retries, since))
	{
		_retries++;
		[self showStatus:[NSString stringWithFormat:@"La consola todavía está cerrando la sesión anterior. Pruebo otra "
			@"vez (%u de %d)…", _retries, CK_UI_SESSION_RETRY_MAX] busy:YES];
		__weak CKStreamViewController *weak = self;
		dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(CK_UI_SESSION_RETRY_SECONDS * NSEC_PER_SEC)),
			dispatch_get_main_queue(), ^{
				[weak retryWhileBusy:session];
			});
		return;
	}
	NSString *text = @(ck_ui_quit_reason_string(reason));
	// The core's own words, in English, for the report.
	if(message.length)
		text = [text stringByAppendingFormat:@"\n\n%@", message];
	[self showEnd:text];
}

- (void)retryWhileBusy:(CKSession *)session
{
	// Left or gave up meanwhile.
	if(session != _session || _leaving)
		return;
	[self startSession];
}

- (void)session:(CKSession *)session didUpdateMetrics:(CKMetrics)metrics
{
	if(session != _session)
		return;
	[self showStreaming];
	if(!_showMetrics)
		return;
	char line[CK_UI_METRICS_LINE_SIZE];
	ck_ui_metrics_line(&metrics, line, sizeof(line));
	_metricsLabel.text = [NSString stringWithFormat:@"  %s", line];
}

- (void)retryTapped
{
	[self openSession];
}

- (void)appDidEnterBackground:(NSNotification *)notification
{
	// iOS stops a backgrounded app within seconds: close the session cleanly first.
	if(!_session || _leaving)
		return;
	[self showEnd:@"La sesión se cerró al salir de la app."];
}

// ---------------------------------------------------------------------------
// The menu
// ---------------------------------------------------------------------------

- (BOOL)gestureRecognizer:(UIGestureRecognizer *)gesture shouldReceiveTouch:(UITouch *)touch
{
	// Three fingers on the touch controls are playing, not asking for the menu.
	return ![touch.view isDescendantOfView:_touchControls];
}

- (void)menuGesture:(UITapGestureRecognizer *)gesture
{
	if(self.presentedViewController)
		return;
	UIAlertController *menu = [UIAlertController alertControllerWithTitle:_console.name message:nil
		preferredStyle:UIAlertControllerStyleAlert];
	[menu addAction:[UIAlertAction actionWithTitle:@"Seguir jugando" style:UIAlertActionStyleCancel handler:nil]];
	__weak CKStreamViewController *weak = self;
	if(!_service.simulated)
		[menu addAction:[UIAlertAction actionWithTitle:_showMetrics ? @"Ocultar métricas" : @"Mostrar métricas"
			style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
				[weak toggleMetrics];
			}]];
	[menu addAction:[UIAlertAction actionWithTitle:@"Salir" style:UIAlertActionStyleDestructive
		handler:^(UIAlertAction *action) {
			[weak leave];
		}]];
	[self presentViewController:menu animated:YES completion:nil];
}

- (void)toggleMetrics
{
	_showMetrics = !_showMetrics;
	_metricsLabel.hidden = !_showMetrics;
	_metricsLabel.text = @"  Las métricas salen cada segundo mientras llega la sesión.";
}

- (void)leave
{
	if(_leaving)
		return;
	_leaving = YES;
	[self closeSession];
	[self.presentingViewController dismissViewControllerAnimated:YES completion:nil];
}

@end
