#!/usr/bin/env bash
# Builds the real core (chiaki-lib) on Linux the way the iPad uses it, LAN only (no holepunch), and
# runs Chiaki's unit tests against it: with OpenSSL, with mbedTLS (the iPad's backend) and, when the
# cross compiler and qemu are installed, with mbedTLS on 32-bit ARM (armv7-a) under qemu-arm.
# Then runs ./tools/native_tests.sh, the tests inherited from VitaRPS5.
# Usage: ./ios/tests/core/run.sh
# Needs cmake 3.24 or later, a C compiler, python3 with protobuf, protoc, and the dev files of
# OpenSSL, mbedTLS 2.28 and Opus. On Ubuntu:
#   apt install cmake ninja-build protobuf-compiler python3-protobuf libssl-dev libmbedtls-dev libopus-dev
# For the armv7 run, also gcc-arm-linux-gnueabihf, qemu-user and the armhf dev packages (README.md).
# The builds go to build-core-tests/ (git-ignored). They are Debug builds so assert() is live.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
BUILD_ROOT="$ROOT/build-core-tests"

git -C "$ROOT" submodule update --init third-party/nanopb third-party/jerasure third-party/gf-complete

GENERATOR=()
if command -v ninja >/dev/null 2>&1; then
    GENERATOR=(-G Ninja)
fi

# run_core_tests <name> <cmake args...>
run_core_tests() {
    local name="$1"
    shift
    echo "== Core tests, $name"
    cmake -S "$ROOT/ios/tests/core" -B "$BUILD_ROOT/$name" "${GENERATOR[@]}" \
        -DCMAKE_BUILD_TYPE=Debug "$@"
    cmake --build "$BUILD_ROOT/$name" --parallel
    ctest --test-dir "$BUILD_ROOT/$name" --output-on-failure
}

run_core_tests openssl -DCHIAKI_LIB_ENABLE_MBEDTLS=OFF
run_core_tests mbedtls -DCHIAKI_LIB_ENABLE_MBEDTLS=ON

if command -v arm-linux-gnueabihf-gcc >/dev/null 2>&1 && command -v qemu-arm >/dev/null 2>&1; then
    run_core_tests armv7-mbedtls -DCHIAKI_LIB_ENABLE_MBEDTLS=ON \
        -DCMAKE_TOOLCHAIN_FILE="$ROOT/ios/tests/core/linux-armv7.cmake"
else
    echo "== Core tests, armv7-mbedtls: skipped (needs arm-linux-gnueabihf-gcc and qemu-arm)"
fi

echo "== VitaRPS5 native tests"
"$ROOT/tools/native_tests.sh"
