// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A6.4: the stream settings (CKSettings, contract C5) kept on the iPad, in the
// plist that ck_ui_settings.h describes, next to the saved registrations. It
// is read once at launch and written whole on every change, so the settings
// survive the app being closed and the iPad restarting. Main thread only.

#import <Foundation/Foundation.h>

#include "ck_ui_settings.h"

NS_ASSUME_NONNULL_BEGIN

/** Posted on the main thread after the settings change. */
extern NSString *const CKSettingsDidChangeNotification;

@interface CKSettingsStore : NSObject

/** The store at CK_UI_SETTINGS_PATH. */
+ (instancetype)sharedStore;

/** Reads path now: a missing or unreadable file gives C5's defaults. */
- (instancetype)initWithPath:(NSString *)path NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

@property (nonatomic, readonly, copy) NSString *path;

/** Always valid for the app (ck_ui_settings_valid()). */
@property (nonatomic, readonly) CKSettings settings;

/**
 * Uses settings from now on and writes them to the file. Returns NO, with
 * error, when the file could not be written; the settings still apply until
 * the app closes. Settings the app does not offer are refused.
 */
- (BOOL)updateSettings:(CKSettings)settings error:(NSError **)error;

@end

NS_ASSUME_NONNULL_END
