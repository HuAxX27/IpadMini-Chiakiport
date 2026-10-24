// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// The game screen: the console's video on the whole screen and nothing else.
// A6.5 opens the session (CKSession) with the saved registration and the
// settings, plays with the DualSense connected to the iPad (C4, A5), asks
// again while the console is still closing the previous session, and says
// why a session ended. A three-finger tap opens the menu: leave, or show the
// metrics. The video (C2) and the audio (C3) plug in here when A3 and A4
// land. Simulated consoles open no session.

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
