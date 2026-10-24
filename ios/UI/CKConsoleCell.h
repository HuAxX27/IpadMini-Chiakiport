// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A row of the console list: the console's name, a colored state with its IP
// and game, and a button with the row's action.

#import <UIKit/UIKit.h>

#import "CKConsoleService.h"

NS_ASSUME_NONNULL_BEGIN

@interface CKConsoleCell : UITableViewCell

+ (CGFloat)height;

/** busyText replaces the button with a spinner and that text, e.g. while waking up. */
- (void)showConsole:(CKConsoleItem *)console busyText:(nullable NSString *)busyText;

/** The action button; the list adds its target. */
@property (nonatomic, readonly) UIButton *actionButton;

@end

NS_ASSUME_NONNULL_END
