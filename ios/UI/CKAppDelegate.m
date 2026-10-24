// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#import "CKAppDelegate.h"

#import "CKConsoleListViewController.h"
#import "CKLanConsoleService.h"
#import "CKMockConsoleService.h"
#import "CKTheme.h"

@implementation CKAppDelegate
{
	CKSettingsStore *_settings;
	CKConsoleListViewController *_list;
	CKLanConsoleService *_lanService;
	CKMockConsoleService *_mockService;
}

/** The consoles on the network, or the made-up ones when the settings ask for them. */
- (id<CKConsoleService>)currentService
{
	if(_settings.simulatedConsoles)
	{
		if(!_mockService)
			_mockService = [[CKMockConsoleService alloc] init];
		return _mockService;
	}
	if(!_lanService)
		_lanService = [[CKLanConsoleService alloc] init];
	return _lanService;
}

- (void)settingsChanged:(NSNotification *)notification
{
	_list.service = [self currentService];
}

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{
	_settings = [CKSettingsStore sharedStore];
	_list = [[CKConsoleListViewController alloc] initWithService:[self currentService] settings:_settings];
	[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(settingsChanged:)
		name:CKSettingsDidChangeNotification object:_settings];
	UINavigationController *navigation = [[UINavigationController alloc] initWithRootViewController:_list];
	UINavigationBar *bar = navigation.navigationBar;
	bar.barStyle = UIBarStyleBlack;
	bar.translucent = NO;
	bar.barTintColor = CKThemeCell();
	bar.tintColor = CKThemeAccent();
	bar.titleTextAttributes = @{ NSForegroundColorAttributeName: CKThemeText() };

	self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
	self.window.backgroundColor = CKThemeBackground();
	self.window.tintColor = CKThemeAccent();
	self.window.rootViewController = navigation;
	[self.window makeKeyAndVisible];
	return YES;
}

@end
