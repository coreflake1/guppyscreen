#!/bin/sh
# Cross-compile a Pillow wheel for the KE's exact Python (3.8.2, mipsel-linux-gnu, glibc,
# hard-float FP64, NaN2008) and write it to scripts/vendor/wheels/, ready to drop into
# moonraker.tar.gz's moonraker/moonraker/scripts/python_wheels/ (see finalize step at the
# bottom of this file's usage notes).
#
# Uses the same pellcorp/k1-bash-build Docker image (pinned by digest below) that
# scripts/build-nginx-mipsel.sh uses - bundles the mips-gcc720-glibc229 cross toolchain +
# sysroot. Requires Docker.
#
# All source inputs are vendored in scripts/vendor/pillow-src/ (never depends on
# python.org/ijg.org/PyPI staying up): Python-3.8.2.tar.xz, jpegsrc.v9e.tar.gz,
# zlib-1.2.11.tar.gz, pillow-10.4.0.tar.gz, and a pre-generated cross-include/ (CPython
# headers, see Gotcha #1 below - normally reused as-is, regenerated only if missing).
#
# ============================================================================
# WHY THIS SCRIPT EXISTS: four real, non-obvious bugs, each hit for real building this
# the first time (2026-07-16). Read this before touching the build steps below - every
# one of these looks like it should "just work" a different, simpler way, and doesn't.
# ============================================================================
#
# GOTCHA #0 (shared with nginx): QEMU MIPS/NaN2008 emulation setup - see
# scripts/lib/ensure-mips-qemu.sh for the full writeup. Handled by that shared script.
#
# GOTCHA #1: the device has NO Python dev headers at all (`/usr/include/python3.8/`
# doesn't exist - confirmed via SSH). Extensions need the TARGET's own Python.h/
# pyconfig.h (struct layouts like SIZEOF_TIME_T are target-specific, the host's headers
# are silently wrong). Fix: cross-compile CPython 3.8.2 itself (matching the device's
# exact version) just far enough to generate a correct pyconfig.h, using the device's own
# captured CONFIG_ARGS/CFLAGS (from /usr/lib/python3.8/config-3.8-mipsel-linux-gnu/Makefile
# on the real device), combined with CPython's own static Include/*.h. This is SLOW
# (a real ./configure + partial build), so the result is cached and vendored directly in
# scripts/vendor/pillow-src/cross-include/ - this script only regenerates it if that
# directory is missing Python.h, which should never happen unless the device's Python
# version changes in a future firmware (see "To regenerate cross-include" below).
# **Verified live against the real device**, not just assumed: pyconfig.h shows
# SIZEOF_TIME_T=4 despite -D__USE_TIME_BITS64 in the captured CFLAGS (looks like it should
# mean 64-bit time_t) - SSH'd in and confirmed sysconfig.get_config_var('SIZEOF_TIME_T')
# and ctypes.sizeof(ctypes.c_long) both really are 4 on the device too. The flag is simply
# inert on this glibc build; the cross-compiled header is correct, not a bug.
#
# GOTCHA #2: a genuine GCC `--sysroot` + `-I` interaction bug, NOT a simple ordering
# mistake. Building any extension with `--sysroot=$SYSROOT -I$SYSROOT/usr/include
# -I/usr/include` (setuptools always auto-appends `-I/usr/include -I/usr/local/include
# -I/usr/include/python3.8`, the BUILD HOST's x86_64 paths, after any custom CC flags)
# fails with `fatal error: bits/unistd_ext.h: No such file or directory`, sourced from
# the HOST's /usr/include/unistd.h - even though the sysroot's -I comes FIRST on the
# command line. Root cause (confirmed via `-H` header-trace diffing a minimal repro
# against the real failing command): when `--sysroot=X` is passed, GCC's own compiled-in
# default search list gets rewritten so its last entry becomes `X/usr/include`. GCC has a
# documented rule that a `-I` directory IDENTICAL to one of its own standard/default
# directories gets silently demoted to search LAST (at the position the standard dir
# would occupy), regardless of where the user put it on the command line. Switching the
# sysroot path to `-isystem` makes it WORSE (`-isystem` is unconditionally searched after
# ALL `-I` flags). Adding `-nostdinc` does NOT fix it either (empirically still demoted).
# **The fix that actually works**: drop `--sysroot=...` from the COMPILE step's CC
# entirely (keep plain `-I$CROSS_INC -I$SYSROOT/usr/include`, correctly ordered, with no
# path that coincidentally matches a GCC default to trigger demotion) - keep `--sysroot`
# only on the LINK step (LDSHARED), where it's still needed for crt/library search. If
# this ever needs re-diagnosing (e.g. a toolchain upgrade changes behavior), verify with
# `gcc ... -H -fsyntax-only some.c 2>&1 | grep -B2 unistd_ext` rather than trusting argv
# order - the failing file's OWN reported path (host vs sysroot) is the tell.
#
# GOTCHA #3: libjpeg must be built `-fPIC` AND `--disable-shared --enable-static`.
# Without `-fPIC`: link fails (`relocation R_MIPS_26 against 'memset' can not be used
# when making a shared object`). Without `--disable-shared`: configure's default
# `--enable-shared` produces a libjpeg.so too, and the linker prefers it over the .a,
# making `_imaging.so` depend on `libjpeg.so.9` at runtime - NOT guaranteed present on
# the device. Static jpeg + dynamic-against-the-device's-existing-system zlib
# (libz.so.1, confirmed already shipped on stock firmware) was the original design
# intent from an earlier (native-build, since-abandoned) attempt at this same problem -
# this preserves it. Verify via `readelf -d _imaging*.so | grep NEEDED` - must show
# libc.so.6 + ld-linux-mipsn8.so.1 + libz.so.1, and NEVER libjpeg.so.9.
#
# GOTCHA #4: the built wheel comes out named `UNKNOWN-0.0.0-cp38-cp38-linux_x86_64.whl`
# even via classic `setup.py bdist_wheel` (not just pip's PEP 517 path). Root cause:
# Ubuntu 20.04's stock apt `python3-setuptools` (~45.2) predates PEP 621
# `pyproject.toml [project]` table support (added ~setuptools 61+), so Pillow's
# `name = "pillow"` / `dynamic = ["version"]` (resolved via PIL.__version__) is silently
# dropped. Fix: `pip install --user --upgrade 'setuptools>=67.8'` (the exact floor
# Pillow's own pyproject.toml [build-system] already declares) before building. Also
# needs `--plat-name linux_mipsel --python-tag cp38` on bdist_wheel (the host's own tags
# leak in otherwise, since the build is driven by the HOST's python3) and a manual
# post-build rename of the .so suffix from the host's `cpython-38-x86_64-linux-gnu` to
# the device's real `cpython-38-mipsel-linux-gnu` (derived from the device's own
# confirmed `config-3.8-mipsel-linux-gnu` dir name - CPython's SOABI mirrors the
# config-dir's platform triplet by construction), plus the matching RECORD-file filename
# fixup (hashes are unchanged since only the path changes, not file content).
# ============================================================================
#
# To regenerate cross-include/ from scratch (only needed if the device's Python version
# ever changes): delete scripts/vendor/pillow-src/cross-include/, re-run this script. It
# will cross-compile CPython using the CONFIG_ARGS/CFLAGS baked in below - if the
# device's actual config differs (check via SSH:
# /usr/lib/python3.8/config-3.8-*/Makefile), update those values first.

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VENDOR_SRC="$SCRIPT_DIR/vendor/pillow-src"
OUT_DIR="$SCRIPT_DIR/vendor/wheels"
K1_BASH_BUILD_IMAGE="pellcorp/k1-bash-build@sha256:0b96d1d65175c5a2e3a83a64c3212d08dd774fef0900f991e0ebc570ba896c85"
WORKDIR="/tmp/openke-pillow-build-$$"
TOOL=/opt/toolchains/mips-gcc720-glibc229
SYSROOT=/opt/k1-sysroot

if ! command -v docker >/dev/null 2>&1; then
    echo "docker is required." >&2
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
docker run --rm -v /tmp:/tmp alpine sh -c "mkdir -p '$WORKDIR'"
cp "$VENDOR_SRC/Python-3.8.2.tar.xz" "$VENDOR_SRC/jpegsrc.v9e.tar.gz" "$VENDOR_SRC/zlib-1.2.11.tar.gz" "$VENDOR_SRC/pillow-10.4.0.tar.gz" /tmp/
docker run --rm -v /tmp:/tmp alpine sh -c "
    mv /tmp/Python-3.8.2.tar.xz /tmp/jpegsrc.v9e.tar.gz /tmp/zlib-1.2.11.tar.gz /tmp/pillow-10.4.0.tar.gz '$WORKDIR/'
    mkdir -p '$WORKDIR/cross-include'
    chmod -R a+rwX '$WORKDIR'
"
if [ -f "$VENDOR_SRC/cross-include/Python.h" ]; then
    echo "Using cached, device-verified cross-include/ headers (skipping CPython cross-compile)."
    docker run --rm -v /tmp:/tmp -v "$VENDOR_SRC/cross-include:/vendored-cross-include:ro" alpine sh -c "
        cp -r /vendored-cross-include/. '$WORKDIR/cross-include/'
    "
else
    echo "=== cross-include/ missing Python.h - regenerating from CPython 3.8.2 source (slow) ==="
    docker run --rm -v /tmp:/tmp -w "$WORKDIR" "$K1_BASH_BUILD_IMAGE" sh -c "
        set -e
        tar xf Python-3.8.2.tar.xz
        cd Python-3.8.2
        export CC='$TOOL/bin/mips-linux-gnu-gcc --sysroot=$SYSROOT'
        export CPP='$TOOL/bin/mips-linux-gnu-gcc --sysroot=$SYSROOT -E'
        export AR='$TOOL/bin/mips-linux-gnu-ar'
        export RANLIB='$TOOL/bin/mips-linux-gnu-ranlib'
        export READELF='$TOOL/bin/mips-linux-gnu-readelf'
        export CFLAGS='-D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -D__USE_TIME_BITS64 -Os'
        export ac_cv_file__dev_ptmx=yes
        export ac_cv_file__dev_ptc=no
        ./configure --host=mips-linux-gnu --build=x86_64-linux-gnu --disable-ipv6 >/tmp/cpython-configure.log 2>&1
        cp pyconfig.h '$WORKDIR/cross-include/'
        cp -r Include/. '$WORKDIR/cross-include/'
    "
    echo "Regenerated. Copy $WORKDIR/cross-include/ back into $VENDOR_SRC/cross-include/ and"
    echo "commit it (after re-verifying SIZEOF_TIME_T etc against the real device via SSH -"
    echo "see Gotcha #1 above) so future runs don't redo this step."
fi

# ----------------------------------------------------------------------------
# Step 2: libjpeg 9e, static + fPIC only (Gotcha #3).
# ----------------------------------------------------------------------------
echo "=== Building libjpeg 9e (static, -fPIC) ==="
docker run --rm -v /tmp:/tmp -w "$WORKDIR" "$K1_BASH_BUILD_IMAGE" sh -c "
    set -e
    tar xzf jpegsrc.v9e.tar.gz
    cd jpeg-9e
    CC='$TOOL/bin/mips-linux-gnu-gcc --sysroot=$SYSROOT -fPIC' \
    CFLAGS='-fPIC -O2' \
    ./configure --host=mips-linux-gnu --prefix='$WORKDIR/jpeg-install' --disable-shared --enable-static >/tmp/jpeg-configure.log 2>&1
    make -j4 >/tmp/jpeg-make.log 2>&1
    make install >/tmp/jpeg-install.log 2>&1
"

# ----------------------------------------------------------------------------
# Step 3: zlib - built only to provide a link-time stub; the actual runtime
# dependency is the device's OWN existing system libz.so.1 (Gotcha #3 note).
# ----------------------------------------------------------------------------
echo "=== Building zlib 1.2.11 (link-time stub only) ==="
docker run --rm -v /tmp:/tmp -w "$WORKDIR" "$K1_BASH_BUILD_IMAGE" sh -c "
    set -e
    tar xzf zlib-1.2.11.tar.gz
    cd zlib-1.2.11
    CC='$TOOL/bin/mips-linux-gnu-gcc --sysroot=$SYSROOT' \
    ./configure --prefix='$WORKDIR/zlib-install' >/tmp/zlib-configure.log 2>&1
    make -j4 >/tmp/zlib-make.log 2>&1
    make install >/tmp/zlib-install.log 2>&1
"

# ----------------------------------------------------------------------------
# Step 4: the Pillow wheel itself (Gotcha #2 for CC, Gotcha #4 for tags/naming).
# ----------------------------------------------------------------------------
echo "=== Building Pillow 10.4.0 wheel ==="
docker run --rm -v /tmp:/tmp -w "$WORKDIR" "$K1_BASH_BUILD_IMAGE" sh -c "
    set -e
    sudo apt-get install -y -qq python3 python3-pip >/dev/null 2>&1
    sudo rm -rf /usr/include/python3.8
    python3 -m pip install --user --upgrade -q 'setuptools>=67.8' wheel
    export PATH=\$HOME/.local/bin:\$PATH
    tar xzf pillow-10.4.0.tar.gz
    cd pillow-10.4.0

    export CC='$TOOL/bin/mips-linux-gnu-gcc -I$WORKDIR/cross-include -I$SYSROOT/usr/include'
    export LDSHARED='$TOOL/bin/mips-linux-gnu-gcc --sysroot=$SYSROOT -shared'
    export JPEG_ROOT='$WORKDIR/jpeg-install'
    export ZLIB_ROOT='$WORKDIR/zlib-install'

    python3 setup.py bdist_wheel --plat-name linux_mipsel --python-tag cp38 >/tmp/pillow-build.log 2>&1
    cp dist/pillow-10.4.0-cp38-cp38-linux_mipsel.whl '$WORKDIR/'
"

# ----------------------------------------------------------------------------
# Step 5: fix the .so suffix (host's leaked in despite --plat-name/--python-tag
# - those only affect the WHEEL's own filename, not the extension suffix
# setuptools bakes into each .so, which is derived from the HOST's sysconfig).
# ----------------------------------------------------------------------------
echo "=== Fixing .so suffix + repackaging ==="
docker run --rm -v /tmp:/tmp -w "$WORKDIR" alpine sh -c "
    set -e
    apk add --no-cache unzip zip >/dev/null 2>&1
    rm -rf extracted && mkdir extracted && cd extracted
    unzip -q '$WORKDIR/pillow-10.4.0-cp38-cp38-linux_mipsel.whl'
    for f in PIL/*.so; do
        new=\$(echo \"\$f\" | sed 's/cpython-38-x86_64-linux-gnu/cpython-38-mipsel-linux-gnu/')
        mv \"\$f\" \"\$new\"
    done
    sed -i 's/cpython-38-x86_64-linux-gnu/cpython-38-mipsel-linux-gnu/g' pillow-10.4.0.dist-info/RECORD
    rm -f '$WORKDIR/pillow-10.4.0-cp38-cp38-linux_mipsel.whl'
    zip -qr -X '$WORKDIR/pillow-10.4.0-cp38-cp38-linux_mipsel.whl' .
"

# ----------------------------------------------------------------------------
# Step 6: verify the ABI (not just "it built without error") - same rigor as
# scripts/build-nginx-mipsel.sh.
# ----------------------------------------------------------------------------
echo "=== Verifying ABI ==="
docker run --rm -v /tmp:/tmp -w "$WORKDIR/extracted" "$K1_BASH_BUILD_IMAGE" sh -c "
    set -e
    FAIL=0
    for f in PIL/*.so; do
        $TOOL/bin/mips-linux-gnu-readelf -h \"\$f\" | grep -q 'Machine:.*MIPS' || { echo \"Not MIPS: \$f\"; FAIL=1; }
        $TOOL/bin/mips-linux-gnu-readelf -h \"\$f\" | grep -q 'nan2008' || { echo \"Not NaN2008: \$f\"; FAIL=1; }
        $TOOL/bin/mips-linux-gnu-readelf -A \"\$f\" | grep -q 'Hard float (32-bit CPU, 64-bit FPU)' || { echo \"Not hard-float FP64: \$f\"; FAIL=1; }
        $TOOL/bin/mips-linux-gnu-readelf -d \"\$f\" | grep -q 'libjpeg' && { echo \"UNEXPECTED libjpeg runtime dependency (should be static): \$f\"; FAIL=1; }
    done
    $TOOL/bin/mips-linux-gnu-readelf -d PIL/_imaging.cpython-38-mipsel-linux-gnu.so | grep -q 'libz.so.1' || { echo 'Missing expected libz.so.1 dependency'; FAIL=1; }
    [ \"\$FAIL\" -eq 0 ] || { echo 'ABI verification FAILED'; exit 1; }
    echo 'ABI verified: MIPS32, glibc dynamic link, hard-float FP64, NaN2008, jpeg static, zlib dynamic-against-system.'
"

cp /tmp/openke-pillow-build-*/pillow-10.4.0-cp38-cp38-linux_mipsel.whl "$OUT_DIR/" 2>/dev/null || \
    docker run --rm -v /tmp:/tmp alpine cat "$WORKDIR/pillow-10.4.0-cp38-cp38-linux_mipsel.whl" > "$OUT_DIR/pillow-10.4.0-cp38-cp38-linux_mipsel.whl"

echo "=== Done ==="
echo "Wrote $OUT_DIR/pillow-10.4.0-cp38-cp38-linux_mipsel.whl"
sha256sum "$OUT_DIR/pillow-10.4.0-cp38-cp38-linux_mipsel.whl"
echo
echo "Next: drop this into moonraker/moonraker/scripts/python_wheels/ inside"
echo "scripts/vendor/moonraker.tar.gz (see finalize-vendor.sh-style script), then test"
echo "'pip install' + 'import PIL' + opening a real image on-device before committing."
