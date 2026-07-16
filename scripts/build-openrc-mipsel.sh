#!/bin/sh
# Cross-compile OpenRC 0.41.2 (supervise-daemon, librc.so.1, libeinfo.so.1 -
# used by GuppyScreen's k1_mods/respawn/ for process supervision) for the KE
# (mipsel, glibc, hard-float FP64, NaN2008) and write the three binaries to
# scripts/vendor/wheels/../openrc-mipsel/, ready to drop into
# k1/k1_mods/respawn/.
#
# Uses the same pellcorp/k1-bash-build Docker image as the other build-*-
# mipsel.sh scripts. Confirmed via readelf against the currently-vendored
# binaries before writing this: no PAM/SELinux/audit dependencies at all (just
# libc/libdl/libutil), so this is a minimal default build - no MKPAM/
# MKSELINUX/MKAUDIT flags needed. OpenRC's own Makefile.inc respects standard
# CC/AR/RANLIB env var overrides, so no source patching is needed for
# cross-compilation - just point the toolchain at it.

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VENDOR_SRC="$SCRIPT_DIR/vendor/openrc-src"
OUT_DIR="$SCRIPT_DIR/vendor/openrc-mipsel"
K1_BASH_BUILD_IMAGE="pellcorp/k1-bash-build@sha256:0b96d1d65175c5a2e3a83a64c3212d08dd774fef0900f991e0ebc570ba896c85"
WORKDIR="/tmp/openke-openrc-build-$$"
TOOL=/opt/toolchains/mips-gcc720-glibc229
SYSROOT=/opt/k1-sysroot
PKG_VERSION=0.41.2

if ! command -v docker >/dev/null 2>&1; then
    echo "docker is required." >&2
    exit 1
fi
mkdir -p "$OUT_DIR"

echo "=== Staging sources ==="
docker run --rm -v /tmp:/tmp alpine sh -c "mkdir -p '$WORKDIR'"
cp "$VENDOR_SRC/openrc-$PKG_VERSION.tar.gz" /tmp/
docker run --rm -v /tmp:/tmp alpine sh -c "
    mv /tmp/openrc-$PKG_VERSION.tar.gz '$WORKDIR/'
    chmod -R a+rwX '$WORKDIR'
"

echo "=== Building OpenRC $PKG_VERSION ==="
docker run --rm -v /tmp:/tmp -w "$WORKDIR" "$K1_BASH_BUILD_IMAGE" sh -c "
    set -e
    tar xzf openrc-$PKG_VERSION.tar.gz
    cd openrc-$PKG_VERSION
    make \
        CC='$TOOL/bin/mips-linux-gnu-gcc --sysroot=$SYSROOT' \
        AR='$TOOL/bin/mips-linux-gnu-ar' \
        RANLIB='$TOOL/bin/mips-linux-gnu-ranlib' \
        PKG_CONFIG=true \
        MKSELINUX=no MKAUDIT=no MKPKGCONFIG=no MKBASHCOMP=no \
        -j4 >/tmp/openrc-make.log 2>&1
    mkdir -p '$WORKDIR/out'
    cp src/rc/supervise-daemon '$WORKDIR/out/'
    cp src/librc/librc.so.1 '$WORKDIR/out/'
    cp src/libeinfo/libeinfo.so.1 '$WORKDIR/out/'
"

echo "=== Verifying ABI ==="
docker run --rm -v /tmp:/tmp -w "$WORKDIR/out" "$K1_BASH_BUILD_IMAGE" sh -c "
    set -e
    FAIL=0
    for f in supervise-daemon librc.so.1 libeinfo.so.1; do
        $TOOL/bin/mips-linux-gnu-readelf -h \"\$f\" | grep -q 'Machine:.*MIPS' || { echo \"Not MIPS: \$f\"; FAIL=1; }
        $TOOL/bin/mips-linux-gnu-readelf -h \"\$f\" | grep -q 'nan2008' || { echo \"Not NaN2008: \$f\"; FAIL=1; }
        $TOOL/bin/mips-linux-gnu-readelf -A \"\$f\" | grep -q 'Hard float (32-bit CPU, 64-bit FPU)' || { echo \"Not hard-float FP64: \$f\"; FAIL=1; }
        echo \"-- NEEDED for \$f --\"
        $TOOL/bin/mips-linux-gnu-readelf -d \"\$f\" | grep NEEDED
    done
    [ \"\$FAIL\" -eq 0 ] || { echo 'ABI verification FAILED'; exit 1; }
    echo 'ABI verified: MIPS32, glibc dynamic link, hard-float FP64, NaN2008.'
"

cp /tmp/openke-openrc-build-*/out/supervise-daemon /tmp/openke-openrc-build-*/out/librc.so.1 /tmp/openke-openrc-build-*/out/libeinfo.so.1 "$OUT_DIR/" 2>/dev/null

echo "=== Done ==="
ls -la "$OUT_DIR/"
sha256sum "$OUT_DIR"/*
