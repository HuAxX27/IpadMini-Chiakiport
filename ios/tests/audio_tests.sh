#!/usr/bin/env bash
# Builds and runs the A4 audio tests on Linux.
# Usage: ./ios/tests/audio_tests.sh
#
#   1. tone_test (the test tone of ckaudio, ios/Audio/ck_audio_tone.c), with
#      AddressSanitizer and UBSan when the compiler has them.
#   2. The same tests built for armv7 (Cortex-A9 and a 32-bit size_t, like the
#      iPad mini's A5) and run under qemu-arm.
#   3. The ckaudio package built with Theos, when THEOS points at the toolchain
#      from ios/toolchain/install-theos.sh and the core is built for the iPad
#      (ios/toolchain/ci-ipad.sh, which also packages it in CI).
#
# Steps 2 and 3 need extra tools (apt install gcc-arm-linux-gnueabihf qemu-user,
# and Theos); without them they print SKIP. Any failure exits non-zero.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$ROOT/build-ios-tests/audio"
CFLAGS=(-std=c11 -Wall -Wextra -Wpedantic -Werror -I"$ROOT/lib/include" -I"$ROOT/ios")
TONE_SRC=("$ROOT/ios/tests/audio/tone_test.c" "$ROOT/ios/Audio/ck_audio_tone.c")
CC="${CC:-cc}"
mkdir -p "$BUILD"

echo "== unit tests ($CC)"
if echo 'int main(void){return 0;}' | "$CC" -fsanitize=address,undefined -x c - -o "$BUILD/probe" 2>/dev/null; then
	SAN=(-g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all)
else
	echo "   no sanitizers in $CC, building without them"
	SAN=(-O2)
fi
"$CC" "${CFLAGS[@]}" "${SAN[@]}" "${TONE_SRC[@]}" -lm -o "$BUILD/tone_test"
"$BUILD/tone_test"

echo "== armv7 under qemu-arm"
if command -v arm-linux-gnueabihf-gcc >/dev/null && command -v qemu-arm >/dev/null; then
	ARM=(arm-linux-gnueabihf-gcc "${CFLAGS[@]}" -O2 -mcpu=cortex-a9 -mfpu=neon -static)
	"${ARM[@]}" "${TONE_SRC[@]}" -lm -o "$BUILD/tone_test_armv7"
	qemu-arm -cpu cortex-a9 "$BUILD/tone_test_armv7"
else
	echo "SKIP: needs arm-linux-gnueabihf-gcc and qemu-arm"
fi

echo "== ckaudio package (Theos)"
if [[ -n "${THEOS:-}" && -d "$THEOS/makefiles" && -f "$ROOT/build-ios-core/chiaki/lib/libchiaki.a" ]]; then
	make -C "$ROOT/ios/Audio/ckaudio" package FINALPACKAGE=1
	ls "$ROOT"/ios/Audio/ckaudio/packages/*.deb
else
	echo "SKIP: set THEOS and build the core for the iPad first (ios/toolchain/ci-ipad.sh)"
fi

echo "audio: all passed"
