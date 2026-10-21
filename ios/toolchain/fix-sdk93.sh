#!/usr/bin/env bash
# Patches theos/sdks' iPhoneOS9.3.sdk so ld64-609 can link against it.
# Idempotent: a stamp file inside the SDK records that the fixes are applied.
#
#   ios/toolchain/fix-sdk93.sh "$THEOS/sdks/iPhoneOS9.3.sdk"
#
# The SDK's .tbd stubs were regenerated from device binaries and have three
# problems with today's linker:
#
# 1. libSystem re-exports /usr/lib/system/liblaunch.dylib, but the SDK has no
#    stub for it, and ld64-609 stops with "file not found". We add an empty
#    stub: nothing in this project calls launchd APIs, and if something ever
#    does, it fails at link time, never on the iPad.
#
# 2. libplatform's string functions are missing (memset, memcpy, strcmp...):
#    on the device they are aliases of _platform_memset and friends, and the
#    stub generator dropped aliases. Clang emits memset and memcpy calls on its
#    own, so almost any program fails to link. We add the public names of the
#    iOS 9.3 libsystem_platform's __platform_* functions, plus memcpy and bcmp
#    (aliases of memmove and memcmp), to libsystem_c, where Apple's later SDKs
#    list them. Nothing newer than iOS 9 is added: clock_gettime and the other
#    iOS 10 APIs must keep failing.
#
# 3. Every stub also lists i386 and x86_64, so ld64-609 warns "built for iOS
#    Simulator" once per library on every link. This SDK is only for devices,
#    so we drop those two archs and the warnings with them.
set -euo pipefail

sdk="${1:?usage: $0 <path to iPhoneOS9.3.sdk>}"
stamp="$sdk/.ck-sdk-fixes"
fixes_version=1

if [ -f "$stamp" ] && [ "$(cat "$stamp")" = "$fixes_version" ]; then
	exit 0
fi
if [ -f "$stamp" ]; then
	echo "fix-sdk93: $sdk ya tiene parches de otra versión de este script; desempaca un SDK limpio" >&2
	exit 1
fi
[ -f "$sdk/usr/lib/system/libsystem_c.tbd" ] || { echo "fix-sdk93: $sdk no parece ser iPhoneOS9.3.sdk" >&2; exit 1; }

# 3. Device archs only.
find "$sdk" -name '*.tbd' -exec sed -i 's/, i386, x86_64 \]/ ]/' {} +

# 1. Empty liblaunch stub.
cat > "$sdk/usr/lib/system/liblaunch.tbd" <<'EOF'
---
archs:                 [ armv7, armv7s, arm64 ]
platform:              ios
install-name:          /usr/lib/system/liblaunch.dylib
current-version:       1
compatibility-version: 1
...
EOF

# 2. libplatform aliases, as one more export section of libsystem_c.
libc="$sdk/usr/lib/system/libsystem_c.tbd"
grep -qx 'exports:' "$libc"
sed -i '/^exports:$/a\
  - archs:              [ armv7, armv7s, arm64 ]\
    symbols:            [ _bcmp, _bzero, _memccpy, _memchr, _memcmp, _memcpy, _memmove, _memset,\
                          _memset_pattern16, _memset_pattern4, _memset_pattern8, _strchr, _strcmp, _strncmp ]' "$libc"

if grep -rq 'i386, x86_64 \]' --include='*.tbd' "$sdk" || ! grep -qw '_memset' "$libc"; then
	echo "fix-sdk93: el parche no se aplicó en $sdk; cambió su estructura" >&2
	exit 1
fi
echo "$fixes_version" > "$stamp"
