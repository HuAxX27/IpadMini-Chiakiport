// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>

/**
 * A view whose layer is the AVSampleBufferDisplayLayer that CKVideoSink
 * draws into, as contract C2 asks of the game screen (A6.5): the picture
 * keeps its 16:9 shape inside the view, with black bars.
 */
@interface CKVVideoView : UIView

@property (nonatomic, readonly) AVSampleBufferDisplayLayer *displayLayer;

@end
