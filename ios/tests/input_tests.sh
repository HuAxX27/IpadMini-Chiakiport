#!/usr/bin/env bash
# Builds and runs the A5 input tests on Linux.
# Usage: ./ios/tests/input_tests.sh
#
#   1. ds5_test (DualSense report parser), hci_filter_test (BTServer HCI
#      filter), ds5_shared_test (the report BTServer shares, with a writer and
#      a reader thread racing), ds5_source_test (the app's input source over
#      that report), pad_map_test (button table and dead zones, A5.4) and
#      ckbtfilter_test (the BTServer tweak's log, shared reports and output
#      reports, built with a stand-in for Substrate), with AddressSanitizer
#      and UBSan when the compiler has them.
#   2. The same tests built for armv7 (Cortex-A9 and a 32-bit size_t, like the
#      iPad mini's A5) and run under qemu-arm.
#   3. The ckdirect package (ckbtfilter and ckhidprobe) built with Theos, when
#      THEOS points at the toolchain from ios/toolchain/install-theos.sh.
#
# Steps 2 and 3 need extra tools (apt install gcc-arm-linux-gnueabihf qemu-user,
# and Theos); without them they print SKIP. Any failure exits non-zero.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$ROOT/build-ios-tests"
CFLAGS=(-std=c11 -Wall -Wextra -Wpedantic -Werror -I"$ROOT/lib/include" -I"$ROOT/ios/Input")
DS5_SRC=("$ROOT/ios/tests/ds5_test.c" "$ROOT/ios/Input/ck_ds5.c" "$ROOT/lib/src/controller.c")
HCI_SRC=("$ROOT/ios/tests/hci_filter_test.c" "$ROOT/ios/Input/ck_hci_filter.c")
SHARED_SRC=("$ROOT/ios/tests/ds5_shared_test.c" "$ROOT/ios/Input/ck_ds5_shared.c")
SOURCE_SRC=("$ROOT/ios/tests/ds5_source_test.c" "$ROOT/ios/Input/ck_ds5_source.c" "$ROOT/ios/Input/ck_ds5_shared.c"
	"$ROOT/ios/Input/ck_ds5.c" "$ROOT/lib/src/controller.c")
PAD_MAP_SRC=("$ROOT/ios/tests/pad_map_test.c" "$ROOT/ios/Input/ck_pad_map.c" "$ROOT/ios/Input/ck_ds5.c"
	"$ROOT/lib/src/controller.c")
TWEAK_SRC=("$ROOT/ios/tests/ckbtfilter_test.c" "$ROOT/ios/Input/ck_hci_filter.c" "$ROOT/ios/Input/ck_ds5_shared.c"
	"$ROOT/ios/Input/ck_ds5.c")
# The tweak's and the source's tests write their files under build-ios-tests,
# never /tmp. Substrate's API takes functions as void *, which -Wpedantic rejects.
SOURCE_FLAGS=(-DSOURCE_PATH="\"$BUILD/ds5_source_test.report\"")
TWEAK_FLAGS=(-Wno-pedantic -D_DEFAULT_SOURCE -I"$ROOT/ios/tests/fakes"
	-DLOG_PATH="\"$BUILD/ckbtfilter_test.log\"" -DSHARED_PATH="\"$BUILD/ckds5.report\"")
CC="${CC:-cc}"
mkdir -p "$BUILD"

echo "== unit tests ($CC)"
if echo 'int main(void){return 0;}' | "$CC" -fsanitize=address,undefined -x c - -o "$BUILD/probe" 2>/dev/null; then
	SAN=(-g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all)
else
	echo "   no sanitizers in $CC, building without them"
	SAN=(-O2)
fi
"$CC" "${CFLAGS[@]}" "${SAN[@]}" "${DS5_SRC[@]}" -o "$BUILD/ds5_test"
"$CC" "${CFLAGS[@]}" "${SAN[@]}" "${HCI_SRC[@]}" -o "$BUILD/hci_filter_test"
"$CC" "${CFLAGS[@]}" "${SAN[@]}" -pthread "${SHARED_SRC[@]}" -o "$BUILD/ds5_shared_test"
"$CC" "${CFLAGS[@]}" "${SAN[@]}" "${SOURCE_FLAGS[@]}" "${SOURCE_SRC[@]}" -o "$BUILD/ds5_source_test"
"$CC" "${CFLAGS[@]}" "${SAN[@]}" "${PAD_MAP_SRC[@]}" -lm -o "$BUILD/pad_map_test"
"$CC" "${CFLAGS[@]}" "${SAN[@]}" -pthread "${TWEAK_FLAGS[@]}" "${TWEAK_SRC[@]}" -o "$BUILD/ckbtfilter_test"
"$BUILD/ds5_test"
"$BUILD/hci_filter_test"
"$BUILD/ds5_shared_test"
"$BUILD/ds5_source_test"
"$BUILD/pad_map_test"
rm -f "$BUILD/ckbtfilter_test.log" "$BUILD/ckds5.report"
"$BUILD/ckbtfilter_test"

echo "== armv7 under qemu-arm"
if command -v arm-linux-gnueabihf-gcc >/dev/null && command -v qemu-arm >/dev/null; then
	ARM=(arm-linux-gnueabihf-gcc "${CFLAGS[@]}" -O2 -mcpu=cortex-a9 -mfpu=neon -static)
	"${ARM[@]}" "${DS5_SRC[@]}" -o "$BUILD/ds5_test_armv7"
	"${ARM[@]}" "${HCI_SRC[@]}" -o "$BUILD/hci_filter_test_armv7"
	"${ARM[@]}" -pthread "${SHARED_SRC[@]}" -o "$BUILD/ds5_shared_test_armv7"
	"${ARM[@]}" "${SOURCE_FLAGS[@]}" "${SOURCE_SRC[@]}" -o "$BUILD/ds5_source_test_armv7"
	"${ARM[@]}" "${PAD_MAP_SRC[@]}" -lm -o "$BUILD/pad_map_test_armv7"
	"${ARM[@]}" -pthread "${TWEAK_FLAGS[@]}" "${TWEAK_SRC[@]}" -o "$BUILD/ckbtfilter_test_armv7"
	qemu-arm -cpu cortex-a9 "$BUILD/ds5_test_armv7"
	qemu-arm -cpu cortex-a9 "$BUILD/hci_filter_test_armv7"
	qemu-arm -cpu cortex-a9 "$BUILD/ds5_shared_test_armv7"
	qemu-arm -cpu cortex-a9 "$BUILD/ds5_source_test_armv7"
	qemu-arm -cpu cortex-a9 "$BUILD/pad_map_test_armv7"
	rm -f "$BUILD/ckbtfilter_test.log" "$BUILD/ckds5.report"
	qemu-arm -cpu cortex-a9 "$BUILD/ckbtfilter_test_armv7"
else
	echo "SKIP: needs arm-linux-gnueabihf-gcc and qemu-arm"
fi

echo "== ckdirect package (Theos)"
if [[ -n "${THEOS:-}" && -d "$THEOS/makefiles" ]]; then
	make -C "$ROOT/ios/Input/ckdirect" package FINALPACKAGE=1
	ls "$ROOT"/ios/Input/ckdirect/packages/*.deb
else
	echo "SKIP: set THEOS to the toolchain from ios/toolchain/install-theos.sh"
fi

echo "input: all passed"
