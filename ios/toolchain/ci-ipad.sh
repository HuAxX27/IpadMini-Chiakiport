#!/usr/bin/env bash
# Everything the CI builds for the iPad mini 1 on each PR (ios-ci.yml), and
# the same thing on a Linux PC:
#
#   1. mbedTLS and Opus for armv7 (build-deps.sh; quick when already built).
#   2. The contracts against the iOS 9.3 SDK for armv7: the C ones, so their
#      _Static_asserts hold with the iPad's 32-bit layout, and CKSession.h as
#      Objective-C, with the defines the core is built with.
#   3. The core (core-check.sh).
#   4. Every Theos package under ios/ (a folder with a Makefile and a control
#      file), as a .deb.
#
#   THEOS=/opt/theos ios/toolchain/ci-ipad.sh
#
# Results go to build-ios-ci/ at the repo root (git-ignored): debs/ has the
# packages, libs-armv7/ the static libraries with their headers and licenses.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
: "${THEOS:?define THEOS (por ejemplo, export THEOS=/opt/theos)}"
bin="$THEOS/toolchain/linux/iphone/bin"
sdk="$THEOS/sdks/iPhoneOS9.3.sdk"
prefix="${CK_IOS_PREFIX:-$root/build-ios-deps/prefix}"
core="$root/build-ios-core"
out="$root/build-ios-ci"

fail() {
	echo "ci-ipad: FALLÓ: $*" >&2
	exit 1
}

"$here/build-deps.sh" "$prefix"
prefix="$(cd "$prefix" && pwd)"

echo "==> Contratos para armv7 con el SDK 9.3"
check=("$bin/clang" -target armv7-apple-ios9.0 -isysroot "$sdk" -fsyntax-only
	-Werror=unguarded-availability -DCHIAKI_LIB_ENABLE_MBEDTLS -DCHIAKI_DISABLE_HOLEPUNCH
	-I"$root/lib/include" -I"$root/ios" -I"$root/bridge/protocol" -I"$prefix/include")
"${check[@]}" -std=c11 "$root/ios/tests/contracts_check.c"
echo "    contracts_check.c"
"${check[@]}" -x objective-c -fobjc-arc "$root/ios/Bridge/CKSession.h"
echo "    Bridge/CKSession.h"

CK_IOS_PREFIX="$prefix" "$here/core-check.sh" "$core"

echo "==> Paquetes de Theos en ios/"
rm -rf "$out/debs" "$out/theos"
mkdir -p "$out/debs"
found=0
while IFS= read -r control; do
	dir="$(dirname "$control")"
	[ -f "$dir/Makefile" ] || continue
	name="${dir#"$root"/}"
	echo "==> $name"
	make -C "$dir" package FINALPACKAGE=1 \
		_THEOS_LOCAL_DATA_DIR="$out/theos/${name//\//_}" THEOS_PACKAGE_DIR="$out/debs"
	found=$((found + 1))
done < <(find "$root/ios" -name control -not -path '*/.theos/*' | sort)
[ "$found" -gt 0 ] || fail "no hay ningún paquete de Theos en ios/"
debs=("$out"/debs/*.deb)
[ -f "${debs[0]}" ] || fail "make package no dejó ningún .deb"

echo "==> Librerías armv7 en $out/libs-armv7"
libs="$out/libs-armv7"
rm -rf "$libs"
mkdir -p "$libs/lib" "$libs/share/licenses"
cp -R "$prefix/include" "$libs/"
cp "$prefix"/lib/*.a "$libs/lib/"
cp -R "$prefix/share/licenses/." "$libs/share/licenses/"
for lib in chiaki/lib/libchiaki.a chiaki/third-party/nanopb/libprotobuf-nanopb.a \
	chiaki/third-party/libjerasure.a chiaki/third-party/libgf_complete.a; do
	[ -f "$core/$lib" ] || fail "falta $lib en $core"
	cp "$core/$lib" "$libs/lib/"
done
install -Dm644 "$root/COPYING" "$libs/share/licenses/chiaki/COPYING"
install -Dm644 "$root/third-party/nanopb/LICENSE.txt" "$libs/share/licenses/nanopb/LICENSE.txt"
install -Dm644 "$root/third-party/jerasure/COPYING" "$libs/share/licenses/jerasure/COPYING"
install -Dm644 "$root/third-party/gf-complete/COPYING" "$libs/share/licenses/gf-complete/COPYING"
for lib in "$libs"/lib/*.a; do
	info="$("$bin/lipo" -info "$lib")"
	case "$info" in
		*"architecture: armv7") ;;
		*) fail "${lib##*/} no es solo armv7: $info" ;;
	esac
done

echo "==> ci-ipad: listo"
for deb in "${debs[@]}"; do
	echo "    ${deb#"$root"/}"
done
echo "    ${libs#"$root"/}: $(cd "$libs/lib" && echo *.a)"
