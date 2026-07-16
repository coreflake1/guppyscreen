#!/bin/sh
# Cross-compile a streaming-form-data wheel for the KE's exact Python (3.8.2,
# mipsel-linux-gnu, glibc, hard-float FP64, NaN2008) and write it to
# scripts/vendor/wheels/, ready to drop into moonraker.tar.gz's
# moonraker/moonraker/scripts/python_wheels/ (see scripts/build-pillow-mipsel.sh's
# own header comment for the finalize step this shares).
#
# Uses the same pellcorp/k1-bash-build Docker image + cross-compiled CPython
# headers as scripts/build-pillow-mipsel.sh - see that script's header comment
# for the full gotcha writeup (the device has no Python dev headers at all;
# GCC's --sysroot + -I interaction demotes a sysroot -I to search-last, so
# --sysroot is deliberately dropped from the COMPILE step and kept only for
# linking; setuptools/PEP-621 metadata needs setuptools>=67.8).
#
# This package needed almost none of that complexity in practice: unlike
# Pillow, streaming-form-data's setup.py passes name/version as plain kwargs
# (no pyproject.toml [project] table at all), and it has zero external library
# dependencies (just compiles its own pre-generated _parser.c) - so there's no
# jpeg/zlib-equivalent step here. Confirmed this by reading the actual sdist's
# setup.py before assuming the same fixes would even be needed.

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VENDOR_SRC="$SCRIPT_DIR/vendor/streaming-form-data-src"
CROSS_INCLUDE_SRC="$SCRIPT_DIR/vendor/pillow-src/cross-include"
OUT_DIR="$SCRIPT_DIR/vendor/wheels"
K1_BASH_BUILD_IMAGE="pellcorp/k1-bash-build@sha256:0b96d1d65175c5a2e3a83a64c3212d08dd774fef0900f991e0ebc570ba896c85"
WORKDIR="/tmp/openke-sfd-build-$$"
TOOL=/opt/toolchains/mips-gcc720-glibc229
SYSROOT=/opt/k1-sysroot
PKG_VERSION=1.16.0

if ! command -v docker >/dev/null 2>&1; then
    echo "docker is required." >&2
    exit 1
fi
if [ ! -f "$CROSS_INCLUDE_SRC/Python.h" ]; then
    echo "Missing $CROSS_INCLUDE_SRC/Python.h - run scripts/build-pillow-mipsel.sh first" >&2
    echo "(it generates/caches the cross-compiled CPython headers this script reuses)." >&2
    exit 1
fi
mkdir -p "$OUT_DIR"

# ----------------------------------------------------------------------------
# Step 0: QEMU MIPS emulation, with the NaN2008 fix. Idempotent - checks first.
# ----------------------------------------------------------------------------
. "$SCRIPT_DIR/lib/ensure-mips-qemu.sh"
ensure_mips_qemu

# ----------------------------------------------------------------------------
# Step 1: stage everything into the per-run workdir (through docker, per the
# /tmp bind-mount gotcha documented in ensure-mips-qemu.sh).
# ----------------------------------------------------------------------------
echo "=== Staging sources ==="
docker run --rm -v /tmp:/tmp alpine sh -c "mkdir -p '$WORKDIR/cross-include'"
cp "$VENDOR_SRC/streaming-form-data-$PKG_VERSION.tar.gz" /tmp/
docker run --rm -v /tmp:/tmp alpine sh -c "
    mv /tmp/streaming-form-data-$PKG_VERSION.tar.gz '$WORKDIR/'
    chmod -R a+rwX '$WORKDIR'
"
docker run --rm -v /tmp:/tmp -v "$CROSS_INCLUDE_SRC:/vendored-cross-include:ro" alpine sh -c "
    cp -r /vendored-cross-include/. '$WORKDIR/cross-include/'
"

# ----------------------------------------------------------------------------
# Step 2: the wheel itself.
# ----------------------------------------------------------------------------
echo "=== Building streaming-form-data $PKG_VERSION wheel ==="
docker run --rm -v /tmp:/tmp -w "$WORKDIR" "$K1_BASH_BUILD_IMAGE" sh -c "
    set -e
    sudo apt-get install -y -qq python3 python3-pip >/dev/null 2>&1
    sudo rm -rf /usr/include/python3.8
    python3 -m pip install --user --upgrade -q 'setuptools>=67.8' wheel
    export PATH=\$HOME/.local/bin:\$PATH
    tar xzf streaming-form-data-$PKG_VERSION.tar.gz
    cd streaming-form-data-$PKG_VERSION

    export CC='$TOOL/bin/mips-linux-gnu-gcc -I$WORKDIR/cross-include -I$SYSROOT/usr/include'
    export LDSHARED='$TOOL/bin/mips-linux-gnu-gcc --sysroot=$SYSROOT -shared'

    python3 setup.py bdist_wheel --plat-name linux_mipsel --python-tag cp38 >/tmp/sfd-build.log 2>&1
    cp dist/streaming_form_data-$PKG_VERSION-cp38-cp38-linux_mipsel.whl '$WORKDIR/'
"

# ----------------------------------------------------------------------------
# Step 3: fix the .so suffix (host's leaked in despite --plat-name/--python-tag
# - those only affect the WHEEL's own filename, not the extension suffix
# setuptools bakes into each .so, which is derived from the HOST's sysconfig).
# ----------------------------------------------------------------------------
echo "=== Fixing .so suffix + repackaging ==="
docker run --rm -v /tmp:/tmp -w "$WORKDIR" alpine sh -c "
    set -e
    apk add --no-cache unzip zip >/dev/null 2>&1
    rm -rf extracted && mkdir extracted && cd extracted
    unzip -q '$WORKDIR/streaming_form_data-$PKG_VERSION-cp38-cp38-linux_mipsel.whl'
    for f in streaming_form_data/*.so; do
        new=\$(echo \"\$f\" | sed 's/cpython-38-x86_64-linux-gnu/cpython-38-mipsel-linux-gnu/')
        mv \"\$f\" \"\$new\"
    done
    sed -i 's/cpython-38-x86_64-linux-gnu/cpython-38-mipsel-linux-gnu/g' streaming_form_data-$PKG_VERSION.dist-info/RECORD
    rm -f '$WORKDIR/streaming_form_data-$PKG_VERSION-cp38-cp38-linux_mipsel.whl'
    zip -qr -X '$WORKDIR/streaming_form_data-$PKG_VERSION-cp38-cp38-linux_mipsel.whl' .
"

# ----------------------------------------------------------------------------
# Step 4: verify the ABI (not just "it built without error").
# ----------------------------------------------------------------------------
echo "=== Verifying ABI ==="
docker run --rm -v /tmp:/tmp -w "$WORKDIR/extracted" "$K1_BASH_BUILD_IMAGE" sh -c "
    set -e
    FAIL=0
    for f in streaming_form_data/*.so; do
        $TOOL/bin/mips-linux-gnu-readelf -h \"\$f\" | grep -q 'Machine:.*MIPS' || { echo \"Not MIPS: \$f\"; FAIL=1; }
        $TOOL/bin/mips-linux-gnu-readelf -h \"\$f\" | grep -q 'nan2008' || { echo \"Not NaN2008: \$f\"; FAIL=1; }
        $TOOL/bin/mips-linux-gnu-readelf -A \"\$f\" | grep -q 'Hard float (32-bit CPU, 64-bit FPU)' || { echo \"Not hard-float FP64: \$f\"; FAIL=1; }
        echo \"-- NEEDED for \$f --\"
        $TOOL/bin/mips-linux-gnu-readelf -d \"\$f\" | grep NEEDED
    done
    [ \"\$FAIL\" -eq 0 ] || { echo 'ABI verification FAILED'; exit 1; }
    echo 'ABI verified: MIPS32, glibc dynamic link, hard-float FP64, NaN2008.'
"

cp /tmp/openke-sfd-build-*/streaming_form_data-$PKG_VERSION-cp38-cp38-linux_mipsel.whl "$OUT_DIR/" 2>/dev/null || \
    docker run --rm -v /tmp:/tmp alpine cat "$WORKDIR/streaming_form_data-$PKG_VERSION-cp38-cp38-linux_mipsel.whl" > "$OUT_DIR/streaming_form_data-$PKG_VERSION-cp38-cp38-linux_mipsel.whl"

echo "=== Done ==="
echo "Wrote $OUT_DIR/streaming_form_data-$PKG_VERSION-cp38-cp38-linux_mipsel.whl"
sha256sum "$OUT_DIR/streaming_form_data-$PKG_VERSION-cp38-cp38-linux_mipsel.whl"
