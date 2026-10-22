#!/usr/bin/env bash
# Builds and runs the bridge's portable C tests on the host (Linux or macOS).
# Usage: ./bridge/tests/run_tests.sh   (needs a C compiler; CC picks another one)
# Binaries go to bridge/tests/build/ (git-ignored).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BRIDGE="$ROOT/bridge"
OUT="$BRIDGE/tests/build"
CC="${CC:-cc}"
CFLAGS=(-std=c11 -O1 -g -Wall -Wextra -Werror -Wconversion -Wno-sign-conversion
	-I"$BRIDGE/protocol" -I"$BRIDGE/core" -I"$ROOT/lib/include")

# Sanitizers when the compiler has them.
if echo 'int main(void){return 0;}' | "$CC" -x c - -fsanitize=address,undefined -o /dev/null 2>/dev/null; then
	CFLAGS+=(-fsanitize=address,undefined -fno-sanitize-recover=undefined)
fi

mkdir -p "$OUT"
SRCS=("$BRIDGE/protocol/ck_pad_codec.c" "$BRIDGE/core/ck_bridge_map.c" "$BRIDGE/core/ck_bridge_pace.c")

status=0
for test in codec map pace; do
	"$CC" "${CFLAGS[@]}" "$BRIDGE/tests/test_$test.c" "${SRCS[@]}" -o "$OUT/test_$test"
	"$OUT/test_$test" || status=1
done
exit $status
