#!/bin/sh
# Cross-compile matplotlib's ft2font extension (matplotlib.ft2font, used for
# PSD-graph plotting - input shaper calibration, etc.) for the KE (mipsel,
# glibc, hard-float FP64, NaN2008) and write ft2font.cpython-38-mipsel-linux-
# gnu.so to scripts/vendor/wheels/, ready to replace
# k1/k1_mods/ft2font.cpython-38-mipsel-linux-gnu.so.
#
# ============================================================================
# PROVENANCE, reconstructed 2026-07-16 (this file had none before - present
# since the project's first commit with no known build recipe)
# ============================================================================
# The currently-vendored .so is NOT the stock module for this device's
# installed matplotlib (2.2.3, pure Python + its own much older ft2font) - the
# strings embedded in it (deprecation messages, PY_ARRAY_UNIQUE_SYMBOL =
# MPL_matplotlib_ft2font_ARRAY_API) are a byte-for-byte match to matplotlib
# 3.3.4's src/ft2font_wrapper.cpp specifically (confirmed by downloading that
# exact source and diffing the embedded deprecation strings - all 5 matched
# exactly, matplotlib version and all). It was likely built standalone
# (matching Python 3.8's ABI, not the installed matplotlib's) by whoever
# maintains the "improved-shapers" Helper-Script feature, since the *stock*
# 2.2.3-era ft2font apparently didn't build/run correctly on this MIPS target.
# This script reconstructs that same standalone build rather than replicating
# stock matplotlib's own (much more complex, auto-downloading) setup.py -
# only 4 source files are actually needed for this one extension:
#   src/ft2font.cpp, src/ft2font_wrapper.cpp, src/mplutils.cpp,
#   src/py_converters.cpp
# (confirmed from matplotlib's own setupext.py FT2Font extension definition -
# read the actual source rather than guessing), plus matplotlib's own bundled
# extern/agg24-svn/include headers (mplutils.h references a couple of Agg
# types - no Agg .cpp files need compiling, just its headers).
#
# Needs, besides the usual cross-compiled CPython headers:
# - FreeType (device already has libfreetype.so.6.17.1 at runtime - built our
#   own 2.10.1 here only for headers + a matching link-time SONAME stub,
#   picked because FreeType's libtool versioning table maps 2.10.1 to exactly
#   that .so.6.17.1 suffix - same "build a stub, run against the device's own
#   copy" pattern as jpeg/zlib/OpenSSL in the other build-*-mipsel.sh scripts).
# - NumPy 1.16.4 headers (confirmed exact installed version via SSH - ft2font
#   uses the NumPy C-API extensively). NumPy's numpyconfig.h needs a build-
#   generated _numpyconfig.h that doesn't exist in the raw sdist - rather than
#   regenerating it (a whole separate numpy build), fetched the real one
#   directly off the device (read-only, it's small and target-specific -
#   NPY_SIZEOF_PY_INTPTR_T=4 etc, confirming the 32-bit target) and vendored
#   it here since there's no other source for it.
# - This is C++ (not C like Pillow/streaming-form-data), so linking pulls in
#   libstdc++.so.6/libgcc_s.so.1/libm.so.6 too - confirmed via readelf against
#   the original binary's NEEDED list before writing this, and g++ (not gcc)
#   is used for the final link step so those come along automatically.

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VENDOR_SRC="$SCRIPT_DIR/vendor/ft2font-src"
CROSS_INCLUDE_SRC="$SCRIPT_DIR/vendor/pillow-src/cross-include"
OUT_DIR="$SCRIPT_DIR/vendor/wheels"
K1_BASH_BUILD_IMAGE="pellcorp/k1-bash-build@sha256:0b96d1d65175c5a2e3a83a64c3212d08dd774fef0900f991e0ebc570ba896c85"
WORKDIR="/tmp/openke-ft2font-build-$$"
TOOL=/opt/toolchains/mips-gcc720-glibc229
SYSROOT=/opt/k1-sysroot
MPL_VERSION=3.3.4
FT_VERSION=2.10.1

if ! command -v docker >/dev/null 2>&1; then
    echo "docker is required." >&2
    exit 1
fi
if [ ! -f "$CROSS_INCLUDE_SRC/Python.h" ]; then
    echo "Missing $CROSS_INCLUDE_SRC/Python.h - run scripts/build-pillow-mipsel.sh first." >&2
    exit 1
fi
if [ ! -f "$VENDOR_SRC/numpy-1.16.4/numpy/core/include/numpy/_numpyconfig.h" ]; then
    echo "Missing vendored numpy _numpyconfig.h - see this script's header comment." >&2
    exit 1
fi
mkdir -p "$OUT_DIR"

. "$SCRIPT_DIR/lib/ensure-mips-qemu.sh"
ensure_mips_qemu

echo "=== Staging sources ==="
docker run --rm -v /tmp:/tmp alpine sh -c "mkdir -p '$WORKDIR/cross-include' '$WORKDIR/numpy-include'"
cp "$VENDOR_SRC/freetype-$FT_VERSION.tar.gz" "$VENDOR_SRC/matplotlib-$MPL_VERSION.tar.gz" /tmp/
docker run --rm -v /tmp:/tmp alpine sh -c "
    mv /tmp/freetype-$FT_VERSION.tar.gz /tmp/matplotlib-$MPL_VERSION.tar.gz '$WORKDIR/'
    chmod -R a+rwX '$WORKDIR'
"
docker run --rm -v /tmp:/tmp -v "$CROSS_INCLUDE_SRC:/vendored-cross-include:ro" alpine sh -c "
    cp -r /vendored-cross-include/. '$WORKDIR/cross-include/'
"
docker run --rm -v /tmp:/tmp -v "$VENDOR_SRC/numpy-1.16.4/numpy/core/include:/vendored-numpy-include:ro" alpine sh -c "
    cp -r /vendored-numpy-include/. '$WORKDIR/numpy-include/'
"

echo "=== Building FreeType $FT_VERSION (headers + link-time stub - device has its own libfreetype.so.6.17.1) ==="
docker run --rm -v /tmp:/tmp -w "$WORKDIR" "$K1_BASH_BUILD_IMAGE" sh -c "
    set -e
    tar xzf freetype-$FT_VERSION.tar.gz
    cd freetype-$FT_VERSION
    CC='$TOOL/bin/mips-linux-gnu-gcc --sysroot=$SYSROOT' \
    ./configure --host=mips-linux-gnu --build=x86_64-linux-gnu \
        --prefix='$WORKDIR/freetype-install' \
        --with-zlib=no --with-bzip2=no --with-png=no --with-harfbuzz=no \
        --disable-static --enable-shared \
        >/tmp/freetype-configure.log 2>&1
    make -j4 >/tmp/freetype-make.log 2>&1
    make install >/tmp/freetype-install.log 2>&1
"

echo "=== Extracting matplotlib $MPL_VERSION's ft2font sources ==="
docker run --rm -v /tmp:/tmp -w "$WORKDIR" alpine sh -c "
    set -e
    tar xzf matplotlib-$MPL_VERSION.tar.gz \
        matplotlib-$MPL_VERSION/src/ft2font.cpp \
        matplotlib-$MPL_VERSION/src/ft2font.h \
        matplotlib-$MPL_VERSION/src/ft2font_wrapper.cpp \
        matplotlib-$MPL_VERSION/src/mplutils.cpp \
        matplotlib-$MPL_VERSION/src/mplutils.h \
        matplotlib-$MPL_VERSION/src/py_converters.cpp \
        matplotlib-$MPL_VERSION/src/py_converters.h \
        matplotlib-$MPL_VERSION/src/py_exceptions.h \
        matplotlib-$MPL_VERSION/src/numpy_cpp.h \
        matplotlib-$MPL_VERSION/src/_backend_agg_basic_types.h \
        matplotlib-$MPL_VERSION/src/path_converters.h \
        matplotlib-$MPL_VERSION/src/py_adaptors.h \
        matplotlib-$MPL_VERSION/src/array.h \
        matplotlib-$MPL_VERSION/extern/agg24-svn/include
"

echo "=== Staging libstdc++ (toolchain's own copy lives in ITS OWN internal sysroot, not our custom \$SYSROOT - copying just the .so out avoids the linker seeing two conflicting glibc copies) ==="
docker run --rm -v /tmp:/tmp "$K1_BASH_BUILD_IMAGE" sh -c "
    mkdir -p '$WORKDIR/libstdcxx'
    cp $TOOL/mips-linux-gnu/libc/usr/lib/libstdc++.so.6.0.24 '$WORKDIR/libstdcxx/'
    ln -sf libstdc++.so.6.0.24 '$WORKDIR/libstdcxx/libstdc++.so.6'
    ln -sf libstdc++.so.6.0.24 '$WORKDIR/libstdcxx/libstdc++.so'
"

echo "=== Compiling ft2font ==="
docker run --rm -v /tmp:/tmp -w "$WORKDIR/matplotlib-$MPL_VERSION" "$K1_BASH_BUILD_IMAGE" sh -c "
    set -e
    CXX='$TOOL/bin/mips-linux-gnu-g++ --sysroot=$SYSROOT'
    INCLUDES='-I$WORKDIR/cross-include -I$SYSROOT/usr/include -Isrc -I$WORKDIR/numpy-include -Iextern/agg24-svn/include -I$WORKDIR/freetype-install/include/freetype2'
    DEFINES='-DPY_ARRAY_UNIQUE_SYMBOL=MPL_matplotlib_ft2font_ARRAY_API -DNPY_NO_DEPRECATED_API=NPY_1_7_API_VERSION -D__STDC_FORMAT_MACROS=1'
    mkdir -p '$WORKDIR/obj'
    for f in src/ft2font.cpp src/ft2font_wrapper.cpp src/mplutils.cpp src/py_converters.cpp; do
        name=\$(basename \"\$f\" .cpp)
        \$CXX -std=c++14 -fPIC \$INCLUDES \$DEFINES -O2 -c \"\$f\" -o \"$WORKDIR/obj/\$name.o\"
    done
    \$CXX -shared -fPIC $WORKDIR/obj/*.o \
        -L$WORKDIR/freetype-install/lib -lfreetype \
        -L$WORKDIR/libstdcxx -lstdc++ \
        -o '$WORKDIR/ft2font.cpython-38-mipsel-linux-gnu.so'
" 2>&1 | tee /tmp/ft2font-build.log | tail -60

echo "=== Verifying ABI ==="
docker run --rm -v /tmp:/tmp -w "$WORKDIR" "$K1_BASH_BUILD_IMAGE" sh -c "
    set -e
    F=ft2font.cpython-38-mipsel-linux-gnu.so
    FAIL=0
    $TOOL/bin/mips-linux-gnu-readelf -h \"\$F\" | grep -q 'Machine:.*MIPS' || { echo 'Not MIPS'; FAIL=1; }
    $TOOL/bin/mips-linux-gnu-readelf -h \"\$F\" | grep -q 'nan2008' || { echo 'Not NaN2008'; FAIL=1; }
    $TOOL/bin/mips-linux-gnu-readelf -A \"\$F\" | grep -q 'Hard float (32-bit CPU, 64-bit FPU)' || { echo 'Not hard-float FP64'; FAIL=1; }
    echo '-- NEEDED --'
    $TOOL/bin/mips-linux-gnu-readelf -d \"\$F\" | grep NEEDED
    [ \"\$FAIL\" -eq 0 ] || { echo 'ABI verification FAILED'; exit 1; }
    echo 'ABI verified: MIPS32, glibc dynamic link, hard-float FP64, NaN2008.'
"

docker run --rm -v /tmp:/tmp alpine cat "$WORKDIR/ft2font.cpython-38-mipsel-linux-gnu.so" > "$OUT_DIR/ft2font.cpython-38-mipsel-linux-gnu.so"
docker run --rm -v /tmp:/tmp alpine rm -rf "$WORKDIR"

echo "=== Done ==="
ls -la "$OUT_DIR/ft2font.cpython-38-mipsel-linux-gnu.so"
sha256sum "$OUT_DIR/ft2font.cpython-38-mipsel-linux-gnu.so"
