// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#import <UIKit/UIKit.h>

/**
 * The whole screen is the video, from a capture or from the PS5 live; a panel
 * on top shows what the sink counts every second and has the buttons. Tapping
 * the video hides or shows it.
 */
@interface CKVViewController : UIViewController

/** Stops whatever is playing, e.g. when the app leaves the foreground. */
- (void)stopAll;

@end
