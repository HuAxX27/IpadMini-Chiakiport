// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// CK Control's only screen (A5.4, A5.5). The touch controls cover it; under
// them, in the free middle, a text says what each source gives and what
// both together would send, and a button opens or closes a session with the
// registered PS5. The sources are set up the way the game screen (A6.5) will
// set them up: the DualSense through the default pad map, and the touch
// controls' view. Each one is wrapped in a tap that keeps a copy of its last
// poll for the text: while there is no session this screen polls them itself
// 30 times a second; during a session only CKSession's input thread does.
//
// It also writes what happens to /tmp/ckcontrol.log, so a test can be read
// over SSH: what each source holds when it changes, which of the checks of
// kCheckNames each one still lacks, and the session's state and rates.

#import "CKCViewController.h"

#import "Bridge/CKSession.h"
#import "CKTouchControlsView.h"

#include "Bridge/ck_registry.h"
#include "ck_ds5_source.h"
#include "ck_pad_map.h"

#include <pthread.h>
#include <stdatomic.h>

/** A source seen from outside: polls the inner one and keeps a copy of what it said. */
typedef struct ckc_tap_t
{
	CKInputSource inner;
	pthread_mutex_t mutex;
	bool has;
	ChiakiControllerState state;
	uint32_t polls;
} CKCTap;

static bool tap_poll(void *user, ChiakiControllerState *state)
{
	CKCTap *tap = user;
	bool has = tap->inner.poll && tap->inner.poll(tap->inner.user, state);
	pthread_mutex_lock(&tap->mutex);
	tap->has = has;
	if(has)
		tap->state = *state;
	tap->polls++;
	pthread_mutex_unlock(&tap->mutex);
	return has;
}

static CKInputSource tap_input(CKCTap *tap)
{
	CKInputSource input = { tap->inner.name, tap, tap_poll };
	return input;
}

static void tap_read(CKCTap *tap, bool *has, ChiakiControllerState *state, uint32_t *polls)
{
	pthread_mutex_lock(&tap->mutex);
	*has = tap->has;
	*state = tap->state;
	*polls = tap->polls;
	pthread_mutex_unlock(&tap->mutex);
}

/** Frames the PS5 sent, counted on the core's video thread. The picture itself is dropped. */
static atomic_uint frames_received;

static bool count_frame(uint8_t *buf, size_t buf_size, int32_t frames_lost, bool frame_recovered, void *user)
{
	(void)buf;
	(void)buf_size;
	(void)frames_lost;
	(void)frame_recovered;
	atomic_fetch_add_explicit((atomic_uint *)user, 1, memory_order_relaxed);
	return true;
}

/**
 * What the test asks of each source, one bit each: the 16 buttons by their
 * bit, each trigger pressed to the end, each stick to the end on each side
 * and a finger on the touchpad. The log says which ones each source has done.
 */
enum
{
	CKC_CHECK_L2 = 16,
	CKC_CHECK_R2,
	CKC_CHECK_AXES, // left x, left y, right x, right y; the negative side first
	CKC_CHECK_TOUCH = CKC_CHECK_AXES + 8,
	CKC_CHECK_COUNT
};

static NSString *const kCheckNames[CKC_CHECK_COUNT] = {
	@"✕", @"○", @"□", @"△", @"←", @"→", @"↑", @"↓", @"L1", @"R1", @"L3", @"R3", @"Opciones", @"Crear", @"Panel",
	@"PS", @"L2", @"R2", @"LX-", @"LX+", @"LY-", @"LY+", @"RX-", @"RX+", @"RY-", @"RY+", @"dedo",
};

static const uint32_t kChecksAll = (1u << CKC_CHECK_COUNT) - 1;

static uint32_t checks_held(bool has, const ChiakiControllerState *s)
{
	if(!has)
		return 0;
	uint32_t held = s->buttons & 0xffff;
	if(s->l2_state >= 250)
		held |= 1u << CKC_CHECK_L2;
	if(s->r2_state >= 250)
		held |= 1u << CKC_CHECK_R2;
	const int16_t axes[4] = { s->left_x, s->left_y, s->right_x, s->right_y };
	for(int i = 0; i < 4; i++)
	{
		if(axes[i] <= -30000)
			held |= 1u << (CKC_CHECK_AXES + 2 * i);
		if(axes[i] >= 30000)
			held |= 1u << (CKC_CHECK_AXES + 2 * i + 1);
	}
	for(int i = 0; i < CHIAKI_CONTROLLER_TOUCHES_MAX; i++)
	{
		if(s->touches[i].id >= 0)
			held |= 1u << CKC_CHECK_TOUCH;
	}
	return held;
}

static NSString *checks_text(uint32_t checks, NSString *none)
{
	NSMutableArray *names = [NSMutableArray array];
	for(int i = 0; i < CKC_CHECK_COUNT; i++)
	{
		if(checks & (1u << i))
			[names addObject:kCheckNames[i]];
	}
	return names.count ? [names componentsJoinedByString:@" "] : none;
}

static NSString *state_text(bool has, const ChiakiControllerState *s)
{
	if(!has)
		return @"nada";
	unsigned fingers = 0;
	for(int i = 0; i < CHIAKI_CONTROLLER_TOUCHES_MAX; i++)
		fingers += s->touches[i].id >= 0;
	return [NSString stringWithFormat:@"%@\n    LX %6d  LY %6d  RX %6d  RY %6d  L2 %3u  R2 %3u  dedos %u",
		checks_text(s->buttons & 0xffff, @"ninguno"), s->left_x, s->left_y, s->right_x, s->right_y, s->l2_state,
		s->r2_state, fingers];
}

static NSString *const kStateNames[] = { @"sin sesión", @"conectando", @"transmitiendo", @"detenida" };

/** Where the log goes. /tmp is the same for every user on a jailbroken iPad. */
#define CKC_LOG_PATH "/tmp/ckcontrol.log"

@interface CKCViewController () <CKSessionDelegate>
- (void)log:(NSString *)format, ... NS_FORMAT_FUNCTION(1, 2);
@end

@implementation CKCViewController
{
	CKTouchControlsView *_touch;
	UILabel *_info;
	UIButton *_connect;
	NSTimer *_timer;

	CKDS5Source _ds5;
	CKPadMapSource _pad;
	CKCTap _padTap;
	CKCTap _touchTap;

	BOOL _hasConsole;
	CKRegistryEntry _console;
	NSString *_consoleText;

	CKSession *_session;
	CKSessionState _sessionState;
	NSString *_note;

	CFAbsoluteTime _rateStart;
	uint32_t _rateStartPolls;
	unsigned _rateStartFrames;
	double _pollRate;
	double _fps;

	FILE *_log;
	CFAbsoluteTime _logStart;
	bool _logPadConnected;
	/** What each source held at the last tick, and what it has done so far: DualSense, touch. */
	uint32_t _held[2];
	uint32_t _done[2];
	unsigned _rateLines;
}

- (instancetype)initWithNibName:(NSString *)nibName bundle:(NSBundle *)bundle
{
	self = [super initWithNibName:nibName bundle:bundle];
	if(!self)
		return nil;
	pthread_mutex_init(&_padTap.mutex, NULL);
	pthread_mutex_init(&_touchTap.mutex, NULL);
	chiaki_controller_state_set_idle(&_padTap.state);
	chiaki_controller_state_set_idle(&_touchTap.state);
	ck_ds5_source_init(&_ds5, CK_DS5_SHARED_PATH);
	CKPadMap map;
	ck_pad_map_set_default(&map);
	_padTap.inner = ck_pad_map_source_input(&_pad, ck_ds5_source_input(&_ds5), &map);
	_sessionState = CKSessionStateIdle;
	_logStart = CFAbsoluteTimeGetCurrent();
	_log = fopen(CKC_LOG_PATH, "w");
	if(_log)
		setvbuf(_log, NULL, _IOLBF, 0);
	return self;
}

- (void)log:(NSString *)format, ...
{
	if(!_log)
		return;
	va_list args;
	va_start(args, format);
	NSString *line = [[NSString alloc] initWithFormat:format arguments:args];
	va_end(args);
	fprintf(_log, "%8.2f %s\n", CFAbsoluteTimeGetCurrent() - _logStart, line.UTF8String);
}

/** Logs a source's change: what it holds now and, when it did something new, what it still lacks. */
- (void)logSource:(int)index name:(NSString *)name held:(uint32_t)held state:(const ChiakiControllerState *)state
{
	if(held == _held[index])
		return;
	_held[index] = held;
	[self log:@"%@: %@ (LX %d LY %d RX %d RY %d L2 %u R2 %u)", name, checks_text(held, @"nada"), state->left_x,
		state->left_y, state->right_x, state->right_y, state->l2_state, state->r2_state];
	uint32_t done = _done[index] | held;
	if(done == _done[index])
		return;
	_done[index] = done;
	if(done == kChecksAll)
		[self log:@"%@: listo, hizo las %d pruebas", name, CKC_CHECK_COUNT];
	else
		[self log:@"%@: le faltan %d: %@", name, CKC_CHECK_COUNT - __builtin_popcount(done),
			checks_text(kChecksAll & ~done, @"")];
}

- (void)dealloc
{
	[[NSNotificationCenter defaultCenter] removeObserver:self];
	[_timer invalidate];
	[_session stop];
	ck_ds5_source_fini(&_ds5);
	if(_log)
		fclose(_log);
	pthread_mutex_destroy(&_padTap.mutex);
	pthread_mutex_destroy(&_touchTap.mutex);
}

- (void)loadConsole
{
	CKRegistry registry;
	if(ck_registry_load(&registry, CK_REGISTRY_DEFAULT_PATH) != CHIAKI_ERR_SUCCESS)
	{
		_consoleText = @"no pude leer los registros de ckctl";
		return;
	}
	if(registry.count == 0)
	{
		_consoleText = @"ninguna registrada (ckctl regist)";
		return;
	}
	_console = registry.entries[0];
	if(!_console.last_host[0])
	{
		_consoleText = @"registrada, pero sin IP (ckctl discover)";
		return;
	}
	_hasConsole = YES;
	NSString *name = [[NSString alloc] initWithBytes:_console.host.server_nickname
		length:strnlen(_console.host.server_nickname, sizeof(_console.host.server_nickname))
		encoding:NSUTF8StringEncoding];
	_consoleText = [NSString stringWithFormat:@"%@ en %s%@", name ?: @"?", _console.last_host,
		registry.count > 1 ? @" (la primera de las registradas)" : @""];
}

- (void)viewDidLoad
{
	[super viewDidLoad];
	self.view.backgroundColor = [UIColor blackColor];

	_info = [[UILabel alloc] initWithFrame:CGRectZero];
	_info.numberOfLines = 0;
	_info.textColor = [UIColor whiteColor];
	_info.font = [UIFont monospacedDigitSystemFontOfSize:14 weight:UIFontWeightRegular];
	[self.view addSubview:_info];

	_connect = [UIButton buttonWithType:UIButtonTypeSystem];
	_connect.titleLabel.font = [UIFont systemFontOfSize:22];
	_connect.tintColor = [UIColor whiteColor];
	_connect.layer.borderColor = [UIColor colorWithWhite:1.0 alpha:0.6].CGColor;
	_connect.layer.borderWidth = 1;
	_connect.layer.cornerRadius = 8;
	[_connect addTarget:self action:@selector(connectTapped:) forControlEvents:UIControlEventTouchUpInside];
	[self.view addSubview:_connect];

	// On top of everything: it only takes the touches that land on a control,
	// so the button under it still works.
	_touch = [[CKTouchControlsView alloc] initWithFrame:self.view.bounds];
	_touch.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
	[self.view addSubview:_touch];
	_touchTap.inner = _touch.inputSource;

	[self loadConsole];
	[self log:@"CK Control %@ (%s), pantalla %.0f x %.0f", [NSBundle mainBundle].infoDictionary[@"CFBundleShortVersionString"],
		CK_GIT_COMMIT, self.view.bounds.size.width, self.view.bounds.size.height];
	[self log:@"PS5: %@", _consoleText];
	[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(didEnterBackground:)
		name:UIApplicationDidEnterBackgroundNotification object:nil];
	[self updateButton];
	[self tick:nil];
}

- (void)viewDidAppear:(BOOL)animated
{
	[super viewDidAppear:animated];
	_rateStart = CFAbsoluteTimeGetCurrent();
	_timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 30.0 target:self selector:@selector(tick:)
		userInfo:nil repeats:YES];
}

- (void)viewWillDisappear:(BOOL)animated
{
	[super viewWillDisappear:animated];
	[_timer invalidate];
	_timer = nil;
}

- (void)viewDidLayoutSubviews
{
	[super viewDidLayoutSubviews];
	// The free middle of the touch layout at 1024 x 768: between the d-pad and
	// the face buttons, under PS and over the sticks.
	CGRect b = self.view.bounds;
	CGFloat width = 500;
	_info.frame = CGRectMake(floor((b.size.width - width) / 2), 204, width, 216);
	_connect.frame = CGRectMake(floor(b.size.width / 2 - 150), 428, 300, 52);
}

- (BOOL)prefersStatusBarHidden
{
	return YES;
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations
{
	return UIInterfaceOrientationMaskLandscape;
}

- (void)updateButton
{
	[_connect setTitle:_session ? @"Desconectar" : @"Conectar con la PS5" forState:UIControlStateNormal];
	_connect.enabled = _session || _hasConsole;
}

- (void)tick:(NSTimer *)timer
{
	(void)timer;
	// Only one thread polls a source at a time: this one, until a session starts.
	if(!_session)
	{
		ChiakiControllerState scratch;
		chiaki_controller_state_set_idle(&scratch);
		tap_poll(&_padTap, &scratch);
		chiaki_controller_state_set_idle(&scratch);
		tap_poll(&_touchTap, &scratch);
	}

	bool padHas, touchHas;
	ChiakiControllerState pad, touch;
	uint32_t padPolls, touchPolls;
	tap_read(&_padTap, &padHas, &pad, &padPolls);
	tap_read(&_touchTap, &touchHas, &touch, &touchPolls);
	(void)padPolls;

	bool padConnected = ck_ds5_source_connected(&_ds5);
	if(padConnected != _logPadConnected)
	{
		_logPadConnected = padConnected;
		[self log:padConnected ? @"DualSense conectado" : @"DualSense desconectado"];
	}
	[self logSource:0 name:@"DualSense" held:checks_held(padHas, &pad) state:&pad];
	[self logSource:1 name:@"Táctil" held:checks_held(touchHas, &touch) state:&touch];

	// What CKSession sends: the sources merged as contract C4 says.
	ChiakiControllerState merged;
	chiaki_controller_state_set_idle(&merged);
	if(padHas)
		chiaki_controller_state_or(&merged, &merged, &pad);
	if(touchHas)
		chiaki_controller_state_or(&merged, &merged, &touch);

	CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
	unsigned frames = atomic_load_explicit(&frames_received, memory_order_relaxed);
	if(now - _rateStart >= 1.0)
	{
		_pollRate = (touchPolls - _rateStartPolls) / (now - _rateStart);
		_fps = (frames - _rateStartFrames) / (now - _rateStart);
		_rateStart = now;
		_rateStartPolls = touchPolls;
		_rateStartFrames = frames;
		if(_session && ++_rateLines % 5 == 0)
			[self log:@"Sesión: %.0f cuadros/s, entrada %.0f consultas/s", _fps, _pollRate];
	}

	NSMutableString *text = [NSMutableString string];
	[text appendFormat:@"CK Control %@ (%s)\n",
		[NSBundle mainBundle].infoDictionary[@"CFBundleShortVersionString"], CK_GIT_COMMIT];
	[text appendFormat:@"PS5: %@\n", _consoleText];
	[text appendFormat:@"Sesión: %@", kStateNames[_sessionState]];
	if(_session)
		[text appendFormat:@" · %.0f cuadros/s · entrada %.0f consultas/s", _fps, _pollRate];
	[text appendString:@"\n"];
	if(_note)
		[text appendFormat:@"%@\n", _note];
	[text appendFormat:@"\nDualSense%@: %@\n", ck_ds5_source_connected(&_ds5) ? @"" : @" (no conectado)",
		state_text(padHas, &pad)];
	[text appendFormat:@"Táctil: %@\n", state_text(touchHas, &touch)];
	[text appendFormat:@"A la PS5: %@", state_text(padHas || touchHas, &merged)];
	_info.text = text;
}

- (void)connectTapped:(UIButton *)button
{
	(void)button;
	if(_session)
	{
		[self endSessionWithNote:@"Desconecté."];
		return;
	}
	if(!_hasConsole)
		return;
	CKSettings settings;
	ck_settings_set_default(&settings);
	CKSession *session = [[CKSession alloc] initWithHost:@(_console.last_host) registeredHost:&_console.host
		settings:settings];
	if(!session)
	{
		_note = @"No pude preparar la sesión.";
		[self log:@"%@", _note];
		return;
	}
	session.delegate = self;
	[session setVideoSampleCallback:count_frame user:&frames_received];
	[session addInputSource:tap_input(&_padTap)];
	[session addInputSource:tap_input(&_touchTap)];
	NSError *error = nil;
	if(![session start:&error])
	{
		_note = [NSString stringWithFormat:@"No pude abrir la sesión: %@", error.localizedDescription];
		[self log:@"%@", _note];
		return;
	}
	_session = session;
	_sessionState = session.state;
	_note = nil;
	_rateLines = 0;
	[self log:@"Conectando con %s", _console.last_host];
	[self updateButton];
}

/** Stops the session; from then on this screen polls the sources again. */
- (void)endSessionWithNote:(NSString *)note
{
	if(note)
	{
		_note = note;
		[self log:@"%@", note];
	}
	[_session stop];
	_session.delegate = nil;
	_session = nil;
	_sessionState = CKSessionStateIdle;
	[self updateButton];
}

- (void)session:(CKSession *)session didChangeState:(CKSessionState)state
{
	if(session != _session)
		return;
	_sessionState = state;
	[self log:@"Sesión: %@", kStateNames[state]];
	if(state == CKSessionStateStopped)
	{
		// Not from inside the session's own call.
		dispatch_async(dispatch_get_main_queue(), ^{
			if(session == self->_session)
				[self endSessionWithNote:nil];
		});
	}
}

- (void)session:(CKSession *)session didQuitWithReason:(ChiakiQuitReason)reason message:(NSString *)message
{
	if(session != _session)
		return;
	_note = [NSString stringWithFormat:@"Terminó: %s%@%@", chiaki_quit_reason_string(reason), message ? @": " : @"",
		message ?: @""];
	[self log:@"%@", _note];
	dispatch_async(dispatch_get_main_queue(), ^{
		if(session == self->_session)
			[self endSessionWithNote:nil];
	});
}

- (void)didEnterBackground:(NSNotification *)notification
{
	(void)notification;
	[_touch releaseAllControls];
	if(_session)
		[self endSessionWithNote:@"La app pasó a segundo plano: cerré la sesión."];
}

@end
