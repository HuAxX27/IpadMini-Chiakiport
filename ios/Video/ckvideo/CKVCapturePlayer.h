// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#import <Foundation/Foundation.h>

#include "CKVideoSink.h"

NS_ASSUME_NONNULL_BEGIN

/** Refused frames in a row before the player sends a keyframe, as the console does when the core asks for one. */
#define CKV_KEYFRAME_AFTER 8

/**
 * Plays a capture from A2.8 (raw Annex B, the profile header and then each
 * frame as the core hands them over) through a video sink, 30 frames a
 * second, on its own thread as the core's video thread would, and starts
 * over at the end.
 *
 * When the sink refuses CKV_KEYFRAME_AFTER frames in a row, the player does
 * what the console does when the core asks it for a keyframe: it sends one,
 * by going back to the capture's start (its IDR).
 */
@interface CKVCapturePlayer : NSObject

/** Maps the file. nil, with the reason in *error, when it cannot be read or has no frame. */
- (nullable instancetype)initWithPath:(NSString *)path error:(NSString *_Nullable *_Nullable)error;
- (instancetype)init NS_UNAVAILABLE;

@property (nonatomic, readonly) NSString *path;
@property (nonatomic, readonly) NSUInteger frameCount;
@property (nonatomic, readonly) size_t byteCount;
/** Times the capture played to its end. */
@property (atomic, readonly) NSUInteger loops;
/** Frames the sink refused (returned false). */
@property (atomic, readonly) NSUInteger refused;
/** Times the player went back to the keyframe because the sink kept refusing frames. */
@property (atomic, readonly) NSUInteger keyframesSent;

/** Starts sending frames to the sink, which must outlive -stop. Main thread. */
- (void)startWithSink:(CKVideoSink *)sink;
/** Returns once the player's thread has finished. Safe to call more than once. Main thread. */
- (void)stop;

@end

NS_ASSUME_NONNULL_END
