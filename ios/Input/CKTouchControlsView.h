// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// The on-screen controls (A5.5) as a UIKit view: draws the layout of
// ck_touch_controls.h and hands it the touches. Put it over the picture,
// covering the whole screen, and add its inputSource to the CKSession.
//
// - It lays itself out in layoutSubviews from its bounds: sideways, upright
//   or any size.
// - Only the controls take touches: a touch anywhere else goes to the views
//   below it, so the app can keep its own taps and gestures there.
// - Hidden, out of a window or after -releaseAllControls, nothing is pressed
//   and its source returns false.
// - Its source points into the view: remove it from the session (or stop the
//   session) before the view goes away.
//
// Main thread only, like every UIView. Built with ARC.

#import <UIKit/UIKit.h>

#include "CKInputSource.h"

NS_ASSUME_NONNULL_BEGIN

@interface CKTouchControlsView : UIView

/** The source for -[CKSession addInputSource:], named "touch". */
@property (nonatomic, readonly) CKInputSource inputSource;

/** Lets go of every control, as when the view is hidden. */
- (void)releaseAllControls;

@end

NS_ASSUME_NONNULL_END
