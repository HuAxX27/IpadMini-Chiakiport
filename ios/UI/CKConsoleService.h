// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// What the screens ask about consoles: the list, waking one up, registering
// and the saved registration a session needs. CKMockConsoleService answers
// with simulated consoles (A6.1); the one on the network comes with A6.2 and
// A6.3, on top of CKConsoles.h and ck_registry.h.

#import <Foundation/Foundation.h>

#include "ck_console_rows.h"

NS_ASSUME_NONNULL_BEGIN

/** One row of the console list: a copy of a CKConsoleRow that does not change. */
@interface CKConsoleItem : NSObject

- (instancetype)initWithRow:(const CKConsoleRow *)row NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

@property (nonatomic, readonly) CKConsoleRow row;
@property (nonatomic, readonly, copy) NSString *name;
@property (nonatomic, readonly, copy) NSString *address;
@property (nonatomic, readonly, copy) NSString *hostID;
@property (nonatomic, readonly, copy) NSString *runningGame;

@end

@protocol CKConsoleService;

@protocol CKConsoleServiceDelegate <NSObject>

/** The list changed. On the main thread. */
- (void)consoleServiceDidChange:(id<CKConsoleService>)service;

@end

/** nil when the wake-up went out, else what to tell the user. */
typedef void (^CKWakeCompletion)(NSString *_Nullable error);

/** How a registration ended, and a sentence for the user. */
typedef void (^CKRegistCompletion)(CKRegistResult result, NSString *message);

/** Every method runs on the main thread, and so does every completion. */
@protocol CKConsoleService <NSObject>

@property (nonatomic, weak, nullable) id<CKConsoleServiceDelegate> delegate;

/** The rows, in ck_console_rows_build()'s order. */
@property (nonatomic, readonly, copy) NSArray<CKConsoleItem *> *consoles;

/** YES when the consoles are made up, to try the app without a console. */
@property (nonatomic, readonly, getter=isSimulated) BOOL simulated;

/** Searching costs battery and Wi-Fi: the list searches only while it is on screen. */
- (void)start;
- (void)stop;

- (void)wakeConsole:(CKConsoleItem *)console completion:(CKWakeCompletion)completion;

/**
 * Registers with the console and saves the registration. One at a time.
 * accountID is CHIAKI_PSN_ACCOUNT_ID_SIZE bytes, as ck_parse_psn_account_id() writes it.
 */
- (void)registerConsole:(CKConsoleItem *)console
              accountID:(const uint8_t *)accountID
                    pin:(uint32_t)pin
             completion:(CKRegistCompletion)completion;

/** Stops a running registration; its completion then reports CK_REGIST_CANCELED. */
- (void)cancelRegistration;

/** The saved registration CKSession needs. NO when the console has none. */
- (BOOL)getRegisteredHost:(ChiakiRegisteredHost *)host forConsole:(CKConsoleItem *)console;

@end

NS_ASSUME_NONNULL_END
