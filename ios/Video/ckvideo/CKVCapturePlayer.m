// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#import "CKVCapturePlayer.h"

#include "ck_h264.h"

#include <errno.h>
#include <fcntl.h>
#include <mach/mach_time.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define CKV_FPS 30

static bool has_slice(const uint8_t *au, size_t size)
{
	CKH264FrameInfo info;
	ck_h264_annexb_to_avcc(au, size, 0, NULL, 0, &info);
	return (info.nal_types & (CK_H264_NAL_BIT(CK_H264_NAL_SLICE) | CK_H264_NAL_BIT(CK_H264_NAL_IDR))) != 0;
}

@interface CKVCapturePlayer ()
@property (atomic, readwrite) NSUInteger loops;
@property (atomic, readwrite) NSUInteger refused;
@property (atomic, readwrite) NSUInteger keyframesSent;
@end

@implementation CKVCapturePlayer
{
	const uint8_t *_bytes;
	CKVideoSink *_sink;
	NSThread *_thread;
	dispatch_semaphore_t _finished;
	volatile bool _stopping;
}

- (instancetype)initWithPath:(NSString *)path error:(NSString **)error
{
	self = [super init];
	if(!self)
		return nil;
	_path = [path copy];
	int fd = open(path.fileSystemRepresentation, O_RDONLY);
	struct stat st;
	if(fd < 0 || fstat(fd, &st) != 0 || st.st_size <= 0)
	{
		if(error)
			*error = [NSString stringWithFormat:@"no pude abrir %@: %s", path, fd < 0 ? strerror(errno) : "está vacío"];
		if(fd >= 0)
			close(fd);
		return nil;
	}
	_byteCount = (size_t)st.st_size;
	void *map = mmap(NULL, _byteCount, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if(map == MAP_FAILED)
	{
		if(error)
			*error = [NSString stringWithFormat:@"no pude leer %@: %s", path, strerror(errno)];
		return nil;
	}
	_bytes = map;
	size_t pos = 0;
	const uint8_t *au;
	size_t au_size;
	while(ck_h264_next_access_unit(_bytes, _byteCount, &pos, &au, &au_size))
		_frameCount += has_slice(au, au_size);
	if(!_frameCount)
	{
		if(error)
			*error = [NSString stringWithFormat:@"%@ no tiene cuadros H.264", path];
		return nil;
	}
	return self;
}

- (void)dealloc
{
	[self stop];
	if(_bytes)
		munmap((void *)_bytes, _byteCount);
}

- (void)startWithSink:(CKVideoSink *)sink
{
	if(_thread)
		return;
	_sink = sink;
	_stopping = false;
	_finished = dispatch_semaphore_create(0);
	_thread = [[NSThread alloc] initWithTarget:self selector:@selector(run) object:nil];
	_thread.name = @"CKVCapturePlayer";
	[_thread start];
}

- (void)stop
{
	if(!_thread)
		return;
	_stopping = true;
	dispatch_semaphore_wait(_finished, DISPATCH_TIME_FOREVER);
	_thread = nil;
	_sink = NULL;
}

- (void)run
{
	@autoreleasepool
	{
		[self play];
	}
	dispatch_semaphore_signal(_finished);
}

// Sends each frame when its time comes. After a stall it carries on from
// there instead of catching up in a burst.
- (void)play
{
	mach_timebase_info_data_t timebase;
	mach_timebase_info(&timebase);
	uint64_t period = (uint64_t)(1000000000ull / CKV_FPS) * timebase.denom / timebase.numer;
	uint64_t deadline = mach_absolute_time();
	unsigned refused_in_a_row = 0;
	while(!_stopping)
	{
		size_t pos = 0;
		const uint8_t *au;
		size_t au_size;
		bool ended = true;
		while(!_stopping && ck_h264_next_access_unit(_bytes, _byteCount, &pos, &au, &au_size))
		{
			bool frame = has_slice(au, au_size);
			if(frame)
			{
				deadline += period;
				uint64_t now = mach_absolute_time();
				if(deadline > now)
					mach_wait_until(deadline);
				else
					deadline = now;
			}
			if(ck_video_sink_sample_cb((uint8_t *)au, au_size, 0, false, _sink))
				refused_in_a_row = 0;
			else
			{
				self.refused++;
				refused_in_a_row += frame;
			}
			if(refused_in_a_row >= CKV_KEYFRAME_AFTER)
			{
				refused_in_a_row = 0;
				self.keyframesSent++;
				printf("CKVCapturePlayer: %u cuadros rechazados seguidos, vuelvo al keyframe del inicio\n",
					CKV_KEYFRAME_AFTER);
				ended = false;
				break;
			}
		}
		if(!_stopping && ended)
			self.loops++;
	}
}

@end
