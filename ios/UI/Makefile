# Chiakiport, the app (A6): a UIKit app for the iPad mini 1 (armv7, iOS 9.0),
# signed with ldid and packaged as a .deb for /Applications. See README.md.
#
#   make package FINALPACKAGE=1    # .deb in packages/
#
# It links the core as ios/toolchain/ci-ipad.sh builds it: chiaki-lib and its
# third-party libraries from CK_CORE_BUILD (core-check.sh), mbedTLS and Opus
# from CK_IOS_PREFIX (build-deps.sh). Both default to where those scripts
# leave them.
TARGET := iphone:clang:9.3:9.0
ARCHS := armv7
INSTALL_TARGET_PROCESSES = Chiakiport
# Cydia's dpkg on iOS 9 is old: gzip is the one compression it surely reads.
THEOS_PLATFORM_DEB_COMPRESSION_TYPE = gzip

CK_ROOT := $(abspath $(CURDIR)/../..)
CK_CORE_BUILD ?= $(CK_ROOT)/build-ios-core
CK_IOS_PREFIX ?= $(CK_ROOT)/build-ios-deps/prefix

include $(THEOS)/makefiles/common.mk

CK_GIT_COMMIT := $(shell git rev-parse --short HEAD 2>/dev/null || echo desconocido)

APPLICATION_NAME = Chiakiport

# The code the app shares with ckctl lives in ios/Bridge. Theos names each
# object and its directory stamp after the source path it is given, so a ../
# path would leave build files outside .theos: each shared_*.c includes one
# file from there, as ios/cli does.
Chiakiport_FILES = main.m CKAppDelegate.m CKConsoleService.m CKMockConsoleService.m CKLanConsoleService.m \
	CKConsoleListViewController.m CKConsoleCell.m CKRegistViewController.m CKStreamViewController.m \
	CKSettingsStore.m CKSettingsViewController.m \
	ck_console_rows.c ck_ui_settings.c shared_ck_consoles.c shared_ck_registry.c
Chiakiport_FRAMEWORKS = UIKit Foundation CoreGraphics QuartzCore

# The core's headers change layout with these two defines: they must match
# how core-check.sh built chiaki-lib.
Chiakiport_CFLAGS = -fobjc-arc -Wall -Werror -Wunguarded-availability \
	-DCHIAKI_LIB_ENABLE_MBEDTLS -DCHIAKI_DISABLE_HOLEPUNCH -DCK_GIT_COMMIT='"$(CK_GIT_COMMIT)"' \
	-I.. -I$(CK_ROOT)/lib/include -I$(CK_IOS_PREFIX)/include
Chiakiport_LDFLAGS = $(CK_CORE_BUILD)/chiaki/lib/libchiaki.a \
	$(CK_CORE_BUILD)/chiaki/third-party/nanopb/libprotobuf-nanopb.a \
	$(CK_CORE_BUILD)/chiaki/third-party/libjerasure.a \
	$(CK_CORE_BUILD)/chiaki/third-party/libgf_complete.a \
	$(CK_IOS_PREFIX)/lib/libmbedtls.a $(CK_IOS_PREFIX)/lib/libmbedx509.a \
	$(CK_IOS_PREFIX)/lib/libmbedcrypto.a $(CK_IOS_PREFIX)/lib/libopus.a

include $(THEOS_MAKE_PATH)/application.mk
