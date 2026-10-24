// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#import "CKVVideoView.h"

@implementation CKVVideoView

+ (Class)layerClass
{
	return [AVSampleBufferDisplayLayer class];
}

- (instancetype)initWithFrame:(CGRect)frame
{
	self = [super initWithFrame:frame];
	if(!self)
		return nil;
	self.backgroundColor = [UIColor blackColor];
	self.displayLayer.videoGravity = AVLayerVideoGravityResizeAspect;
	return self;
}

- (AVSampleBufferDisplayLayer *)displayLayer
{
	return (AVSampleBufferDisplayLayer *)self.layer;
}

@end
