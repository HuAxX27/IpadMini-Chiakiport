#!/usr/bin/env bash
# Installs .deb packages on the iPad over SSH: copies each one, installs it
# with dpkg -i and refreshes the home screen with uicache. For Linux (and the
# Docker image); deploy.ps1 does the same from Windows.
#
#   ios/toolchain/deploy.sh <iPad IP> [package.deb | folder ...]
#
# With no packages it installs every .deb in build-ios-ci/debs (what
# ci-ipad.sh leaves); a folder means every .deb in it, such as the unzipped
# "ipad-debs" artifact of a PR. The iPad needs its jailbreak active (re-run
# Carbon after a reboot) and OpenSSH from Cydia. SSH asks for root's password
# once: every step shares one connection.
#
# Environment: CK_IPAD_USER (default root), CK_IPAD_PORT (default 22) and
# CK_SSH_OPTS, extra ssh options split on spaces (for example "-i key").
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"

fail() {
	echo "deploy: FALLÓ: $*" >&2
	exit 1
}

[ $# -ge 1 ] || fail "uso: $0 <IP del iPad> [paquete.deb | carpeta ...]"
ip="$1"
shift
[ $# -ge 1 ] || set -- "$root/build-ios-ci/debs"

debs=()
for arg in "$@"; do
	if [ -d "$arg" ]; then
		for deb in "$arg"/*.deb; do
			[ -f "$deb" ] && debs+=("$deb")
		done
	elif [ -f "$arg" ] && [[ "$arg" == *.deb ]]; then
		debs+=("$arg")
	else
		fail "$arg no es un .deb ni una carpeta"
	fi
done
[ ${#debs[@]} -gt 0 ] || fail "no hay ningún .deb en: $*"

control="$(mktemp -d)"
ssh_opts=(-p "${CK_IPAD_PORT:-22}"
	# The iPad's OpenSSH only offers an ssh-rsa host key, which new clients
	# turn off.
	-o HostKeyAlgorithms=+ssh-rsa
	-o ControlMaster=auto -o ControlPath="$control/%C" -o ControlPersist=60)
# shellcheck disable=SC2206
[ -n "${CK_SSH_OPTS:-}" ] && ssh_opts+=($CK_SSH_OPTS)
target="${CK_IPAD_USER:-root}@$ip"

remote() {
	ssh "${ssh_opts[@]}" "$target" "$@"
}

cleanup() {
	ssh "${ssh_opts[@]}" -O exit "$target" 2>/dev/null || true
	rm -rf "$control"
}
trap cleanup EXIT

echo "==> Conectando a $target"
if ! remote 'command -v dpkg >/dev/null && command -v uicache >/dev/null'; then
	fail "no se pudo usar SSH, dpkg o uicache en $ip. ¿El iPad está en la misma red, con el jailbreak activo (Carbon después de reiniciar) y OpenSSH instalado?"
fi

for deb in "${debs[@]}"; do
	name="${deb##*/}"
	echo "==> Instalando $name"
	# Sent through ssh's stdin, so the iPad does not need scp or sftp-server.
	remote "cat > '/tmp/$name' && dpkg -i '/tmp/$name'; s=\$?; rm -f '/tmp/$name'; exit \$s" < "$deb" \
		|| fail "dpkg -i $name"
done

echo "==> Actualizando la pantalla de inicio (uicache)"
# From root's home (/var/root) mobile cannot read its working directory.
remote 'cd /tmp && su mobile -c uicache' || fail "uicache"
echo "==> deploy: listo, ${#debs[@]} paquete(s) en $ip"
