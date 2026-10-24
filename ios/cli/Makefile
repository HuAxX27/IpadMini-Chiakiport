# ckctl (A2.4): finds, registers and wakes the console from a shell on the
# iPad. Installs to /usr/bin/ckctl. See README.md.
#
#   make package FINALPACKAGE=1    # .deb in packages/
#
# It links the core as ios/toolchain/ci-ipad.sh builds it: chiaki-lib and its
# third-party libraries from CK_CORE_BUILD (core-check.sh), mbedTLS and Opus
# from CK_IOS_PREFIX (build-deps.sh). Both default to where those scripts
# leave them.
TARGET := iphone:clang:9.3:9.0
ARCHS := armv7
# Cydia's dpkg on iOS 9 is old: gzip is the one compression it surely reads.
THEOS_PLATFORM_DEB_COMPRESSION_TYPE = gzip

CK_ROOT := $(abspath $(CURDIR)/../..)
CK_CORE_BUILD ?= $(CK_ROOT)/build-ios-core
CK_IOS_PREFIX ?= $(CK_ROOT)/build-ios-deps/prefix

include $(THEOS)/makefiles/common.mk

# ck_consoles.c lives in ios/Bridge, where the app uses it too. Theos names
# each object and its directory stamp after the source path it is given, so
# a ../ path would leave build files outside .theos: shared_*.c includes it.
TOOL_NAME = ckctl
ckctl_FILES = main.c ckctl.c shared_ck_consoles.c
# The core's headers change layout with these two defines: they must match
# how core-check.sh built chiaki-lib.
ckctl_CFLAGS = -std=c11 -Wall -Wextra -Werror -Wunguarded-availability \
	-DCHIAKI_LIB_ENABLE_MBEDTLS -DCHIAKI_DISABLE_HOLEPUNCH \
	-I.. -I$(CK_ROOT)/lib/include -I$(CK_IOS_PREFIX)/include
ckctl_LDFLAGS = $(CK_CORE_BUILD)/chiaki/lib/libchiaki.a \
	$(CK_CORE_BUILD)/chiaki/third-party/nanopb/libprotobuf-nanopb.a \
	$(CK_CORE_BUILD)/chiaki/third-party/libjerasure.a \
	$(CK_CORE_BUILD)/chiaki/third-party/libgf_complete.a \
	$(CK_IOS_PREFIX)/lib/libmbedtls.a $(CK_IOS_PREFIX)/lib/libmbedx509.a \
	$(CK_IOS_PREFIX)/lib/libmbedcrypto.a $(CK_IOS_PREFIX)/lib/libopus.a
ckctl_INSTALL_PATH = /usr/bin

include $(THEOS_MAKE_PATH)/tool.mk
