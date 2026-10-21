// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#import "CKHRootViewController.h"

#include <sys/sysctl.h>

#if defined(__arm64__)
#define CKH_ARCH "arm64"
#elif defined(__ARM_ARCH_7S__)
#define CKH_ARCH "armv7s"
#elif defined(__ARM_ARCH_7A__)
#define CKH_ARCH "armv7"
#else
#define CKH_ARCH "desconocida"
#endif

static NSString *CKHMachine(void)
{
	char machine[64] = {0};
	size_t size = sizeof(machine) - 1;
	if (sysctlbyname("hw.machine", machine, &size, NULL, 0) != 0)
		return @"?";
	return [NSString stringWithUTF8String:machine];
}

@implementation CKHRootViewController {
	UILabel *_titleLabel;
	UILabel *_infoLabel;
	UILabel *_liveLabel;
	NSTimer *_timer;
	NSDate *_launchDate;
	NSUInteger _taps;
}

- (UILabel *)addLabelWithFont:(UIFont *)font color:(UIColor *)color
{
	UILabel *label = [[UILabel alloc] initWithFrame:CGRectZero];
	label.font = font;
	label.textColor = color;
	label.textAlignment = NSTextAlignmentCenter;
	label.numberOfLines = 0;
	[self.view addSubview:label];
	return label;
}

- (void)viewDidLoad
{
	[super viewDidLoad];
	self.view.backgroundColor = [UIColor blackColor];

	_titleLabel = [self addLabelWithFont:[UIFont boldSystemFontOfSize:48] color:[UIColor whiteColor]];
	_titleLabel.text = @"Chiakiport: hola mundo";

	UIDevice *device = [UIDevice currentDevice];
	UIScreen *screen = [UIScreen mainScreen];
	NSProcessInfo *process = [NSProcessInfo processInfo];
	NSDictionary *bundle = [NSBundle mainBundle].infoDictionary;
	CGSize points = screen.bounds.size;
	_infoLabel = [self addLabelWithFont:[UIFont systemFontOfSize:22] color:[UIColor lightGrayColor]];
	_infoLabel.text = [NSString stringWithFormat:
		@"%@ %@ · %@ · %s\n"
		@"Pantalla %.0f × %.0f a %.0fx · %lu núcleos · %llu MB\n"
		@"CK Hola %@ (%s)",
		device.systemName, device.systemVersion, CKHMachine(), CKH_ARCH,
		MAX(points.width, points.height), MIN(points.width, points.height), screen.scale,
		(unsigned long)process.activeProcessorCount, process.physicalMemory / (1024 * 1024),
		bundle[@"CFBundleShortVersionString"], CK_GIT_COMMIT];

	_liveLabel = [self addLabelWithFont:[UIFont systemFontOfSize:30] color:[UIColor whiteColor]];

	[self.view addGestureRecognizer:[[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(tapped:)]];
	_launchDate = [NSDate date];
	[self updateLiveLabel];
}

- (void)viewDidAppear:(BOOL)animated
{
	[super viewDidAppear:animated];
	_timer = [NSTimer scheduledTimerWithTimeInterval:1.0 target:self selector:@selector(tick:) userInfo:nil repeats:YES];
}

- (void)viewWillDisappear:(BOOL)animated
{
	[super viewWillDisappear:animated];
	[_timer invalidate];
	_timer = nil;
}

- (void)viewDidLayoutSubviews
{
	[super viewDidLayoutSubviews];
	CGRect bounds = self.view.bounds;
	CGFloat width = bounds.size.width - 80;
	NSArray *labels = @[ _titleLabel, _infoLabel, _liveLabel ];
	CGFloat spacing = 32, total = -spacing;
	for (UILabel *label in labels)
		total += [label sizeThatFits:CGSizeMake(width, CGFLOAT_MAX)].height + spacing;
	CGFloat y = floor((bounds.size.height - total) / 2);
	for (UILabel *label in labels) {
		CGFloat height = [label sizeThatFits:CGSizeMake(width, CGFLOAT_MAX)].height;
		label.frame = CGRectMake(40, y, width, ceil(height));
		y += ceil(height) + spacing;
	}
}

- (BOOL)prefersStatusBarHidden
{
	return YES;
}

- (void)tick:(NSTimer *)timer
{
	[self updateLiveLabel];
}

- (void)tapped:(UITapGestureRecognizer *)recognizer
{
	_taps++;
	[self updateLiveLabel];
}

- (void)updateLiveLabel
{
	NSTimeInterval open = -[_launchDate timeIntervalSinceNow];
	_liveLabel.text = [NSString stringWithFormat:@"Abierta hace %.0f s · %lu toques", floor(open), (unsigned long)_taps];
	[self.view setNeedsLayout];
}

@end
