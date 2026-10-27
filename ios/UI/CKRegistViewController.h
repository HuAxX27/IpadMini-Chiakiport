// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Registering the iPad with a console: the PSN Account ID, the PIN the
// console shows, and what to fix when the console says no. A6.1 lays it out
// over the simulated consoles; A6.3 connects it to ck_regist.

#import <UIKit/UIKit.h>

#import "CKConsoleService.h"

NS_ASSUME_NONNULL_BEGIN

@interface CKRegistViewController : UITableViewController <UITextFieldDelegate>

- (instancetype)initWithService:(id<CKConsoleService>)service console:(CKConsoleItem *)console NS_DESIGNATED_INITIALIZER;
- (instancetype)initWithStyle:(UITableViewStyle)style NS_UNAVAILABLE;
- (instancetype)initWithNibName:(nullable NSString *)nibName bundle:(nullable NSBundle *)bundle NS_UNAVAILABLE;
- (nullable instancetype)initWithCoder:(NSCoder *)coder NS_UNAVAILABLE;

@end

NS_ASSUME_NONNULL_END
