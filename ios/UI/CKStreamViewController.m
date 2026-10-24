// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#import "CKStreamViewController.h"

#import "CKTheme.h"

@implementation CKStreamViewController
{
	id<CKConsoleService> _service;
	CKConsoleItem *_console;
	UILabel *_placeholderLabel;
	UILabel *_hintLabel;
}

- (instancetype)initWithService:(id<CKConsoleService>)service console:(CKConsoleItem *)console
{
	self = [super initWithNibName:nil bundle:nil];
	if(!self)
		return nil;
	_service = service;
	_console = console;
	self.modalPresentationStyle = UIModalPresentationFullScreen;
	self.modalTransitionStyle = UIModalTransitionStyleCrossDissolve;
	return self;
}

- (void)viewDidLoad
{
	[super viewDidLoad];
	self.view.backgroundColor = [UIColor blackColor];
	self.view.multipleTouchEnabled = YES;

	_placeholderLabel = [[UILabel alloc] init];
	_placeholderLabel.textColor = CKThemeSecondaryText();
	_placeholderLabel.font = [UIFont systemFontOfSize:24];
	_placeholderLabel.textAlignment = NSTextAlignmentCenter;
	_placeholderLabel.numberOfLines = 0;
	_placeholderLabel.text = [NSString stringWithFormat:@"%@\n\nAquí va el video de la consola.\nLlega con la pantalla de "
		@"juego (A6.5).", _console.name];
	[self.view addSubview:_placeholderLabel];

	_hintLabel = [[UILabel alloc] init];
	_hintLabel.textColor = CKThemeSecondaryText();
	_hintLabel.font = [UIFont systemFontOfSize:17];
	_hintLabel.textAlignment = NSTextAlignmentCenter;
	_hintLabel.text = @"Toca con tres dedos para salir";
	[self.view addSubview:_hintLabel];

	UITapGestureRecognizer *menu = [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(menuGesture:)];
	menu.numberOfTouchesRequired = 3;
	[self.view addGestureRecognizer:menu];
}

- (void)viewDidLayoutSubviews
{
	[super viewDidLayoutSubviews];
	CGRect bounds = self.view.bounds;
	_placeholderLabel.frame = CGRectInset(bounds, 80, 160);
	_hintLabel.frame = CGRectMake(0, bounds.size.height - 64, bounds.size.width, 32);
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

- (void)menuGesture:(UITapGestureRecognizer *)gesture
{
	if(self.presentedViewController)
		return;
	UIAlertController *menu = [UIAlertController alertControllerWithTitle:_console.name message:nil
		preferredStyle:UIAlertControllerStyleAlert];
	[menu addAction:[UIAlertAction actionWithTitle:@"Seguir jugando" style:UIAlertActionStyleCancel handler:nil]];
	__weak CKStreamViewController *weak = self;
	[menu addAction:[UIAlertAction actionWithTitle:@"Salir" style:UIAlertActionStyleDestructive
		handler:^(UIAlertAction *action) {
			[weak leave];
		}]];
	[self presentViewController:menu animated:YES completion:nil];
}

- (void)leave
{
	[self.presentingViewController dismissViewControllerAnimated:YES completion:nil];
}

@end
