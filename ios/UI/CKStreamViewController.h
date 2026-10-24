// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// The game screen: the console's video on the whole screen and nothing else.
// A three-finger tap opens the menu to leave. A6.1 shows a stand-in; A6.5
// brings the session (CKSession), the video (C2), the audio (C3), the
// controls (C4) and the metrics.

#import <UIKit/UIKit.h>

#import "CKConsoleService.h"

NS_ASSUME_NONNULL_BEGIN

@interface CKStreamViewController : UIViewController

- (instancetype)initWithService:(id<CKConsoleService>)service console:(CKConsoleItem *)console NS_DESIGNATED_INITIALIZER;
- (instancetype)initWithNibName:(nullable NSString *)nibName bundle:(nullable NSBundle *)bundle NS_UNAVAILABLE;
- (nullable instancetype)initWithCoder:(NSCoder *)coder NS_UNAVAILABLE;

@end

NS_ASSUME_NONNULL_END
