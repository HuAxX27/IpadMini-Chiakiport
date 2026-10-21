#!/usr/bin/env bash
# Builds and runs the A3.1 tests for ios/Video/ck_h264.c on Linux.
# Usage: ./ios/tests/ck_h264_tests.sh [capture.h264 ...]
#
#   1. Unit tests, with AddressSanitizer and UBSan when the compiler has them.
#   2. The same tests built for armv7 (Cortex-A9 and a 32-bit size_t, like the
#      iPad mini's A5) and run under qemu-arm.
#   3. "check" on the x264 sample in ios/tests/data and on every capture given,
#      such as the PS5 recording from A2.8.
#   4. Our AVCC byte for byte against ffmpeg's: the committed reference for the
#      sample, and a fresh 960x540 clip when ffmpeg has libx264.
#
# Steps 2 and the fresh clip need extra tools (apt install gcc-arm-linux-gnueabihf
# qemu-user ffmpeg); without them they print SKIP. Any failure exits non-zero.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$ROOT/build-ios-tests"
DATA="$ROOT/ios/tests/data"
SAMPLE="$DATA/x264_320x180.h264"
SRC=("$ROOT/ios/tests/ck_h264_tests.c" "$ROOT/ios/Video/ck_h264.c")
CFLAGS=(-std=c99 -Wall -Wextra -Werror -I"$ROOT/ios")
CC="${CC:-cc}"
mkdir -p "$BUILD"

# A clip shaped like the PS5 stream: Main profile, no B-frames, several slices
# per frame, an SPS and PPS before every IDR, and access unit delimiters.
# Arguments: size, frames, bitrate, output. The committed sample is
# make_clip 320x180 18 150k "$SAMPLE", and x264_320x180.ffmpeg.avcc is
# ffmpeg_avcc of it.
make_clip() {
	ffmpeg -hide_banner -loglevel error -y -f lavfi -i "testsrc2=size=$1:rate=30" -frames:v "$2" \
		-c:v libx264 -profile:v main -pix_fmt yuv420p -preset veryfast -tune zerolatency -b:v "$3" \
		-x264-params "keyint=15:min-keyint=15:scenecut=0:slices=4:aud=1:repeat-headers=1" -f h264 "$4"
}

# ffmpeg's own Annex B to AVCC conversion: remux to MP4, then dump the samples.
ffmpeg_avcc() {
	ffmpeg -hide_banner -loglevel error -y -i "$1" -c copy "$BUILD/remux.mp4"
	ffmpeg -hide_banner -loglevel error -y -i "$BUILD/remux.mp4" -map 0:v -c copy -f data "$2"
}

echo "== unit tests ($CC)"
if echo 'int main(void){return 0;}' | "$CC" -fsanitize=address,undefined -x c - -o "$BUILD/probe" 2>/dev/null; then
	"$CC" "${CFLAGS[@]}" -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all "${SRC[@]}" -o "$BUILD/ck_h264_tests"
else
	echo "   no sanitizers in $CC, building without them"
	"$CC" "${CFLAGS[@]}" -O2 "${SRC[@]}" -o "$BUILD/ck_h264_tests"
fi
"$BUILD/ck_h264_tests"

echo "== armv7 under qemu-arm"
if command -v arm-linux-gnueabihf-gcc >/dev/null && command -v qemu-arm >/dev/null; then
	arm-linux-gnueabihf-gcc "${CFLAGS[@]}" -O2 -mcpu=cortex-a9 -mfpu=neon -static "${SRC[@]}" -o "$BUILD/ck_h264_tests_armv7"
	qemu-arm -cpu cortex-a9 "$BUILD/ck_h264_tests_armv7"
	qemu-arm -cpu cortex-a9 "$BUILD/ck_h264_tests_armv7" check "$SAMPLE"
else
	echo "SKIP: needs arm-linux-gnueabihf-gcc and qemu-arm"
fi

echo "== captures"
"$BUILD/ck_h264_tests" check "$SAMPLE" "$@"

echo "== against ffmpeg"
"$BUILD/ck_h264_tests" avcc "$SAMPLE" "$BUILD/sample.avcc"
cmp "$BUILD/sample.avcc" "$DATA/x264_320x180.ffmpeg.avcc"
echo "sample: same AVCC as the committed ffmpeg reference"
encoders="$(ffmpeg -hide_banner -encoders 2>/dev/null || true)"
if [[ "$encoders" == *libx264* ]]; then
	make_clip 960x540 90 2M "$BUILD/clip540.h264"
	ffmpeg_avcc "$BUILD/clip540.h264" "$BUILD/clip540.ffmpeg.avcc"
	"$BUILD/ck_h264_tests" avcc "$BUILD/clip540.h264" "$BUILD/clip540.avcc"
	cmp "$BUILD/clip540.avcc" "$BUILD/clip540.ffmpeg.avcc"
	echo "960x540 clip: same AVCC as ffmpeg"
	"$BUILD/ck_h264_tests" check "$BUILD/clip540.h264"
else
	echo "SKIP: fresh clip needs ffmpeg with libx264"
fi

echo "ck_h264: all passed"
