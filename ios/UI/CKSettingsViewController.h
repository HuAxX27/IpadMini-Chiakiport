// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A6.4: the settings screen. Resolution, frame rate and bitrate of contract
// C5, as ck_ui_settings.h offers them; each change is saved right away and
// applies to the next session.

#import <UIKit/UIKit.h>

#import "CKSettingsStore.h"

NS_ASSUME_NONNULL_BEGIN

@interface CKSettingsViewController : UITableViewController

- (instancetype)initWithStore:(CKSettingsStore *)store NS_DESIGNATED_INITIALIZER;
- (instancetype)initWithStyle:(UITableViewStyle)style NS_UNAVAILABLE;
- (instancetype)initWithNibName:(nullable NSString *)nibName bundle:(nullable NSBundle *)bundle NS_UNAVAILABLE;
- (nullable instancetype)initWithCoder:(NSCoder *)coder NS_UNAVAILABLE;

@end

NS_ASSUME_NONNULL_END
