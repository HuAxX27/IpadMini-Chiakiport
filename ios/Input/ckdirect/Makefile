# ckdirect (A5.7): the two pieces for connecting the DualSense straight to the
# iPad, in one .deb for a jailbroken iOS 9 device.
#
#   ckbtfilter   Substrate filter inside BTServer: makes the pad look like a
#                keyboard while pairing, so iOS lets it connect, and logs the
#                Bluetooth traffic it sees to /tmp/ckbtfilter.log.
#   ckhidprobe   command-line tool: lists the HID devices and reads the
#                DualSense with IOHIDManager.
#
#   make package FINALPACKAGE=1    # .deb in packages/
TARGET := iphone:clang:9.3:9.0
ARCHS := armv7
INSTALL_TARGET_PROCESSES = BTServer
# Cydia's dpkg on iOS 9 is old: gzip is the one compression it surely reads.
THEOS_PLATFORM_DEB_COMPRESSION_TYPE = gzip

include $(THEOS)/makefiles/common.mk

SUBPROJECTS = btfilter hidprobe

include $(THEOS_MAKE_PATH)/aggregate.mk
