// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#import "CKHAppDelegate.h"
#import "CKHRootViewController.h"

@implementation CKHAppDelegate

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{
	self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
	self.window.rootViewController = [[CKHRootViewController alloc] init];
	[self.window makeKeyAndVisible];
	return YES;
}

@end
