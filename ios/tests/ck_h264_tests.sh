#!/usr/bin/env bash
# Builds and runs the video tests (A3) on Linux: ios/Video/ck_h264.c (A3.1)
# and the C half of the video sink, ios/Video/ck_video_core.c (A3.2, and its
# recovery from A3.3).
# Usage: ./ios/tests/ck_h264_tests.sh [capture.h264 ...]
#
#   1. Unit tests of both, with AddressSanitizer and UBSan when the compiler
#      has them.
#   2. The same tests built for armv7 (Cortex-A9 and a 32-bit size_t, like the
#      iPad mini's A5) and run under qemu-arm, also on every capture given.
#   3. "check" on the x264 sample in ios/tests/data and on every capture given,
#      such as the PS5 recording from A2.8.
#   4. Our AVCC byte for byte against ffmpeg's: the committed reference for the
#      sample, every capture given, and a fresh 960x540 clip when ffmpeg has
#      libx264.
#   5. The sample, every capture and the clip sent through the sink frame by
#      frame to a fake decoder; ffmpeg must decode what the decoder got into
#      the same pictures as the original, frame for frame.
#   6. The same, breaking the decoder on every 10th frame: each frame must be
#      decoded, lost or held until the next keyframe as the recovery says, and
#      what the decoder got must decode into the original's pictures for the
#      frames it kept, with no picture made from a missing reference.
#
# Steps 2, the captures against ffmpeg, the fresh clip and steps 5 and 6 need extra
# tools (apt install gcc-arm-linux-gnueabihf qemu-user ffmpeg); without them
# they print SKIP. Any failure exits non-zero.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$ROOT/build-ios-tests"
DATA="$ROOT/ios/tests/data"
SAMPLE="$DATA/x264_320x180.h264"
SRC=("$ROOT/ios/tests/ck_h264_tests.c" "$ROOT/ios/Video/ck_h264.c")
SINK_SRC=("$ROOT/ios/tests/ck_video_tests.c" "$ROOT/ios/Video/ck_video_core.c" "$ROOT/ios/Video/ck_h264.c")
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

# ffmpeg's decoded pictures, one MD5 per frame, without the header lines.
frame_md5s() {
	ffmpeg -hide_banner -loglevel error -i "$1" -f framemd5 - | grep -v '^#'
}

echo "== unit tests ($CC)"
if echo 'int main(void){return 0;}' | "$CC" -fsanitize=address,undefined -x c - -o "$BUILD/probe" 2>/dev/null; then
	"$CC" "${CFLAGS[@]}" -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all "${SRC[@]}" -o "$BUILD/ck_h264_tests"
	"$CC" "${CFLAGS[@]}" -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all "${SINK_SRC[@]}" -pthread -o "$BUILD/ck_video_tests"
else
	echo "   no sanitizers in $CC, building without them"
	"$CC" "${CFLAGS[@]}" -O2 "${SRC[@]}" -o "$BUILD/ck_h264_tests"
	"$CC" "${CFLAGS[@]}" -O2 "${SINK_SRC[@]}" -pthread -o "$BUILD/ck_video_tests"
fi
"$BUILD/ck_h264_tests"
"$BUILD/ck_video_tests"

echo "== armv7 under qemu-arm"
if command -v arm-linux-gnueabihf-gcc >/dev/null && command -v qemu-arm >/dev/null; then
	arm-linux-gnueabihf-gcc "${CFLAGS[@]}" -O2 -mcpu=cortex-a9 -mfpu=neon -static "${SRC[@]}" -o "$BUILD/ck_h264_tests_armv7"
	arm-linux-gnueabihf-gcc "${CFLAGS[@]}" -O2 -mcpu=cortex-a9 -mfpu=neon -static "${SINK_SRC[@]}" -pthread -o "$BUILD/ck_video_tests_armv7"
	qemu-arm -cpu cortex-a9 "$BUILD/ck_h264_tests_armv7"
	qemu-arm -cpu cortex-a9 "$BUILD/ck_h264_tests_armv7" check "$SAMPLE" "$@"
	qemu-arm -cpu cortex-a9 "$BUILD/ck_video_tests_armv7"
	for capture in "$SAMPLE" "$@"; do
		qemu-arm -cpu cortex-a9 "$BUILD/ck_video_tests_armv7" replay "$capture" "$BUILD/replay_armv7.h264"
		qemu-arm -cpu cortex-a9 "$BUILD/ck_video_tests_armv7" replay "$capture" "$BUILD/replay_armv7.h264" 10 "$BUILD/kept_armv7.txt"
	done
else
	echo "SKIP: needs arm-linux-gnueabihf-gcc and qemu-arm"
fi

echo "== captures"
"$BUILD/ck_h264_tests" check "$SAMPLE" "$@"

echo "== against ffmpeg"
"$BUILD/ck_h264_tests" avcc "$SAMPLE" "$BUILD/sample.avcc"
cmp "$BUILD/sample.avcc" "$DATA/x264_320x180.ffmpeg.avcc"
echo "sample: same AVCC as the committed ffmpeg reference"
if command -v ffmpeg >/dev/null; then
	for capture in "$@"; do
		ffmpeg_avcc "$capture" "$BUILD/capture.ffmpeg.avcc"
		"$BUILD/ck_h264_tests" avcc "$capture" "$BUILD/capture.avcc"
		cmp "$BUILD/capture.avcc" "$BUILD/capture.ffmpeg.avcc"
		echo "$capture: same AVCC as ffmpeg"
	done
elif [ "$#" -gt 0 ]; then
	echo "SKIP: comparing the captures needs ffmpeg"
fi
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

echo "== through the sink to a fake decoder"
if command -v ffmpeg >/dev/null; then
	streams=("$SAMPLE" "$@")
	[ -f "$BUILD/clip540.h264" ] && [[ "$encoders" == *libx264* ]] && streams+=("$BUILD/clip540.h264")
	for stream in "${streams[@]}"; do
		"$BUILD/ck_video_tests" replay "$stream" "$BUILD/replay.h264"
		frame_md5s "$stream" >"$BUILD/original.md5"
		frame_md5s "$BUILD/replay.h264" >"$BUILD/replay.md5"
		[ -s "$BUILD/original.md5" ]
		cmp "$BUILD/original.md5" "$BUILD/replay.md5"
		echo "$stream: what the decoder got decodes to the same $(wc -l <"$BUILD/original.md5") pictures"
	done
else
	echo "SKIP: needs ffmpeg"
fi

echo "== recovery: the decoder breaks on every 10th frame"
if command -v ffmpeg >/dev/null; then
	for stream in "${streams[@]}"; do
		"$BUILD/ck_video_tests" replay "$stream" "$BUILD/broken.h264" 10 "$BUILD/kept.txt"
		# The original's pictures for the frames kept, against what the decoder got.
		frame_md5s "$stream" | awk -F', *' 'NR == FNR { keep[$1 + 1] = 1; next } FNR in keep { print $NF }' \
			"$BUILD/kept.txt" - >"$BUILD/original.md5"
		frame_md5s "$BUILD/broken.h264" | awk -F', *' '{ print $NF }' >"$BUILD/broken.md5"
		[ -s "$BUILD/original.md5" ]
		cmp "$BUILD/original.md5" "$BUILD/broken.md5"
		echo "$stream: the $(wc -l <"$BUILD/broken.md5") pictures kept are the original's"
	done
else
	echo "SKIP: needs ffmpeg"
fi

echo "ck_h264 and ck_video_core: all passed"
