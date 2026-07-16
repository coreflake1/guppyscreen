# Reconstructed 2026-07-16 from `docker history --no-trunc` against the exact
# pinned digest every scripts/build-*-mipsel.sh script uses:
#   pellcorp/k1-bash-build@sha256:0b96d1d65175c5a2e3a83a64c3212d08dd774fef0900f991e0ebc570ba896c85
#
# This is NOT pellcorp's own original Dockerfile source (not published anywhere
# we could find) - it's a faithful reconstruction from the image's own build
# history, which buildkit preserves verbatim as the exact RUN/ADD/ENV commands
# used. Exists so this whole cross-compile toolchain can be rebuilt from
# scratch if pellcorp/k1-bash-build ever disappears from Docker Hub, without
# depending on it staying available - the same vendoring philosophy as
# scripts/vendor/nginx-src/, pillow-src/, etc. See docker/README.md for how to
# build and use this.
#
# k1-sysroot-min.tar.gz (vendored alongside this Dockerfile, ~11MB) is the
# exact /opt/k1-sysroot content extracted directly from the real pinned image
# via `docker run ... tar czf ... -C /opt k1-sysroot` - guaranteed byte-
# identical to what every build script has actually been using, not a
# best-effort recreation.

FROM ubuntu:20.04

RUN apt-get update
RUN DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential sudo wget cmake git

RUN adduser --disabled-password --gecos "" developer && \
    usermod -a -G sudo developer && \
    echo "%sudo ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/nopasswd

# The MIPS cross toolchain itself (mips-gcc720-glibc229) - a real, versioned
# GitHub release asset, not a Docker-Hub-only artifact. This URL is the actual
# source, confirmed via the image's own build history.
RUN mkdir /opt/toolchains && \
    wget https://github.com/ballaswag/k1-discovery/releases/download/1.0.0/mips-gcc720-glibc229.tar.gz -O /tmp/mips-gcc720-glibc229.tar.gz && \
    tar -zxf /tmp/mips-gcc720-glibc229.tar.gz -C /opt/toolchains && \
    rm /tmp/mips-gcc720-glibc229.tar.gz

# The device-mirroring sysroot (glibc runtime headers/libs only, no dev
# userland - see scripts/build-pillow-mipsel.sh's own header comment for what
# this does and doesn't contain). Vendored alongside this Dockerfile.
ADD k1-sysroot-min.tar.gz /opt/
ENV SYSROOT=/opt/k1-sysroot
RUN test -f $SYSROOT/lib/ld-linux-mipsn8.so.1 && test -f $SYSROOT/lib/libc.so.6

# QEMU user-mode emulation for MIPS - see scripts/lib/ensure-mips-qemu.sh for
# the NaN2008 gotcha this alone does NOT fix (the default qemu-mipsel-static
# CPU model doesn't support the NaN2008 FP encoding this toolchain uses - the
# build scripts patch around it themselves at runtime, not baked in here).
RUN apt-get update && apt-get install -y --no-install-recommends qemu-user-static binfmt-support && \
    update-binfmts --enable qemu-mipsel

ENV PATH=/opt/toolchains/mips-gcc720-glibc229/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

USER developer
WORKDIR /home/developer
