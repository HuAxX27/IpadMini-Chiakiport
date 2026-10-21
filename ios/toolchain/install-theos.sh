#!/usr/bin/env bash
# Installs Theos, its Linux iOS toolchain and the patched iOS 9.3 SDK into
# $THEOS (default /opt/theos), every piece pinned and hash-checked by
# versions.env. The Dockerfile runs it; it also works directly on a Linux host.
# Running it again only redoes what changed in versions.env.
#
#   THEOS=$HOME/theos ios/toolchain/install-theos.sh
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=versions.env
. "$here/versions.env"
THEOS="${THEOS:-/opt/theos}"

missing=""
for cmd in git curl tar xz sha256sum make perl fakeroot rsync; do
	command -v "$cmd" >/dev/null || missing="$missing $cmd"
done
if [ -n "$missing" ]; then
	echo "install-theos: faltan comandos:$missing" >&2
	exit 1
fi

arch="$(uname -m)"
case "$arch" in
	x86_64) toolchain_sha="$TOOLCHAIN_SHA256_x86_64" ;;
	aarch64) toolchain_sha="$TOOLCHAIN_SHA256_aarch64" ;;
	*) echo "install-theos: no hay toolchain precompilado para $arch" >&2; exit 1 ;;
esac

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# fetch URL SHA256 FILE: download and refuse anything whose hash differs.
fetch() {
	curl -fsSL --retry 3 -o "$3" "$1"
	if ! echo "$2  $3" | sha256sum -c --quiet - >/dev/null 2>&1; then
		echo "install-theos: $1 no coincide con su sha256 fijado ($2)" >&2
		exit 1
	fi
}

echo "==> Theos $THEOS_COMMIT"
mkdir -p "$THEOS"
if [ ! -d "$THEOS/.git" ]; then
	git -C "$THEOS" init -q
	git -C "$THEOS" remote add origin https://github.com/theos/theos.git
fi
if [ "$(git -C "$THEOS" rev-parse -q --verify HEAD 2>/dev/null)" != "$THEOS_COMMIT" ]; then
	git -C "$THEOS" fetch -q --depth 1 origin "$THEOS_COMMIT"
	git -C "$THEOS" checkout -q --force "$THEOS_COMMIT"
fi
git -C "$THEOS" submodule -q update --init --recursive --depth 1

echo "==> Toolchain $TOOLCHAIN_TAG ($arch)"
toolchain_stamp="$THEOS/toolchain/.ck-toolchain"
if [ "$(cat "$toolchain_stamp" 2>/dev/null)" != "$toolchain_sha" ]; then
	fetch "https://github.com/L1ghtmann/llvm-project/releases/download/$TOOLCHAIN_TAG/iOSToolchain-$arch.tar.xz" \
		"$toolchain_sha" "$tmp/toolchain.tar.xz"
	rm -rf "$THEOS/toolchain/linux"
	mkdir -p "$THEOS/toolchain"
	tar -xJf "$tmp/toolchain.tar.xz" -C "$THEOS/toolchain"
	echo "$toolchain_sha" > "$toolchain_stamp"
fi
clang_version="$("$THEOS/toolchain/linux/iphone/bin/clang" --version)"
ld_version="$("$THEOS/toolchain/linux/iphone/bin/ld" -v 2>&1)"
echo "    ${clang_version%%$'\n'*}"
echo "    ${ld_version%%$'\n'*}"

echo "==> $SDK_NAME ($SDK_TAG)"
sdk="$THEOS/sdks/$SDK_NAME"
sdk_stamp="$THEOS/sdks/.ck-$SDK_NAME"
if [ "$(cat "$sdk_stamp" 2>/dev/null)" != "$SDK_SHA256" ]; then
	fetch "https://github.com/theos/sdks/releases/download/$SDK_TAG/$SDK_NAME.tar.xz" \
		"$SDK_SHA256" "$tmp/sdk.tar.xz"
	rm -rf "$sdk" "$sdk_stamp"
	mkdir -p "$THEOS/sdks"
	tar -xJf "$tmp/sdk.tar.xz" -C "$THEOS/sdks"
	"$here/fix-sdk93.sh" "$sdk"
	echo "$SDK_SHA256" > "$sdk_stamp"
fi

echo "==> Listo: export THEOS=$THEOS"
