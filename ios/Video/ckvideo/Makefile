# CK Video (A3.2 to A3.5): the video test bench on the iPad. Plays a capture
# from A2.8 through CKVideoSink, the same sink the app uses (contract C2), and
# shows on screen what the sink counts. It is a tool for testing the video,
# not the app: A6 builds that in ios/UI. See ../README.md.
#
#   make package FINALPACKAGE=1    # .deb in packages/
TARGET := iphone:clang:9.3:9.0
ARCHS := armv7
INSTALL_TARGET_PROCESSES = CKVideo
# Cydia's dpkg on iOS 9 is old: gzip is the one compression it surely reads.
THEOS_PLATFORM_DEB_COMPRESSION_TYPE = gzip

CK_ROOT := $(abspath $(CURDIR)/../../..)
CK_IOS_PREFIX ?= $(CK_ROOT)/build-ios-deps/prefix

include $(THEOS)/makefiles/common.mk

CK_GIT_COMMIT := $(shell git rev-parse --short HEAD 2>/dev/null || echo desconocido)

# CKVideoSink.m, ck_video_core.c and ck_h264.c live in ios/Video, where the
# app uses them too. Theos names each object after the source path it is
# given, so a ../ path would leave build files outside .theos: each shared_*
# file includes one of them.
APPLICATION_NAME = CKVideo
CKVideo_FILES = main.m CKVAppDelegate.m CKVViewController.m CKVVideoView.m CKVCapturePlayer.m \
	shared_CKVideoSink.m shared_ck_video_core.c shared_ck_h264.c
CKVideo_FRAMEWORKS = UIKit Foundation CoreGraphics QuartzCore AVFoundation CoreMedia CoreVideo VideoToolbox
# CKVideoSink.h includes the core's session.h, whose layout changes with these
# two defines: they must match how core-check.sh builds chiaki-lib.
CKVideo_CFLAGS = -fobjc-arc -Wall -Wextra -Werror -Wunguarded-availability \
	-DCHIAKI_LIB_ENABLE_MBEDTLS -DCHIAKI_DISABLE_HOLEPUNCH \
	-I.. -I$(CK_ROOT)/lib/include -I$(CK_IOS_PREFIX)/include \
	-DCK_GIT_COMMIT='"$(CK_GIT_COMMIT)"'

include $(THEOS_MAKE_PATH)/application.mk
