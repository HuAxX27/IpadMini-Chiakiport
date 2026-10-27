// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#import "CKVAppDelegate.h"
#import "CKVViewController.h"

#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

/** Where the app writes what the sink and the core say, to read it over SSH. */
#define CKV_LOG_DIR "/var/mobile/Library/Chiakiport"
#define CKV_LOG_PATH CKV_LOG_DIR "/ckvideo.log"

@implementation CKVAppDelegate
{
	CKVViewController *_controller;
}

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{
	// An app's output goes nowhere on iOS 9: keep it in a file instead,
	// one per launch.
	mkdir(CKV_LOG_DIR, 0755);
	if(freopen(CKV_LOG_PATH, "w", stdout))
	{
		dup2(fileno(stdout), fileno(stderr));
		setvbuf(stdout, NULL, _IOLBF, 0);
		setvbuf(stderr, NULL, _IOLBF, 0);
	}
	NSDictionary *info = [NSBundle mainBundle].infoDictionary;
	printf("CK Video %s (%s) en iOS %s\n", [info[@"CFBundleShortVersionString"] UTF8String], CK_GIT_COMMIT,
		[UIDevice currentDevice].systemVersion.UTF8String);

	self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
	_controller = [[CKVViewController alloc] init];
	self.window.rootViewController = _controller;
	[self.window makeKeyAndVisible];
	return YES;
}

// VideoToolbox stops working in the background: stop before getting there.
- (void)applicationWillResignActive:(UIApplication *)application
{
	[_controller stopAll];
}

@end
