#!/usr/bin/env bash
# Builds and runs the A4 audio tests on Linux.
# Usage: ./ios/tests/audio_tests.sh [captura.opus ...]
#
#   1. tone_test (the test tone of ckaudio, ios/Audio/ck_audio_tone.c) and
#      decoder_test (the Opus decoder and the Ogg Opus reader), with
#      AddressSanitizer and UBSan when the compiler has them. make_opus writes
#      a recording like a PS5's through ios/Bridge/ck_recorder.c, and
#      check_decode.py has opus_wav decode it, and every capture given (the
#      .opus of cksession -g), to WAV, and compares it with ffmpeg's.
#   2. The same tests built for armv7 (Cortex-A9 and a 32-bit size_t, like the
#      iPad mini's A5) and run under qemu-arm.
#   3. The ckaudio package built with Theos, when THEOS points at the toolchain
#      from ios/toolchain/install-theos.sh and the core is built for the iPad
#      (ios/toolchain/ci-ipad.sh, which also packages it in CI).
#
# Needs libopus-dev, ffmpeg and python3; steps 2 and 3 need extra tools (apt
# install gcc-arm-linux-gnueabihf qemu-user libopus-dev:armhf, and Theos), and
# without them they print SKIP. Any failure exits non-zero.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$ROOT/build-ios-tests/audio"
CAPTURES=("$@")
CFLAGS=(-std=c11 -Wall -Wextra -Wpedantic -Werror -I"$ROOT/lib/include" -I"$ROOT/ios")
# The core's log, which the decoder and the recorder write to. lib/ is upstream:
# it is built without this port's warnings.
CORE_SRC=("$ROOT/lib/src/log.c" "$ROOT/lib/src/redact.c")
TONE_SRC=("$ROOT/ios/tests/audio/tone_test.c" "$ROOT/ios/Audio/ck_audio_tone.c")
DECODER_SRC=("$ROOT/ios/Audio/ck_audio_decoder.c" "$ROOT/ios/Audio/ck_ogg_opus.c")
CC="${CC:-cc}"
mkdir -p "$BUILD"

# build SUFFIX COMPILER [flags...]: every test program, each name ending in SUFFIX.
build() {
	local suffix="$1"
	shift
	local core=()
	for src in "${CORE_SRC[@]}"; do
		local obj
		obj="$BUILD/$(basename "$src" .c)$suffix.o"
		"$@" -std=c11 -I"$ROOT/lib/include" -c "$src" -o "$obj"
		core+=("$obj")
	done
	"$@" "${CFLAGS[@]}" "${TONE_SRC[@]}" -lm -o "$BUILD/tone_test$suffix"
	"$@" "${CFLAGS[@]}" "$ROOT/ios/tests/audio/decoder_test.c" "${DECODER_SRC[@]}" "${core[@]}" -lopus -lm \
		-o "$BUILD/decoder_test$suffix"
	"$@" "${CFLAGS[@]}" "$ROOT/ios/tests/audio/opus_wav.c" "${DECODER_SRC[@]}" "${core[@]}" -lopus -lm \
		-o "$BUILD/opus_wav$suffix"
	"$@" "${CFLAGS[@]}" "$ROOT/ios/tests/audio/make_opus.c" "$ROOT/ios/Bridge/ck_recorder.c" "${core[@]}" -lopus -lm \
		-o "$BUILD/make_opus$suffix"
}

# run SUFFIX [emulator...]: runs the tests built with build SUFFIX.
run() {
	local suffix="$1"
	shift
	"$@" "$BUILD/tone_test$suffix"
	rm -f "$BUILD/recording$suffix.opus" "$BUILD/recording$suffix.h264"
	"$@" "$BUILD/make_opus$suffix" "$BUILD/recording$suffix" 3 50
	"$@" "$BUILD/decoder_test$suffix" "$BUILD/recording$suffix.opus"
	python3 "$ROOT/ios/tests/audio/check_decode.py" "$BUILD/recording$suffix.opus" "${CAPTURES[@]}" \
		-- "$@" "$BUILD/opus_wav$suffix"
}

echo "== unit tests ($CC)"
if echo 'int main(void){return 0;}' | "$CC" -fsanitize=address,undefined -x c - -o "$BUILD/probe" 2>/dev/null; then
	SAN=(-g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all)
else
	echo "   no sanitizers in $CC, building without them"
	SAN=(-O2)
fi
build "" "$CC" "${SAN[@]}"
run ""

echo "== armv7 under qemu-arm"
ARMHF_LIB=/usr/lib/arm-linux-gnueabihf
if command -v arm-linux-gnueabihf-gcc >/dev/null && command -v qemu-arm >/dev/null && [ -f "$ARMHF_LIB/libopus.a" ]; then
	# The cross compiler does not look in /usr/include, where the Opus headers
	# are, and must not: the host's libc headers are there too.
	mkdir -p "$BUILD/armhf-include"
	ln -sfn /usr/include/opus "$BUILD/armhf-include/opus"
	build _armv7 arm-linux-gnueabihf-gcc -O2 -mcpu=cortex-a9 -mfpu=neon -static \
		-I"$BUILD/armhf-include" -L"$ARMHF_LIB"
	run _armv7 qemu-arm -cpu cortex-a9
else
	echo "SKIP: needs arm-linux-gnueabihf-gcc, qemu-arm and libopus-dev:armhf"
fi

echo "== ckaudio package (Theos)"
if [[ -n "${THEOS:-}" && -d "$THEOS/makefiles" && -f "$ROOT/build-ios-core/chiaki/lib/libchiaki.a" ]]; then
	make -C "$ROOT/ios/Audio/ckaudio" package FINALPACKAGE=1
	ls "$ROOT"/ios/Audio/ckaudio/packages/*.deb
else
	echo "SKIP: set THEOS and build the core for the iPad first (ios/toolchain/ci-ipad.sh)"
fi

echo "audio: all passed"
