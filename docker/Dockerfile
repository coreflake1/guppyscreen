# GuppyScreen cross-compilation toolchain image.
#
# Reproduces the build environment used to cross-compile GuppyScreen for the
# Ender-3 V3 KE (MIPS/mipsel) and for aarch64 targets. This is a from-source
# replacement for the external `ballaswag/guppydev:latest` image: same Ubuntu
# base, same two toolchains, same PATH and install layout — but defined here,
# pinned by version and SHA256, and auditable.
#
# Build:
#   docker build -t ghcr.io/coreflake1/guppydev:latest docker/
#
# Use (cross-compile for the KE):
#   docker run --rm -it -v "$PWD":/work -w /work \
#     ghcr.io/coreflake1/guppydev:latest bash scripts/build-mips.sh
#
# Toolchains provided on PATH:
#   - mipsel-buildroot-linux-musl-  (Bootlin mips32el musl, gcc 12.3.0) + mipsel-linux- alias
#   - aarch64-none-linux-gnu-       (Arm GNU toolchain 10.2-2020.11, gcc 10.2.1)
FROM ubuntu:22.04

# Match upstream: no recommended/suggested packages.
RUN echo 'APT::Install-Recommends "0";' >> /etc/apt/apt.conf.d/00-docker \
 && echo 'APT::Install-Suggests "0";'   >> /etc/apt/apt.conf.d/00-docker

RUN DEBIAN_FRONTEND=noninteractive apt-get update \
 && apt-get install -y \
      build-essential \
      cmake \
      git \
      ca-certificates \
      wget \
      xz-utils \
      bzip2 \
 && update-ca-certificates \
 && rm -rf /var/lib/apt/lists/*

# --- Cross toolchains -------------------------------------------------------
# Bootlin mips32el musl (the KE target) and the Arm aarch64-none-linux-gnu set.
ARG MIPS_TOOLCHAIN=mips32el--musl--stable-2024.02-1
ARG MIPS_URL=https://toolchains.bootlin.com/downloads/releases/toolchains/mips32el/tarballs/mips32el--musl--stable-2024.02-1.tar.bz2
ARG MIPS_SHA256=25c0b3217df1bf1a7bae2cc4f56cdeab9fec98b172bbf0b336b2e8fe41d3ee4e

ARG ARM_TOOLCHAIN=gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu
ARG ARM_URL=https://developer.arm.com/-/media/Files/downloads/gnu-a/10.2-2020.11/binrel/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu.tar.xz
ARG ARM_SHA256=fe7f72330216612de44891ebe5e228eed7c0c051ac090c395b2b33115c6f5408

WORKDIR /toolchains

RUN wget -q "$MIPS_URL" -O mips.tar.bz2 \
 && echo "${MIPS_SHA256}  mips.tar.bz2" | sha256sum -c - \
 && tar xjf mips.tar.bz2 \
 && rm mips.tar.bz2

RUN wget -q "$ARM_URL" -O arm.tar.xz \
 && echo "${ARM_SHA256}  arm.tar.xz" | sha256sum -c - \
 && tar xf arm.tar.xz \
 && rm arm.tar.xz

# Same PATH ordering as the upstream image.
ENV PATH=/toolchains/${ARM_TOOLCHAIN}/bin:/toolchains/${MIPS_TOOLCHAIN}/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

LABEL org.opencontainers.image.title="GuppyScreen cross toolchain" \
      org.opencontainers.image.description="Ubuntu 22.04 + mipsel-musl (Bootlin) and aarch64-none-linux-gnu (Arm) toolchains for building GuppyScreen." \
      org.opencontainers.image.source="https://github.com/coreflake1/guppyscreen"

CMD ["/bin/bash"]
