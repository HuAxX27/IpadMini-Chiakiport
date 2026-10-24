// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A6.2: the consoles on the network. It searches with CKConsoles.h's browser
// (one search a second, by broadcast, the same as ckctl discover), reads the
// saved registrations from the C5 plist that ckctl also writes, and wakes
// registered consoles up. When a registered console answers from a new IP, it
// keeps that IP in the plist, as ckctl does. It never writes over a
// registration file it could not read.

#import "CKConsoleService.h"

NS_ASSUME_NONNULL_BEGIN

@interface CKLanConsoleService : NSObject <CKConsoleService>

/** The store at CK_REGISTRY_DEFAULT_PATH. */
- (instancetype)init;

- (instancetype)initWithRegistryPath:(NSString *)path NS_DESIGNATED_INITIALIZER;

@end

NS_ASSUME_NONNULL_END
