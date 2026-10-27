// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#import "CKConsoleListViewController.h"

#import "CKConsoleCell.h"
#import "CKRegistViewController.h"
#import "CKStreamViewController.h"
#import "CKTheme.h"

static NSString *const CKConsoleCellID = @"console";
/** A PS5 takes 10 to 20 s to turn on; past this, the wake-up did not work. */
static const NSTimeInterval CKWakeTimeout = 45;

@implementation CKConsoleListViewController
{
	id<CKConsoleService> _service;
	NSArray<CKConsoleItem *> *_consoles;
	/** hostID -> when the wake-up went out, until the console answers as on. */
	NSMutableDictionary<NSString *, NSDate *> *_waking;
	NSTimer *_wakeTimer;
	UILabel *_emptyLabel;
	UILabel *_footerLabel;
	BOOL _visible;
}

- (instancetype)initWithService:(id<CKConsoleService>)service
{
	self = [super initWithStyle:UITableViewStylePlain];
	if(!self)
		return nil;
	_service = service;
	_service.delegate = self;
	_consoles = service.consoles;
	_waking = [NSMutableDictionary dictionary];
	self.title = @"Consolas";
	return self;
}

- (void)dealloc
{
	[[NSNotificationCenter defaultCenter] removeObserver:self];
	[_wakeTimer invalidate];
}

- (void)viewDidLoad
{
	[super viewDidLoad];
	UITableView *table = self.tableView;
	table.backgroundColor = CKThemeBackground();
	table.separatorColor = CKThemeSeparator();
	table.rowHeight = [CKConsoleCell height];
	[table registerClass:[CKConsoleCell class] forCellReuseIdentifier:CKConsoleCellID];

	_emptyLabel = [[UILabel alloc] init];
	_emptyLabel.textColor = CKThemeSecondaryText();
	_emptyLabel.font = [UIFont systemFontOfSize:20];
	_emptyLabel.textAlignment = NSTextAlignmentCenter;
	_emptyLabel.numberOfLines = 0;
	_emptyLabel.text = @"Buscando consolas en la red…\n\nLa PS5 y el iPad tienen que estar en la misma red Wi-Fi.";

	UIView *footer = [[UIView alloc] initWithFrame:CGRectMake(0, 0, 100, 96)];
	_footerLabel = [[UILabel alloc] initWithFrame:CGRectInset(footer.bounds, 24, 12)];
	_footerLabel.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
	_footerLabel.textColor = CKThemeSecondaryText();
	_footerLabel.font = [UIFont systemFontOfSize:15];
	_footerLabel.numberOfLines = 0;
	[footer addSubview:_footerLabel];
	table.tableFooterView = footer;

	NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
	[center addObserver:self selector:@selector(appDidEnterBackground:) name:UIApplicationDidEnterBackgroundNotification
		object:nil];
	[center addObserver:self selector:@selector(appWillEnterForeground:) name:UIApplicationWillEnterForegroundNotification
		object:nil];
	[self updateFooter];
}

- (void)viewWillAppear:(BOOL)animated
{
	[super viewWillAppear:animated];
	_visible = YES;
	[_service start];
	[self consoleServiceDidChange:_service];
}

- (void)viewDidDisappear:(BOOL)animated
{
	[super viewDidDisappear:animated];
	_visible = NO;
	[_service stop];
}

- (void)appDidEnterBackground:(NSNotification *)notification
{
	[_service stop];
}

- (void)appWillEnterForeground:(NSNotification *)notification
{
	if(_visible)
		[_service start];
}

- (void)updateFooter
{
	NSDictionary *info = [NSBundle mainBundle].infoDictionary;
	NSMutableArray<NSString *> *lines = [NSMutableArray array];
	if(_service.simulated)
		[lines addObject:@"Consolas simuladas: nada de esta lista existe ni se guarda."];
	[lines addObject:[NSString stringWithFormat:@"Chiakiport %@ (%s)", info[@"CFBundleShortVersionString"], CK_GIT_COMMIT]];
	_footerLabel.text = [lines componentsJoinedByString:@"\n"];
}

// ---------------------------------------------------------------------------
// The list
// ---------------------------------------------------------------------------

- (void)consoleServiceDidChange:(id<CKConsoleService>)service
{
	_consoles = service.consoles;
	for(CKConsoleItem *console in _consoles)
		if(console.row.state == CK_CONSOLE_ROW_READY)
			[_waking removeObjectForKey:console.hostID];
	[self.tableView reloadData];
	self.tableView.backgroundView = _consoles.count ? nil : _emptyLabel;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section
{
	return (NSInteger)_consoles.count;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
	CKConsoleCell *cell = [tableView dequeueReusableCellWithIdentifier:CKConsoleCellID forIndexPath:indexPath];
	CKConsoleItem *console = _consoles[(NSUInteger)indexPath.row];
	[cell showConsole:console busyText:_waking[console.hostID] ? @"Despertando…" : nil];
	[cell.actionButton removeTarget:nil action:NULL forControlEvents:UIControlEventAllEvents];
	[cell.actionButton addTarget:self action:@selector(actionTapped:) forControlEvents:UIControlEventTouchUpInside];
	return cell;
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
	[tableView deselectRowAtIndexPath:indexPath animated:YES];
	[self runActionOf:_consoles[(NSUInteger)indexPath.row]];
}

- (void)actionTapped:(UIButton *)button
{
	CGPoint point = [button convertPoint:CGPointMake(CGRectGetMidX(button.bounds), CGRectGetMidY(button.bounds))
		toView:self.tableView];
	NSIndexPath *indexPath = [self.tableView indexPathForRowAtPoint:point];
	if(indexPath && (NSUInteger)indexPath.row < _consoles.count)
		[self runActionOf:_consoles[(NSUInteger)indexPath.row]];
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

- (void)runActionOf:(CKConsoleItem *)console
{
	if(_waking[console.hostID])
		return;
	switch(console.row.action)
	{
		case CK_CONSOLE_ACTION_REGISTER:
			[self.navigationController pushViewController:[[CKRegistViewController alloc] initWithService:_service
				console:console] animated:YES];
			break;
		case CK_CONSOLE_ACTION_WAKE:
			[self wake:console];
			break;
		case CK_CONSOLE_ACTION_PLAY:
			[self presentViewController:[[CKStreamViewController alloc] initWithService:_service console:console]
				animated:YES completion:nil];
			break;
		default:
			[self explainNoAction:console];
			break;
	}
}

- (void)explainNoAction:(CKConsoleItem *)console
{
	NSString *message;
	if(!console.row.registered)
		message = @"Para registrarla, enciende la consola. Después, en la PS5: Ajustes › Sistema › Remote Play › "
			@"Vincular dispositivo.";
	else if(console.row.state == CK_CONSOLE_ROW_MISSING)
		message = @"No contesta y no sé su IP. Enciéndela o revisa que esté en la misma red Wi-Fi que el iPad.";
	else
		message = @"La consola contestó con un estado que no conozco. Espera unos segundos.";
	[self showAlertWithTitle:console.name message:message];
}

- (void)wake:(CKConsoleItem *)console
{
	NSString *hostID = console.hostID;
	_waking[hostID] = [NSDate date];
	[self.tableView reloadData];
	[self startWakeTimer];
	__weak CKConsoleListViewController *weak = self;
	[_service wakeConsole:console completion:^(NSString *error) {
		if(error)
			[weak wakeOf:hostID failedWithMessage:error];
	}];
}

- (void)wakeOf:(NSString *)hostID failedWithMessage:(NSString *)message
{
	[_waking removeObjectForKey:hostID];
	[self.tableView reloadData];
	[self showAlertWithTitle:@"No pude despertar a la consola" message:message];
}

- (void)startWakeTimer
{
	if(_wakeTimer)
		return;
	_wakeTimer = [NSTimer scheduledTimerWithTimeInterval:1 target:self selector:@selector(checkWaking:) userInfo:nil
		repeats:YES];
}

- (void)checkWaking:(NSTimer *)timer
{
	for(NSString *hostID in [_waking allKeys])
		if(-[_waking[hostID] timeIntervalSinceNow] > CKWakeTimeout)
			[self wakeOf:hostID failedWithMessage:[NSString stringWithFormat:@"Pasaron %.0f s y sigue sin encenderse. "
				@"Revisa que esté conectada a la corriente y a la misma red Wi-Fi.", CKWakeTimeout]];
	if(!_waking.count)
	{
		[_wakeTimer invalidate];
		_wakeTimer = nil;
	}
}

- (void)showAlertWithTitle:(NSString *)title message:(NSString *)message
{
	UIAlertController *alert = [UIAlertController alertControllerWithTitle:title message:message
		preferredStyle:UIAlertControllerStyleAlert];
	[alert addAction:[UIAlertAction actionWithTitle:@"Entendido" style:UIAlertActionStyleDefault handler:nil]];
	[self presentViewController:alert animated:YES completion:nil];
}

@end
