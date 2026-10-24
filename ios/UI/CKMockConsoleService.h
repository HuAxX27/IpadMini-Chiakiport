// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A6.1: made-up consoles, to try every screen without a console. Four of
// them, one per kind of row: registered and on, registered in rest mode,
// on but not registered, and registered but not answering. Waking one up
// turns it on 5 s later; registering takes 2 s and works with any PIN but
// 00000000, which answers like a wrong PIN. Nothing is saved.

#import "CKConsoleService.h"

@interface CKMockConsoleService : NSObject <CKConsoleService>
@end
