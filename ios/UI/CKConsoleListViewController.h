// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// The first screen: the consoles on the network and the registered ones,
// each with what a tap does (register, wake up or play). It searches only
// while it is on screen and the app is in front.

#import <UIKit/UIKit.h>

#import "CKConsoleService.h"

NS_ASSUME_NONNULL_BEGIN

@interface CKConsoleListViewController : UITableViewController <CKConsoleServiceDelegate>

- (instancetype)initWithService:(id<CKConsoleService>)service NS_DESIGNATED_INITIALIZER;
- (instancetype)initWithStyle:(UITableViewStyle)style NS_UNAVAILABLE;
- (instancetype)initWithNibName:(nullable NSString *)nibName bundle:(nullable NSBundle *)bundle NS_UNAVAILABLE;
- (nullable instancetype)initWithCoder:(NSCoder *)coder NS_UNAVAILABLE;

@end

NS_ASSUME_NONNULL_END
