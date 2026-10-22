#!/usr/bin/env bash
# Builds the libraries the core needs that do not come with it, for armv7 and
# iOS 9.0 with ios9-armv7.cmake, as static libraries in one prefix:
#
#   - mbedTLS, from the 2.28 LTS series (the core does not build with 3.x).
#   - Opus, with its NEON code always on: every iPad that runs iOS 9 has NEON,
#     and Opus cannot detect it at run time on iOS.
#
# nanopb, jerasure and gf-complete are not built here: the core's CMake
# compiles them from their submodules in third-party/, with the same toolchain.
# Versions and commits are pinned in versions.env.
#
#   THEOS=/opt/theos ios/toolchain/build-deps.sh [prefix]
#
# The prefix defaults to build-ios-deps/prefix at the repo root (git-ignored);
# pass it to CMake as -DCK_IOS_PREFIX=<prefix> so find_library() sees it. Each
# library's license goes to share/licenses/<name>/ in the prefix.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
# shellcheck source=versions.env
. "$here/versions.env"
: "${THEOS:?define THEOS (por ejemplo, export THEOS=/opt/theos)}"
work="$root/build-ios-deps"
prefix="${1:-$work/prefix}"
mkdir -p "$work/src" "$prefix"
prefix="$(cd "$prefix" && pwd)"

fail() {
	echo "build-deps: FALLÓ: $*" >&2
	exit 1
}

generator=()
command -v ninja >/dev/null && generator=(-G Ninja)

# fetch NAME URL TAG COMMIT: shallow clone of TAG, refused if it is not COMMIT.
fetch() {
	local dir="$work/src/$1"
	if [ "$(git -C "$dir" rev-parse HEAD 2>/dev/null)" != "$4" ]; then
		rm -rf "$dir"
		git -c advice.detachedHead=false clone -q --depth 1 --branch "$3" "$2" "$dir"
		[ "$(git -C "$dir" rev-parse HEAD)" = "$4" ] || fail "$1 $3 no es el commit fijado $4"
	fi
}

# build NAME [cmake args...]: configure, build and install into the prefix.
build() {
	local name="$1"
	shift
	echo "==> $name"
	cmake -S "$work/src/$name" -B "$work/build/$name" "${generator[@]}" \
		-DCMAKE_TOOLCHAIN_FILE="$here/ios9-armv7.cmake" -DTHEOS="$THEOS" \
		-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
		-DBUILD_SHARED_LIBS=OFF "$@"
	cmake --build "$work/build/$name"
	cmake --install "$work/build/$name"
}

fetch mbedtls https://github.com/Mbed-TLS/mbedtls.git "$MBEDTLS_TAG" "$MBEDTLS_COMMIT"
build mbedtls \
	-DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF \
	-DUSE_STATIC_MBEDTLS_LIBRARY=ON -DUSE_SHARED_MBEDTLS_LIBRARY=OFF
install -Dm644 "$work/src/mbedtls/LICENSE" "$prefix/share/licenses/mbedtls/LICENSE"

fetch opus https://github.com/xiph/opus.git "$OPUS_TAG" "$OPUS_COMMIT"
# OPUS_MAY_HAVE_NEON keeps its default (ON): without run-time detection on iOS
# it only adds the defines that make the NEON headers visible, and turning it
# off leaves the NEON code calling functions it has not declared.
build opus \
	-DOPUS_BUILD_PROGRAMS=OFF -DOPUS_BUILD_TESTING=OFF -DOPUS_PRESUME_NEON=ON
install -Dm644 "$work/src/opus/COPYING" "$prefix/share/licenses/opus/COPYING"
install -Dm644 "$work/src/opus/LICENSE_PLEASE_READ.txt" "$prefix/share/licenses/opus/LICENSE_PLEASE_READ.txt"

echo "==> Revisando $prefix/lib"
bin="$THEOS/toolchain/linux/iphone/bin"
for lib in libmbedcrypto.a libmbedx509.a libmbedtls.a libopus.a; do
	[ -f "$prefix/lib/$lib" ] || fail "falta $lib"
	info="$("$bin/lipo" -info "$prefix/lib/$lib")"
	case "$info" in
		*"architecture: armv7") echo "    $lib: armv7" ;;
		*) fail "$lib no es solo armv7: $info" ;;
	esac
done
opus_symbols="$("$bin/nm" "$prefix/lib/libopus.a" 2>/dev/null)"
grep -q '_neon' <<<"$opus_symbols" || fail "libopus.a no trae el código NEON"
echo "    libopus.a trae el código NEON"
echo "==> build-deps: listo en $prefix"
