#!/usr/bin/env bash
# Installs the pinned tools of check.sh that Ubuntu does not package well:
# gitleaks and actionlint, from their GitHub releases, checked against the
# hashes in tools.env. On Linux x86_64, as on the GitHub runners.
#
#   .github/quality/install-tools.sh [gitleaks] [actionlint]   # default: both
#
# They go to CK_TOOLS_BIN (default ~/.local/bin). In GitHub Actions that folder
# is added to the PATH of the next steps.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=tools.env
. "$here/tools.env"
bin="${CK_TOOLS_BIN:-$HOME/.local/bin}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

fail() {
	echo "install-tools: FALLÓ: $*" >&2
	exit 1
}

[ "$(uname -sm)" = "Linux x86_64" ] || fail "solo hay hashes para Linux x86_64; en otro sistema instala gitleaks y actionlint a mano"

# fetch NAME URL SHA256: downloads URL and checks it, then unpacks NAME into bin.
fetch() {
	local name="$1" url="$2" want="$3" got
	curl -fsSL --retry 3 -o "$tmp/$name.tar.gz" "$url"
	got="$(sha256sum "$tmp/$name.tar.gz" | cut -d' ' -f1)"
	[ "$got" = "$want" ] || fail "$name: el sha256 es $got y tools.env dice $want"
	mkdir -p "$bin"
	tar -xzf "$tmp/$name.tar.gz" -C "$tmp" "$name"
	install -m 755 "$tmp/$name" "$bin/$name"
	echo "install-tools: $name en $bin ($want)"
}

tools=("$@")
[ "${#tools[@]}" -gt 0 ] || tools=(gitleaks actionlint)
for tool in "${tools[@]}"; do
	case "$tool" in
		gitleaks)
			fetch gitleaks "https://github.com/gitleaks/gitleaks/releases/download/v$GITLEAKS_VERSION/gitleaks_${GITLEAKS_VERSION}_linux_x64.tar.gz" \
				"$GITLEAKS_SHA256_x86_64"
			;;
		actionlint)
			fetch actionlint "https://github.com/rhysd/actionlint/releases/download/v$ACTIONLINT_VERSION/actionlint_${ACTIONLINT_VERSION}_linux_amd64.tar.gz" \
				"$ACTIONLINT_SHA256_x86_64"
			;;
		*) fail "herramienta desconocida: $tool (gitleaks, actionlint)" ;;
	esac
done

if [ -n "${GITHUB_PATH:-}" ]; then
	echo "$bin" >>"$GITHUB_PATH"
fi
