TARGET := iphone:clang:9.3:9.0
ARCHS := armv7

include $(THEOS)/makefiles/common.mk

# The portable sources live in ios/Input and lib/src. Theos names each object
# and its directory stamp after the source path it is given, so ../ paths would
# leave build files outside .theos: the shared_*.c files include them instead.
TOOL_NAME = ckhidprobe
ckhidprobe_FILES = main.c shared_ds5_print.c shared_ck_ds5.c shared_ck_ds5_shared.c shared_ck_ds5_source.c \
	shared_controller.c
ckhidprobe_CFLAGS = -std=c11 -Wall -Wextra -Werror -Wunguarded-availability -I../.. -I../../tools -I../../../../lib/include
ckhidprobe_FRAMEWORKS = CoreFoundation IOKit
ckhidprobe_INSTALL_PATH = /usr/bin

include $(THEOS_MAKE_PATH)/tool.mk
