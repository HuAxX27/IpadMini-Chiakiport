#!/usr/bin/env bash
# Builds the core (chiaki-lib) for the iPad mini 1 with ios9-armv7.cmake, LAN
# only and with mbedTLS, and links the whole archive into one signed armv7
# executable (core-check/). It proves the core configures, compiles and links
# against the iOS 9.3 SDK.
#
#   THEOS=/opt/theos ios/toolchain/core-check.sh [build dir] [extra cmake args]
#
# mbedTLS comes from the root CMakeLists' FetchContent (2.28.0) until A1.4
# builds it; Opus stays off until then too. The build dir defaults to
# build-ios-core/ at the repo root (git-ignored by build-*).
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
: "${THEOS:?define THEOS (por ejemplo, export THEOS=/opt/theos)}"
build="${1:-$root/build-ios-core}"
shift || true

git -C "$root" submodule update --init third-party/nanopb third-party/jerasure third-party/gf-complete

generator=()
command -v ninja >/dev/null && generator=(-G Ninja)
cmake -S "$here/core-check" -B "$build" "${generator[@]}" \
	-DCMAKE_TOOLCHAIN_FILE="$here/ios9-armv7.cmake" -DTHEOS="$THEOS" \
	-DCMAKE_BUILD_TYPE=Release \
	-DCHIAKI_LIB_MBEDTLS_EXTERNAL_PROJECT=ON \
	-DCHIAKI_LIB_ENABLE_OPUS=OFF \
	"$@"
cmake --build "$build"

bin="$THEOS/toolchain/linux/iphone/bin"
"$bin/lipo" -archs "$build/ck-core-check"
"$bin/otool" -l "$build/ck-core-check" | grep -A3 LC_VERSION_MIN_IPHONEOS
echo "==> core-check: el core compila y enlaza para armv7 con el SDK 9.3"
