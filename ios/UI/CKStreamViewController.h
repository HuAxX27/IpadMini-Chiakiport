// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// The game screen: the console's video on the whole screen and nothing else.
// A6.5 opens the session (CKSession) with the saved registration and the
// settings, shows the video through CKVideoSink (C2, A3), plays with the
// DualSense connected to the iPad or the touch controls (C4, A5), asks again
// while the console is still closing the previous session, and says why a
// session ended. A three-finger tap off the touch controls opens the menu:
// leave, or show the metrics. The audio (C3) plugs in here when A4 lands.
// Simulated consoles open no session.

#import <UIKit/UIKit.h>

#import "CKConsoleService.h"

#include "Bridge/CKSettings.h"

NS_ASSUME_NONNULL_BEGIN

@interface CKStreamViewController : UIViewController

- (instancetype)initWithService:(id<CKConsoleService>)service
                        console:(CKConsoleItem *)console
                       settings:(CKSettings)settings NS_DESIGNATED_INITIALIZER;
- (instancetype)initWithNibName:(nullable NSString *)nibName bundle:(nullable NSBundle *)bundle NS_UNAVAILABLE;
- (nullable instancetype)initWithCoder:(NSCoder *)coder NS_UNAVAILABLE;

@end

NS_ASSUME_NONNULL_END
