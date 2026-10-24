# CK Video (A3.2 to A3.5): the video test bench on the iPad. Plays a capture
# from A2.8, or the PS5 live through CKSession (contract C1), through
# CKVideoSink, the same sink the app uses (contract C2), and shows on screen
# what the sink counts. It is a tool for testing the video, not the app: A6
# builds that in ios/UI. See ../README.md.
#
#   make package FINALPACKAGE=1    # .deb in packages/
#
# It links the core as ios/toolchain/ci-ipad.sh builds it, like ios/cli:
# chiaki-lib and its third-party libraries from CK_CORE_BUILD
# (core-check.sh), mbedTLS and Opus from CK_IOS_PREFIX (build-deps.sh).
TARGET := iphone:clang:9.3:9.0
ARCHS := armv7
INSTALL_TARGET_PROCESSES = CKVideo
# Cydia's dpkg on iOS 9 is old: gzip is the one compression it surely reads.
THEOS_PLATFORM_DEB_COMPRESSION_TYPE = gzip

CK_ROOT := $(abspath $(CURDIR)/../../..)
CK_CORE_BUILD ?= $(CK_ROOT)/build-ios-core
CK_IOS_PREFIX ?= $(CK_ROOT)/build-ios-deps/prefix

include $(THEOS)/makefiles/common.mk

CK_GIT_COMMIT := $(shell git rev-parse --short HEAD 2>/dev/null || echo desconocido)

# CKVideoSink.m, ck_video_core.c and ck_h264.c live in ios/Video, and
# CKSession.m, ck_session.c, ck_video_info.c and ck_registry.c in ios/Bridge,
# where the app uses them too. Theos names each object after the source path
# it is given, so a ../ path would leave build files outside .theos: each
# shared_* file includes one of them.
APPLICATION_NAME = CKVideo
CKVideo_FILES = main.m CKVAppDelegate.m CKVViewController.m CKVVideoView.m CKVCapturePlayer.m CKVLiveSession.m \
	shared_CKVideoSink.m shared_ck_video_core.c shared_ck_h264.c \
	shared_CKSession.m shared_ck_session.c shared_ck_video_info.c shared_ck_registry.c
CKVideo_FRAMEWORKS = UIKit Foundation CoreGraphics QuartzCore AVFoundation CoreMedia CoreVideo VideoToolbox
# The core's headers change layout with these two defines: they must match
# how core-check.sh built chiaki-lib.
CKVideo_CFLAGS = -fobjc-arc -Wall -Wextra -Werror -Wunguarded-availability \
	-DCHIAKI_LIB_ENABLE_MBEDTLS -DCHIAKI_DISABLE_HOLEPUNCH \
	-I.. -I$(CK_ROOT)/ios -I$(CK_ROOT)/lib/include -I$(CK_IOS_PREFIX)/include \
	-DCK_GIT_COMMIT='"$(CK_GIT_COMMIT)"'
CKVideo_LDFLAGS = $(CK_CORE_BUILD)/chiaki/lib/libchiaki.a \
	$(CK_CORE_BUILD)/chiaki/third-party/nanopb/libprotobuf-nanopb.a \
	$(CK_CORE_BUILD)/chiaki/third-party/libjerasure.a \
	$(CK_CORE_BUILD)/chiaki/third-party/libgf_complete.a \
	$(CK_IOS_PREFIX)/lib/libmbedtls.a $(CK_IOS_PREFIX)/lib/libmbedx509.a \
	$(CK_IOS_PREFIX)/lib/libmbedcrypto.a $(CK_IOS_PREFIX)/lib/libopus.a

include $(THEOS_MAKE_PATH)/application.mk
