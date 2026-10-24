// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A3.2: contract C2 (CKVideoSink.h) with VideoToolbox. ck_video_core.c does
// the work that can be tested on Linux; this file is its decoder:
//
//   - VTDecompressionSession, synchronous: each frame is decoded before the
//     core's callback returns, so the callback can say whether it worked, as
//     C2 asks, and the time it took is the decode time.
//   - The picture goes to the screen's AVSampleBufferDisplayLayer as an
//     uncompressed sample marked "display immediately": no clock, no queue,
//     it shows at the next screen refresh.
//
// The frame's bytes are never copied: the sample buffer handed to
// VideoToolbox points at the sink's AVCC buffer. CoreMedia's small wrapper
// objects (block and sample buffers) are made per frame, since the API has
// no way to reuse them. Built with ARC.

#import "CKVideoSink.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <VideoToolbox/VideoToolbox.h>

#include "ck_h264.h"
#include "ck_video_core.h"
#include "ck_video_sink_stats.h"

#include <mach/mach_time.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/** Decode errors logged one by one before only every this many is. */
#define LOGGED_ERRORS_MAX 10
#define LOG_ERROR_EVERY 100

struct ck_video_sink_t
{
	CKVideoCore *core;
	/** The AVSampleBufferDisplayLayer, retained. NULL decodes without showing. */
	CFTypeRef layer;
	mach_timebase_info_data_t timebase;

	// Only touched inside the core's lock, by the decoder functions below.
	VTDecompressionSessionRef session;
	/** The stream's format, from its SPS and PPS. */
	CMVideoFormatDescriptionRef format;
	/** The decoded pictures' format, for the samples handed to the layer. */
	CMVideoFormatDescriptionRef image_format;
	/** What the output callback said about the frame being decoded. */
	bool output_called;
	OSStatus output_status;
	uint32_t decode_errors;
	bool layer_error_logged;

	// Counted inside the lock, taken by ck_video_sink_take_all_stats() without it.
	uint32_t frames_shown;
	uint32_t layer_failures;
};

static void sink_log(const char *format, ...) __attribute__((format(printf, 1, 2)));

static void sink_log(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	fputs("CKVideoSink: ", stderr);
	vfprintf(stderr, format, args);
	fputc('\n', stderr);
	va_end(args);
}

// ---------------------------------------------------------------------------
// The decoder of ck_video_core.c
// ---------------------------------------------------------------------------

static void show_image(CKVideoSink *sink, CVImageBufferRef image)
{
	if(!sink->layer)
		return;
	if(!sink->image_format || !CMVideoFormatDescriptionMatchesImageBuffer(sink->image_format, image))
	{
		if(sink->image_format)
			CFRelease(sink->image_format);
		sink->image_format = NULL;
		OSStatus status = CMVideoFormatDescriptionCreateForImageBuffer(kCFAllocatorDefault, image, &sink->image_format);
		if(status != noErr)
		{
			sink_log("no pude describir la imagen decodificada (%d)", (int)status);
			return;
		}
	}
	// With "display immediately" the layer ignores the time; it is set anyway.
	CMSampleTimingInfo timing = { kCMTimeInvalid, CMClockGetTime(CMClockGetHostTimeClock()), kCMTimeInvalid };
	CMSampleBufferRef sample = NULL;
	OSStatus status = CMSampleBufferCreateForImageBuffer(kCFAllocatorDefault, image, true, NULL, NULL,
		sink->image_format, &timing, &sample);
	if(status != noErr)
	{
		sink_log("no pude envolver la imagen decodificada (%d)", (int)status);
		return;
	}
	CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sample, true);
	if(attachments && CFArrayGetCount(attachments) > 0)
	{
		CFMutableDictionaryRef attachment = (CFMutableDictionaryRef)CFArrayGetValueAtIndex(attachments, 0);
		CFDictionarySetValue(attachment, kCMSampleAttachmentKey_DisplayImmediately, kCFBooleanTrue);
	}
	AVSampleBufferDisplayLayer *layer = (__bridge AVSampleBufferDisplayLayer *)sink->layer;
	if(layer.status == AVQueuedSampleBufferRenderingStatusFailed)
	{
		if(!sink->layer_error_logged)
		{
			sink_log("la capa falló: %s", layer.error.description.UTF8String);
			sink->layer_error_logged = true;
		}
		__atomic_add_fetch(&sink->layer_failures, 1, __ATOMIC_RELAXED);
		[layer flush];
	}
	[layer enqueueSampleBuffer:sample];
	CFRelease(sample);
	__atomic_add_fetch(&sink->frames_shown, 1, __ATOMIC_RELAXED);
}

// Called by VideoToolbox inside VTDecompressionSessionDecodeFrame(), since
// frames are decoded synchronously.
static void decoded_frame_cb(void *decompression_refcon, void *frame_refcon, OSStatus status,
	VTDecodeInfoFlags flags, CVImageBufferRef image, CMTime pts, CMTime duration)
{
	(void)frame_refcon;
	(void)flags;
	(void)pts;
	(void)duration;
	CKVideoSink *sink = decompression_refcon;
	sink->output_called = true;
	sink->output_status = status;
	if(status == noErr && image)
		show_image(sink, image);
}

static void sink_drop(void *user)
{
	CKVideoSink *sink = user;
	if(sink->session)
	{
		VTDecompressionSessionInvalidate(sink->session);
		CFRelease(sink->session);
		sink->session = NULL;
	}
	if(sink->format)
	{
		CFRelease(sink->format);
		sink->format = NULL;
	}
	if(sink->image_format)
	{
		CFRelease(sink->image_format);
		sink->image_format = NULL;
	}
}

static bool sink_configure(void *user, const uint8_t *sps, size_t sps_size, const uint8_t *pps, size_t pps_size)
{
	CKVideoSink *sink = user;
	const uint8_t *const sets[2] = { sps, pps };
	const size_t sizes[2] = { sps_size, pps_size };
	CMVideoFormatDescriptionRef format = NULL;
	OSStatus status = CMVideoFormatDescriptionCreateFromH264ParameterSets(kCFAllocatorDefault, 2, sets, sizes,
		CK_H264_AVCC_LENGTH_SIZE, &format);
	if(status != noErr)
	{
		sink_log("el SPS y el PPS no forman un formato de video (%d)", (int)status);
		return false;
	}
	// Same size and profile: the session takes the new parameters as they are.
	if(sink->session && VTDecompressionSessionCanAcceptFormatDescription(sink->session, format))
	{
		CFRelease(sink->format);
		sink->format = format;
		return true;
	}
	sink_drop(sink);

	// The decoder's own layout (NV12, video range), in IOSurface memory that
	// the layer shows without a copy.
	NSDictionary *attributes = @{
		(__bridge NSString *)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
		(__bridge NSString *)kCVPixelBufferIOSurfacePropertiesKey: @{},
	};
	VTDecompressionOutputCallbackRecord callback = { decoded_frame_cb, sink };
	status = VTDecompressionSessionCreate(kCFAllocatorDefault, format, NULL, (__bridge CFDictionaryRef)attributes,
		&callback, &sink->session);
	if(status != noErr)
	{
		sink_log("VideoToolbox no creó el decodificador (%d)", (int)status);
		sink->session = NULL;
		CFRelease(format);
		return false;
	}
	sink->format = format;
	CMVideoDimensions size = CMVideoFormatDescriptionGetDimensions(format);
	sink_log("decodificador listo para %dx%d", (int)size.width, (int)size.height);
	return true;
}

static void log_decode_error(CKVideoSink *sink, const char *what, OSStatus status)
{
	sink->decode_errors++;
	if(sink->decode_errors <= LOGGED_ERRORS_MAX || sink->decode_errors % LOG_ERROR_EVERY == 0)
		sink_log("%s (%d), error %u", what, (int)status, sink->decode_errors);
}

static CKVideoDecodeResult sink_decode(void *user, const uint8_t *avcc, size_t avcc_size)
{
	CKVideoSink *sink = user;
	if(!sink->session)
		return CK_VIDEO_DECODE_BROKEN;

	// Wraps the sink's buffer without copying it: kCFAllocatorNull never frees it.
	CMBlockBufferRef block = NULL;
	OSStatus status = CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, (void *)avcc, avcc_size,
		kCFAllocatorNull, NULL, 0, avcc_size, 0, &block);
	CMSampleBufferRef sample = NULL;
	if(status == kCMBlockBufferNoErr)
		status = CMSampleBufferCreateReady(kCFAllocatorDefault, block, sink->format, 1, 0, NULL, 1, &avcc_size, &sample);
	if(block)
		CFRelease(block);
	if(status != noErr)
	{
		log_decode_error(sink, "no pude armar el cuadro para VideoToolbox", status);
		return CK_VIDEO_DECODE_FAILED;
	}

	sink->output_called = false;
	sink->output_status = noErr;
	VTDecodeInfoFlags info = 0;
	status = VTDecompressionSessionDecodeFrame(sink->session, sample, 0, NULL, &info);
	CFRelease(sample);
	if(status == noErr && sink->output_called)
		status = sink->output_status;
	if(status == noErr)
		return CK_VIDEO_DECODE_OK;
	log_decode_error(sink, "VideoToolbox no decodificó un cuadro", status);
	// The session is gone (the app went to the background) or the decoder died.
	if(status == kVTInvalidSessionErr || status == kVTVideoDecoderMalfunctionErr)
		return CK_VIDEO_DECODE_BROKEN;
	return CK_VIDEO_DECODE_FAILED;
}

static uint64_t sink_now_us(void *user)
{
	CKVideoSink *sink = user;
	return mach_absolute_time() * sink->timebase.numer / sink->timebase.denom / 1000;
}

// ---------------------------------------------------------------------------
// Contract C2
// ---------------------------------------------------------------------------

CKVideoSink *ck_video_sink_new(void *display_layer)
{
	CKVideoSink *sink = calloc(1, sizeof(*sink));
	if(!sink)
		return NULL;
	mach_timebase_info(&sink->timebase);
	CKVideoDecoder decoder = { sink_configure, sink_decode, sink_drop, sink_now_us, sink };
	sink->core = ck_video_core_new(&decoder);
	if(!sink->core)
	{
		free(sink);
		return NULL;
	}
	if(display_layer)
		sink->layer = CFBridgingRetain((__bridge id)display_layer);
	return sink;
}

void ck_video_sink_free(CKVideoSink *sink)
{
	if(!sink)
		return;
	ck_video_core_free(sink->core);
	if(sink->layer)
		CFRelease(sink->layer);
	free(sink);
}

bool ck_video_sink_sample_cb(uint8_t *buf, size_t buf_size, int32_t frames_lost, bool frame_recovered, void *user)
{
	CKVideoSink *sink = user;
	// The core's video thread has no autorelease pool of its own.
	@autoreleasepool
	{
		return ck_video_core_sample(sink->core, buf, buf_size, frames_lost, frame_recovered);
	}
}

void ck_video_sink_reset(CKVideoSink *sink)
{
	ck_video_core_reset(sink->core);
	if(sink->layer)
		[(__bridge AVSampleBufferDisplayLayer *)sink->layer flushAndRemoveImage];
}

void ck_video_sink_take_all_stats(CKVideoSink *sink, CKVideoSinkStats *stats)
{
	ck_video_core_take_stats(sink->core, &stats->core);
	stats->frames_shown = __atomic_exchange_n(&sink->frames_shown, 0, __ATOMIC_RELAXED);
	stats->layer_failures = __atomic_exchange_n(&sink->layer_failures, 0, __ATOMIC_RELAXED);
}

void ck_video_sink_take_stats(CKVideoSink *sink, uint32_t *frames_decoded, float *decode_ms_avg)
{
	CKVideoSinkStats stats;
	ck_video_sink_take_all_stats(sink, &stats);
	uint32_t decodes = stats.core.frames_decoded + stats.core.frames_failed;
	if(frames_decoded)
		*frames_decoded = stats.core.frames_decoded;
	if(decode_ms_avg)
		*decode_ms_avg = decodes ? (float)(stats.core.decode_us / 1000.0 / decodes) : 0.0f;
}
