#!/usr/bin/env bash
# Checks that the toolchain makes binaries the iPad mini 1 can load: builds the
# smoke tool with Theos and verifies it is a signed armv7 Mach-O for iOS 9.0
# linked against the 9.3 SDK. Then checks that an iOS 10 API (clock_gettime)
# still fails to link, so the SDK keeps enforcing the iOS 9.0 floor.
# The build goes to a temporary directory; the repo is left untouched.
#
#   THEOS=/opt/theos ios/toolchain/smoke-test.sh
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
: "${THEOS:?define THEOS (por ejemplo, export THEOS=/opt/theos)}"
bin="$THEOS/toolchain/linux/iphone/bin"
sdk="$THEOS/sdks/iPhoneOS9.3.sdk"

out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT

fail() {
	echo "smoke-test: FALLÓ: $*" >&2
	exit 1
}

echo "==> make (Theos, armv7, iOS 9.0)"
make -C "$here/smoke" _THEOS_LOCAL_DATA_DIR="$out/.theos" all
tool="$out/.theos/obj/debug/ck-smoke"
[ -f "$tool" ] || fail "make no produjo $tool"

echo "==> Revisando $tool"
archs="$("$bin/lipo" -archs "$tool")"
[ "$archs" = "armv7" ] || fail "arquitecturas '$archs', se esperaba solo armv7"

loads="$("$bin/otool" -l "$tool")"
grep -A3 'LC_VERSION_MIN_IPHONEOS' <<<"$loads" | grep -q 'version 9.0' \
	|| fail "falta LC_VERSION_MIN_IPHONEOS 9.0"
grep -A3 'LC_VERSION_MIN_IPHONEOS' <<<"$loads" | grep -q 'sdk 9.3' \
	|| fail "no se enlazó contra el SDK 9.3"
grep -q 'LC_CODE_SIGNATURE' <<<"$loads" || fail "no está firmado con ldid"
"$bin/otool" -L "$tool" | grep -q '/usr/lib/libSystem.B.dylib' || fail "no enlaza libSystem"
echo "    armv7, iOS 9.0, SDK 9.3, firmado"

echo "==> Revisando el piso de iOS 9.0"
cat > "$out/ios10.c" <<'EOF'
int clock_gettime(int clock_id, void *tp);
int main(void) { char tp[16]; return clock_gettime(6, tp); }
EOF
if "$bin/clang" -target armv7-apple-ios9.0 -isysroot "$sdk" "$out/ios10.c" -o "$out/ios10" 2>"$out/ios10.log"; then
	fail "clock_gettime (iOS 10) enlazó: el SDK ya no protege el piso de iOS 9.0"
fi
grep -q '_clock_gettime' "$out/ios10.log" || { cat "$out/ios10.log" >&2; fail "el error no fue por clock_gettime"; }
echo "    clock_gettime no enlaza, como debe ser"

echo "==> smoke-test: todo bien"
