# Reproducible Linux build environment for the iPad mini 1 (iOS 9.3.5, armv7):
# Theos, its clang 11 / ld64-609 toolchain and the patched iOS 9.3 SDK, all
# pinned in versions.env. See README.md in this folder.
#
#   docker build -t ck-ios-toolchain ios/toolchain
#   docker run --rm -v "$PWD":/work ck-ios-toolchain ios/toolchain/smoke-test.sh
ARG BASE_IMAGE=ubuntu:24.04@sha256:68ee22004a9109b44973313b02ff7b1de250f1de0f5ecb628d73f579aaff33ac
FROM ${BASE_IMAGE}

# Theos needs make, perl, fakeroot and rsync; the toolchain links libtinfo,
# libuuid and libxml2. cmake, ninja, python3 and pkg-config are for building
# the core and its dependencies (A1.3, A1.4); openssh-client for deploys (A1.6).
RUN apt-get update \
	&& DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
		ca-certificates curl git xz-utils make perl fakeroot rsync zip unzip file \
		libtinfo6 libuuid1 libxml2 \
		cmake ninja-build python3 pkg-config \
		openssh-client \
	&& rm -rf /var/lib/apt/lists/*

ENV THEOS=/opt/theos
COPY versions.env install-theos.sh fix-sdk93.sh /opt/ck-toolchain/
RUN /opt/ck-toolchain/install-theos.sh \
	&& chmod -R a+rX "$THEOS" \
	&& git config --system --add safe.directory '*'

# The toolchain's bin/ is not put on PATH: its ar, strip and ld are the Mach-O
# ones and would shadow the Linux tools that native builds and tests need.
# HOME is writable by any user, so `docker run -u "$(id -u):$(id -g)"` works:
# clang keeps its module cache there.
ENV HOME=/tmp
WORKDIR /work
