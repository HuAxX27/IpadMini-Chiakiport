// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#import <Foundation/Foundation.h>

#import "Bridge/CKSession.h"

#include "CKVideoSink.h"

NS_ASSUME_NONNULL_BEGIN

@class CKVLiveSession;

/** Every call arrives on the main thread. */
@protocol CKVLiveSessionDelegate <NSObject>
/** What the session is doing, in words for the panel. */
- (void)liveSession:(CKVLiveSession *)live didSay:(NSString *)status;
- (void)liveSession:(CKVLiveSession *)live didUpdateMetrics:(CKMetrics)metrics;
/** The session ended, by the console's choice or an error; status says why. */
- (void)liveSessionDidEnd:(CKVLiveSession *)live;
@end

/**
 * A3.4: a streaming session with the registered PS5 through CKSession (contract
 * C1), the class the app uses, its video going to a CKVideoSink (contract C2).
 * Takes the console that ckctl registered in the registration store and its
 * last known IP. While the console still holds the last session it answers
 * "in use": the session asks again every CKV_BUSY_RETRY_SECONDS, up to
 * CKV_BUSY_RETRIES times. Main thread only.
 */
@interface CKVLiveSession : NSObject

/** nil, with the reason in *error, when there is no registered console or the core does not start. */
- (nullable instancetype)initWithSink:(CKVideoSink *)sink error:(NSString *_Nullable *_Nullable)error;
- (instancetype)init NS_UNAVAILABLE;

@property (nonatomic, weak, nullable) id<CKVLiveSessionDelegate> delegate;
/** The console's name and IP, from the registration store. */
@property (nonatomic, readonly) NSString *consoleName;
@property (nonatomic, readonly) NSString *host;
@property (nonatomic, readonly) BOOL streaming;
/** From asking for the session that the console accepted to the first decoded frame; 0 until then. */
@property (nonatomic, readonly) double firstFrameMs;

/** Resets the sink and asks the console for a session. */
- (void)start;
/** Returns once the core's threads have joined; the sink gets nothing more. Safe to call more than once. */
- (void)stop;

@end

NS_ASSUME_NONNULL_END
