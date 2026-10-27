# CK Control (A5.4, A5.5): a test app for the controls, before the game
# screen (A6.5) exists. It puts the on-screen controls over a black screen,
# reads the DualSense connected straight to the iPad through the default pad
# map, and shows what each source and the two merged would send. "Conectar"
# opens a session with the registered PS5 through CKSession, the class the
# app uses, with those two sources and no picture: the PS5's own screen shows
# what the controls do. See ../README.md.
#
#   make package FINALPACKAGE=1    # .deb in packages/
#
# It links the core as ios/toolchain/ci-ipad.sh builds it, like ios/cli:
# chiaki-lib and its third-party libraries from CK_CORE_BUILD, mbedTLS and
# Opus from CK_IOS_PREFIX.
TARGET := iphone:clang:9.3:9.0
ARCHS := armv7
INSTALL_TARGET_PROCESSES = CKControl
# Cydia's dpkg on iOS 9 is old: gzip is the one compression it surely reads.
THEOS_PLATFORM_DEB_COMPRESSION_TYPE = gzip

CK_ROOT := $(abspath $(CURDIR)/../../..)
CK_CORE_BUILD ?= $(CK_ROOT)/build-ios-core
CK_IOS_PREFIX ?= $(CK_ROOT)/build-ios-deps/prefix

include $(THEOS)/makefiles/common.mk

CK_GIT_COMMIT := $(shell git rev-parse --short HEAD 2>/dev/null || echo desconocido)

# The sources of ios/Input and ios/Bridge are the ones the app uses too.
# Theos names each object and its directory stamp after the source path it is
# given, so a ../ path would leave build files outside .theos: each shared_*
# file includes one of them.
APPLICATION_NAME = CKControl
CKControl_FILES = main.m CKCAppDelegate.m CKCViewController.m \
	shared_CKTouchControlsView.m shared_ck_touch_controls.c shared_ck_pad_map.c \
	shared_ck_ds5_source.c shared_ck_ds5_shared.c shared_ck_ds5.c \
	shared_CKSession.m shared_ck_session.c shared_ck_video_info.c shared_ck_registry.c
CKControl_FRAMEWORKS = UIKit Foundation CoreGraphics QuartzCore
# The core's headers change layout with these two defines: they must match
# how core-check.sh built chiaki-lib.
CKControl_CFLAGS = -fobjc-arc -Wall -Wextra -Werror -Wunguarded-availability \
	-DCHIAKI_LIB_ENABLE_MBEDTLS -DCHIAKI_DISABLE_HOLEPUNCH -DCK_GIT_COMMIT='"$(CK_GIT_COMMIT)"' \
	-I.. -I../.. -I$(CK_ROOT)/lib/include -I$(CK_IOS_PREFIX)/include
CKControl_LDFLAGS = $(CK_CORE_BUILD)/chiaki/lib/libchiaki.a \
	$(CK_CORE_BUILD)/chiaki/third-party/nanopb/libprotobuf-nanopb.a \
	$(CK_CORE_BUILD)/chiaki/third-party/libjerasure.a \
	$(CK_CORE_BUILD)/chiaki/third-party/libgf_complete.a \
	$(CK_IOS_PREFIX)/lib/libmbedtls.a $(CK_IOS_PREFIX)/lib/libmbedx509.a \
	$(CK_IOS_PREFIX)/lib/libmbedcrypto.a $(CK_IOS_PREFIX)/lib/libopus.a

include $(THEOS_MAKE_PATH)/application.mk
