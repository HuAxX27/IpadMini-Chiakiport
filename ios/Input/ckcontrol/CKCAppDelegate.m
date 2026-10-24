// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#import "CKCAppDelegate.h"
#import "CKCViewController.h"

@implementation CKCAppDelegate

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{
	(void)launchOptions;
	// Playing with the DualSense alone never touches the screen: keep it from dimming and locking.
	application.idleTimerDisabled = YES;
	self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
	self.window.rootViewController = [[CKCViewController alloc] init];
	[self.window makeKeyAndVisible];
	return YES;
}

@end
