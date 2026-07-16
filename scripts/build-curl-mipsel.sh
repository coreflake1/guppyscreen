#!/bin/sh
# Cross-compile curl 7.68.0 (+ its own OpenSSL 1.1.1 and zlib link-time deps)
# for the KE (mipsel, glibc, hard-float FP64, NaN2008) and write curl-mipsel
# to scripts/vendor/, ready to replace the existing vendored binary.
#
# Uses the same pellcorp/k1-bash-build Docker image as the other build-*-
# mipsel.sh scripts. Confirmed via readelf against the currently-vendored
# curl-mipsel before writing this: despite installer.sh's own comment calling
# it a "Static MIPS curl binary", it's actually dynamically linked against
# libcurl.so.4 + libssl.so.1.1 + libcrypto.so.1.1 + libz.so.1 (all already
# present on the device's stock firmware, which is why the existing fallback
# has always worked reliably in practice) - that comment is stale/inaccurate,
# not something this script tries to unilaterally redesign into a true static
# binary (a bigger, different-shaped change). This rebuild faithfully matches
# the SAME dynamic-linking shape: builds its own OpenSSL/zlib copies only for
# headers + link-time SONAME stubs, exactly like jpeg/zlib were built for
# Pillow - the actual runtime resolution uses the device's own already-present
# libssl.so.1.1/libcrypto.so.1.1/libz.so.1, confirmed via readelf against the
# original binary before assuming this is safe.
#
# Unlike the Pillow build, this needs NO cross-compiled Python headers and
# hits NONE of that script's setuptools-specific gotchas (the GCC --sysroot +
# -I demotion bug is specifically triggered by setuptools injecting competing
# host -I flags after a custom CC - plain autotools builds like zlib/OpenSSL/
# curl don't do that, confirmed by nginx's own build already using --sysroot
# throughout without issue) - so --sysroot is used normally for both compiling
# and linking here, the simpler case.

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VENDOR_SRC="$SCRIPT_DIR/vendor/curl-src"
OUT_FILE="$SCRIPT_DIR/vendor/curl-mipsel.new"
K1_BASH_BUILD_IMAGE="pellcorp/k1-bash-build@sha256:0b96d1d65175c5a2e3a83a64c3212d08dd774fef0900f991e0ebc570ba896c85"
WORKDIR="/tmp/openke-curl-build-$$"
TOOL=/opt/toolchains/mips-gcc720-glibc229
SYSROOT=/opt/k1-sysroot
CURL_VERSION=7.68.0
OPENSSL_VERSION=1.1.1w
ZLIB_VERSION=1.2.11

if ! command -v docker >/dev/null 2>&1; then
    echo "docker is required." >&2
    exit 1
fi

# zlib/OpenSSL/curl's own ./configure scripts can try to compile-and-run a
# small test binary for some platform checks - make sure QEMU is ready first,
# same as nginx/Pillow needed.
. "$SCRIPT_DIR/lib/ensure-mips-qemu.sh"
ensure_mips_qemu

echo "=== Staging sources ==="
docker run --rm -v /tmp:/tmp alpine sh -c "mkdir -p '$WORKDIR'"
cp "$VENDOR_SRC/curl-$CURL_VERSION.tar.gz" "$VENDOR_SRC/openssl-$OPENSSL_VERSION.tar.gz" "$VENDOR_SRC/zlib-$ZLIB_VERSION.tar.gz" /tmp/
docker run --rm -v /tmp:/tmp alpine sh -c "
    mv /tmp/curl-$CURL_VERSION.tar.gz /tmp/openssl-$OPENSSL_VERSION.tar.gz /tmp/zlib-$ZLIB_VERSION.tar.gz '$WORKDIR/'
    chmod -R a+rwX '$WORKDIR'
"

CC="$TOOL/bin/mips-linux-gnu-gcc --sysroot=$SYSROOT"

echo "=== Building zlib $ZLIB_VERSION (link-time stub only - device has its own libz.so.1) ==="
docker run --rm -v /tmp:/tmp -w "$WORKDIR" "$K1_BASH_BUILD_IMAGE" sh -c "
    set -e
    tar xzf zlib-$ZLIB_VERSION.tar.gz
    cd zlib-$ZLIB_VERSION
    CC='$CC' ./configure --prefix='$WORKDIR/zlib-install' >/tmp/zlib-configure.log 2>&1
    make -j4 >/tmp/zlib-make.log 2>&1
    make install >/tmp/zlib-install.log 2>&1
"

echo "=== Building OpenSSL $OPENSSL_VERSION (link-time stub only - device has its own libssl.so.1.1/libcrypto.so.1.1) ==="
docker run --rm -v /tmp:/tmp -w "$WORKDIR" "$K1_BASH_BUILD_IMAGE" sh -c "
    set -e
    tar xzf openssl-$OPENSSL_VERSION.tar.gz
    cd openssl-$OPENSSL_VERSION
    CC='$TOOL/bin/mips-linux-gnu-gcc' \
    AR='$TOOL/bin/mips-linux-gnu-ar' \
    RANLIB='$TOOL/bin/mips-linux-gnu-ranlib' \
    ./Configure linux-generic32 --sysroot=$SYSROOT shared no-tests \
        --prefix='$WORKDIR/openssl-install' >/tmp/openssl-configure.log 2>&1
    make -j4 >/tmp/openssl-make.log 2>&1
    make install_sw >/tmp/openssl-install.log 2>&1
"

echo "=== Building curl $CURL_VERSION ==="
docker run --rm -v /tmp:/tmp -w "$WORKDIR" "$K1_BASH_BUILD_IMAGE" sh -c "
    set -e
    tar xzf curl-$CURL_VERSION.tar.gz
    cd curl-$CURL_VERSION
    CC='$CC' \
    LDFLAGS='-Wl,-rpath-link,$WORKDIR/openssl-install/lib' \
    ./configure --host=mips-linux-gnu --build=x86_64-linux-gnu \
        --with-ssl='$WORKDIR/openssl-install' \
        --with-zlib='$WORKDIR/zlib-install' \
        --disable-static --enable-shared \
        --disable-manual \
        >/tmp/curl-configure.log 2>&1
    make -j4 >/tmp/curl-make.log 2>&1
    mkdir -p '$WORKDIR/out'
    # src/curl is a libtool wrapper SHELL SCRIPT (sets LD_LIBRARY_PATH then
    # execs the real binary) - the actual cross-compiled ELF lives at
    # src/.libs/curl. Confirmed by size (8KB text script vs a real binary)
    # after the first attempt at this copied the wrapper by mistake.
    cp src/.libs/curl '$WORKDIR/out/curl-mipsel'
    cp lib/.libs/libcurl.so.4.* '$WORKDIR/out/' 2>/dev/null || cp lib/.libs/libcurl.so* '$WORKDIR/out/'
"

echo "=== Verifying ABI ==="
docker run --rm -v /tmp:/tmp -w "$WORKDIR/out" "$K1_BASH_BUILD_IMAGE" sh -c "
    set -e
    FAIL=0
    BIN=curl-mipsel
    $TOOL/bin/mips-linux-gnu-readelf -h \"\$BIN\" | grep -q 'Machine:.*MIPS' || { echo 'Not MIPS'; FAIL=1; }
    $TOOL/bin/mips-linux-gnu-readelf -h \"\$BIN\" | grep -q 'nan2008' || { echo 'Not NaN2008'; FAIL=1; }
    $TOOL/bin/mips-linux-gnu-readelf -A \"\$BIN\" | grep -q 'Hard float (32-bit CPU, 64-bit FPU)' || { echo 'Not hard-float FP64'; FAIL=1; }
    echo '-- NEEDED --'
    $TOOL/bin/mips-linux-gnu-readelf -d \"\$BIN\" | grep NEEDED
    [ \"\$FAIL\" -eq 0 ] || { echo 'ABI verification FAILED'; exit 1; }
    echo 'ABI verified: MIPS32, glibc dynamic link, hard-float FP64, NaN2008.'
"

docker run --rm -v /tmp:/tmp alpine cat "$WORKDIR/out/curl-mipsel" > "$OUT_FILE"
chmod +x "$OUT_FILE"
docker run --rm -v /tmp:/tmp alpine rm -rf "$WORKDIR"

echo "=== Done ==="
ls -la "$OUT_FILE"
sha256sum "$OUT_FILE"
