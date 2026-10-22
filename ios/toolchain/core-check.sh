#!/usr/bin/env bash
# Builds the core (chiaki-lib) for the iPad mini 1 with ios9-armv7.cmake, LAN
# only, with mbedTLS and Opus, and links the whole archive into one signed
# armv7 executable (core-check/). It proves the core configures, compiles and
# links against the iOS 9.3 SDK.
#
#   THEOS=/opt/theos [CK_IOS_PREFIX=...] ios/toolchain/core-check.sh [build dir] [extra cmake args]
#
# mbedTLS and Opus come from CK_IOS_PREFIX, by default build-ios-deps/prefix,
# which build-deps.sh fills if it is empty. The build dir defaults to
# build-ios-core/ at the repo root (both git-ignored by build-*).
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
: "${THEOS:?define THEOS (por ejemplo, export THEOS=/opt/theos)}"
build="${1:-$root/build-ios-core}"
shift || true
prefix="${CK_IOS_PREFIX:-$root/build-ios-deps/prefix}"
if [ ! -f "$prefix/lib/libopus.a" ] || [ ! -f "$prefix/lib/libmbedtls.a" ]; then
	"$here/build-deps.sh" "$prefix"
fi
prefix="$(cd "$prefix" && pwd)"

git -C "$root" submodule update --init third-party/nanopb third-party/jerasure third-party/gf-complete

generator=()
command -v ninja >/dev/null && generator=(-G Ninja)
cmake -S "$here/core-check" -B "$build" "${generator[@]}" \
	-DCMAKE_TOOLCHAIN_FILE="$here/ios9-armv7.cmake" -DTHEOS="$THEOS" \
	-DCK_IOS_PREFIX="$prefix" -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build "$build"

bin="$THEOS/toolchain/linux/iphone/bin"
"$bin/lipo" -archs "$build/ck-core-check"
"$bin/otool" -l "$build/ck-core-check" | grep -A3 LC_VERSION_MIN_IPHONEOS
echo "==> core-check: el core compila y enlaza para armv7 con el SDK 9.3"
