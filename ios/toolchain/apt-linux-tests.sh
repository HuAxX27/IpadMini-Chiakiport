#!/usr/bin/env bash
# Installs on Ubuntu 24.04 what the port's Linux tests need (ios/tests), for
# the host and for armhf: the core tests and the H.264 tests also run as
# 32-bit ARM under qemu-arm, which needs the armhf copies of OpenSSL, mbedTLS
# and Opus. armhf packages live on ports.ubuntu.com, so the existing sources
# are limited to the host architecture and a ports source is added for armhf.
# The CI (ios-ci.yml) runs it; it is also the setup for a Linux PC.
#
#   sudo ios/toolchain/apt-linux-tests.sh
set -euo pipefail

host_arch="$(dpkg --print-architecture)"
. /etc/os-release
codename="$VERSION_CODENAME"

dpkg --add-architecture armhf
if [ -f /etc/apt/sources.list.d/ubuntu.sources ] \
	&& ! grep -q '^Architectures:' /etc/apt/sources.list.d/ubuntu.sources; then
	sed -i "/^URIs:/a Architectures: $host_arch" /etc/apt/sources.list.d/ubuntu.sources
fi
if [ -f /etc/apt/sources.list ]; then
	sed -i -E "s/^deb (http|https|mirror)/deb [arch=$host_arch] \\1/" /etc/apt/sources.list
fi
cat > /etc/apt/sources.list.d/ck-armhf-ports.sources <<EOF
Types: deb
URIs: http://ports.ubuntu.com/ubuntu-ports/
Suites: $codename $codename-updates $codename-security
Components: main universe
Architectures: armhf
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg
EOF

apt-get update -qq
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
	ca-certificates build-essential cmake ninja-build python3 protobuf-compiler python3-protobuf git \
	libssl-dev libmbedtls-dev libopus-dev \
	gcc-arm-linux-gnueabihf libc6-dev-armhf-cross qemu-user ffmpeg \
	libssl-dev:armhf libmbedtls-dev:armhf libopus-dev:armhf
