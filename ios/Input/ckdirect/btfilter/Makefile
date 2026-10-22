TARGET := iphone:clang:9.3:9.0
ARCHS := armv7

include $(THEOS)/makefiles/common.mk

# The portable filter lives in ios/Input with its Linux tests. Theos names each
# object and its directory stamp after the source path it is given, so a ../
# path would leave build files outside .theos: shared_*.c includes it instead.
TWEAK_NAME = ckbtfilter
ckbtfilter_FILES = ckbtfilter.c shared_ck_hci_filter.c
ckbtfilter_CFLAGS = -std=c11 -Wall -Wextra -Werror -Wunguarded-availability -I../..

include $(THEOS_MAKE_PATH)/tweak.mk
