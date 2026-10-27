// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#import "CKAppDelegate.h"

#import "CKConsoleListViewController.h"
#import "CKMockConsoleService.h"
#import "CKTheme.h"

@implementation CKAppDelegate

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{
	id<CKConsoleService> service = [[CKMockConsoleService alloc] init];
	CKConsoleListViewController *list = [[CKConsoleListViewController alloc] initWithService:service];
	UINavigationController *navigation = [[UINavigationController alloc] initWithRootViewController:list];
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
