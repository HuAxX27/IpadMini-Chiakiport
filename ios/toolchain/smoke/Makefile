# Smallest Theos project the toolchain must build: a command-line tool for the
# iPad mini 1. smoke-test.sh builds it and checks the result.
TARGET := iphone:clang:9.3:9.0
ARCHS := armv7

include $(THEOS)/makefiles/common.mk

TOOL_NAME = ck-smoke
ck-smoke_FILES = main.c
ck-smoke_CFLAGS = -std=c11 -Wall -Werror

include $(THEOS_MAKE_PATH)/tool.mk
