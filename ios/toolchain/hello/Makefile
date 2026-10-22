# Hello world for the iPad mini 1: a UIKit app for armv7 and iOS 9.0, signed
# with ldid and packaged as a .deb for /Applications. It is the P1 check that
# the toolchain makes something that opens on the device.
#
#   make package        # .deb in packages/
TARGET := iphone:clang:9.3:9.0
ARCHS := armv7
INSTALL_TARGET_PROCESSES = CKHola
# Cydia's dpkg on iOS 9 is old: gzip is the one compression it surely reads.
THEOS_PLATFORM_DEB_COMPRESSION_TYPE = gzip

include $(THEOS)/makefiles/common.mk

CK_GIT_COMMIT := $(shell git rev-parse --short HEAD 2>/dev/null || echo desconocido)

APPLICATION_NAME = CKHola
CKHola_FILES = main.m CKHAppDelegate.m CKHRootViewController.m
CKHola_FRAMEWORKS = UIKit Foundation CoreGraphics
CKHola_CFLAGS = -fobjc-arc -Wall -Werror -Wunguarded-availability -DCK_GIT_COMMIT='"$(CK_GIT_COMMIT)"'

include $(THEOS_MAKE_PATH)/application.mk
