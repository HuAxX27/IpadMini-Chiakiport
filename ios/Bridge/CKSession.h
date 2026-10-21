// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Contract C1 (A2 <-> A3, A4, A5, A6): the Objective-C face of a Chiaki
// streaming session.
// Owner: A2. A change to this file goes in its own PR, approved by A0.

#import <Foundation/Foundation.h>

#include <chiaki/audioreceiver.h>
#include <chiaki/regist.h>
#include <chiaki/session.h>

#include "CKSettings.h"
#include "../Input/CKInputSource.h"

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, CKSessionState) {
	CKSessionStateIdle,
	CKSessionStateConnecting,
	CKSessionStateStreaming,
	CKSessionStateStopped,
};

@class CKSession;

/** Every delegate call arrives on the main thread. */
@protocol CKSessionDelegate <NSObject>

- (void)session:(CKSession *)session didChangeState:(CKSessionState)state;

/** The stream ended. reason is the core's quit reason; message may be nil. */
- (void)session:(CKSession *)session didQuitWithReason:(ChiakiQuitReason)reason message:(nullable NSString *)message;

@optional

/** Once per second while streaming. */
- (void)session:(CKSession *)session didUpdateMetrics:(CKMetrics)metrics;

@end

@interface CKSession : NSObject

/**
 * host is the console's IP address. registeredHost comes from the
 * registration store (see docs/ipad/contracts.md) and is copied.
 */
- (instancetype)initWithHost:(NSString *)host
              registeredHost:(const ChiakiRegisteredHost *)registeredHost
                    settings:(CKSettings)settings NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

@property (nonatomic, weak, nullable) id<CKSessionDelegate> delegate;
@property (nonatomic, readonly) CKSessionState state;

/** Set before -start:. The callbacks run on core threads (contracts C2 and C3). */
- (void)setVideoSampleCallback:(ChiakiVideoSampleCallback)callback user:(void *)user;
- (void)setAudioSink:(ChiakiAudioSink)sink;

/** Sources are polled and merged as contract C4 describes. Main thread only. */
- (void)addInputSource:(CKInputSource)source;
- (void)removeAllInputSources;

- (BOOL)start:(NSError **)error;

/** Returns once the core's threads have joined. Safe to call more than once. */
- (void)stop;

@end

NS_ASSUME_NONNULL_END
