# ckaudio (A4.2) tries the iPad's audio output from a shell, without the app:
# "ckaudio tono" plays a test tone through the AudioQueue output and the audio
# session the player uses. Installs to /usr/bin. See ../README.md.
#
#   make package FINALPACKAGE=1    # .deb in packages/
#
# It links chiaki-lib for its log only, as ios/toolchain/ci-ipad.sh builds it
# (CK_CORE_BUILD, from core-check.sh), like ios/cli.
TARGET := iphone:clang:9.3:9.0
ARCHS := armv7
# Cydia's dpkg on iOS 9 is old: gzip is the one compression it surely reads.
THEOS_PLATFORM_DEB_COMPRESSION_TYPE = gzip

CK_ROOT := $(abspath $(CURDIR)/../../..)
CK_CORE_BUILD ?= $(CK_ROOT)/build-ios-core
CK_IOS_PREFIX ?= $(CK_ROOT)/build-ios-deps/prefix

include $(THEOS)/makefiles/common.mk

# The output, the session and the tone live in ios/Audio, where the player
# uses them too. Theos names each object after the source path it is given, so
# a ../ path would leave build files outside .theos: each shared_* file
# includes one of them.
TOOL_NAME = ckaudio

# The core's headers change layout with these two defines: they must match
# how core-check.sh built chiaki-lib.
CK_CFLAGS = -Wall -Wextra -Werror -Wunguarded-availability \
	-DCHIAKI_LIB_ENABLE_MBEDTLS -DCHIAKI_DISABLE_HOLEPUNCH \
	-I../.. -I$(CK_ROOT)/lib/include -I$(CK_IOS_PREFIX)/include

ckaudio_FILES = main.m shared_ck_audio_output_aq.c shared_ck_audio_session.m shared_ck_audio_tone.c
ckaudio_CFLAGS = -fobjc-arc $(CK_CFLAGS)
ckaudio_FRAMEWORKS = Foundation AVFoundation AudioToolbox
ckaudio_LDFLAGS = $(CK_CORE_BUILD)/chiaki/lib/libchiaki.a
ckaudio_INSTALL_PATH = /usr/bin

include $(THEOS_MAKE_PATH)/tool.mk
