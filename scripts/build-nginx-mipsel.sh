#!/bin/sh
# Cross-compile a fresh nginx for the KE (mipsel, glibc, hard-float FP64, NaN2008) and
# repackage it into scripts/vendor/nginx.tar.gz, ready to commit.
#
# Uses pellcorp's k1-nginx build recipe (vendored in scripts/vendor/nginx-src/ - source
# tarballs + their build.sh, so this never depends on their GitHub repo staying up) run
# inside their pellcorp/k1-bash-build Docker image (pinned by digest below), which
# bundles the mips-gcc720-glibc229 cross toolchain + sysroot this needs.
#
# Requires Docker. Everything else (QEMU emulation setup) this script does itself and is
# idempotent - safe to just re-run.
#
# ============================================================================
# THE GOTCHA THIS SCRIPT EXISTS TO AVOID REDISCOVERING
# ============================================================================
# nginx's ./configure needs to actually EXECUTE small MIPS test binaries as part of its
# standard cross-compilation checks (inherent to how nginx's build system works - not
# skippable). That needs QEMU user-mode emulation via the kernel's binfmt_misc - see
# scripts/lib/ensure-mips-qemu.sh (shared by every build-*-mipsel.sh script) for the full
# gotcha writeup (NaN2008 FP encoding gap in the standard public tooling, and why the fix
# is a wrapper script, not the raw qemu binary).
# ============================================================================

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VENDOR_SRC="$SCRIPT_DIR/vendor/nginx-src"
OUT_TARBALL="$SCRIPT_DIR/vendor/nginx.tar.gz"
K1_BASH_BUILD_IMAGE="pellcorp/k1-bash-build@sha256:0b96d1d65175c5a2e3a83a64c3212d08dd774fef0900f991e0ebc570ba896c85"
WORKDIR="/tmp/openke-nginx-build-$$"

if ! command -v docker >/dev/null 2>&1; then
    echo "docker is required." >&2
    exit 1
fi

# ----------------------------------------------------------------------------
# Step 1: QEMU MIPS emulation, with the NaN2008 fix. Idempotent - checks first.
# ----------------------------------------------------------------------------
. "$SCRIPT_DIR/lib/ensure-mips-qemu.sh"
ensure_mips_qemu

# ----------------------------------------------------------------------------
# Step 2: run the actual build (vendored pellcorp/k1-nginx recipe, unmodified).
# ----------------------------------------------------------------------------
echo "=== Building nginx (this takes a few minutes) ==="
docker run --rm -v /tmp:/tmp alpine sh -c "
    rm -rf '$WORKDIR/src'
    mkdir -p '$WORKDIR/src'
"
cp "$VENDOR_SRC/nginx-1.29.3.tar.gz" "$VENDOR_SRC/pcre2-10.43.tar.gz" "$VENDOR_SRC/build.sh" /tmp/
docker run --rm -v /tmp:/tmp alpine sh -c "
    cp /tmp/nginx-1.29.3.tar.gz /tmp/pcre2-10.43.tar.gz /tmp/build.sh '$WORKDIR/src/'
    chmod +x '$WORKDIR/src/build.sh'
    chmod -R a+rwX '$WORKDIR/src'
"
rm -f /tmp/nginx-1.29.3.tar.gz /tmp/pcre2-10.43.tar.gz /tmp/build.sh

docker run --rm -v /tmp:/tmp -w "$WORKDIR/src" "$K1_BASH_BUILD_IMAGE" "$WORKDIR/src/build.sh"

# ----------------------------------------------------------------------------
# Step 3: verify the ABI (not just "it built without error") and repackage into
# the layout installer.sh expects (nginx/sbin/nginx, nginx/nginx/*, not
# pellcorp's own usr/data/nginx/... layout).
# ----------------------------------------------------------------------------
echo "=== Verifying ABI + repackaging ==="
docker run --rm -v /tmp:/tmp alpine sh -c "
    set -e
    apk add --no-cache binutils >/dev/null 2>&1
    rm -rf '$WORKDIR/staged' '$WORKDIR/repack'
    mkdir -p '$WORKDIR/staged' '$WORKDIR/repack/nginx/sbin' '$WORKDIR/repack/nginx/nginx' '$WORKDIR/repack/nginx/logrotate.d'
    tar xzf '$WORKDIR/src/build/nginx.tar.gz' -C '$WORKDIR/staged'

    BIN='$WORKDIR/staged/usr/data/nginx/sbin/nginx'
    [ -f \"\$BIN\" ] || { echo 'Build output missing expected binary path'; exit 1; }

    # ABI check: must match our device's actual runtime (glibc dynamic link,
    # MIPS32r2, hard-float FP64, NaN2008 - same as the production binary this
    # is meant to replace).
    readelf -h \"\$BIN\" | grep -q 'Machine:.*MIPS' || { echo 'Not a MIPS binary'; exit 1; }
    readelf -h \"\$BIN\" 2>&1 | grep -q 'nan2008' || { echo 'ABI check failed: not NaN2008'; exit 1; }
    readelf -A \"\$BIN\" 2>&1 | grep -q 'Hard float (32-bit CPU, 64-bit FPU)' || { echo 'ABI check failed: not hard-float FP64'; exit 1; }
    readelf -d \"\$BIN\" | grep -q 'libc.so.6' || { echo 'ABI check failed: not dynamically linked against glibc'; exit 1; }
    echo 'ABI verified: MIPS32, glibc dynamic link, hard-float FP64, NaN2008 - matches production nginx.'

    cp \"\$BIN\" '$WORKDIR/repack/nginx/sbin/nginx'
    chmod 755 '$WORKDIR/repack/nginx/sbin/nginx'
    # NOTE: pellcorp's own build.sh deletes these stock template files from ITS
    # OWN staging output at the very end (they use a different sites/ layout for
    # their own deployment) - so grab them from nginx's untouched source conf/
    # directory instead, not from the (post-cleanup) staged output. These are
    # nginx's own stock files, unrelated to any K1/Creality customization.
    CONF='$WORKDIR/src/build/nginx-1.29.3/conf'
    for f in koi-win koi-utf win-utf; do
        [ -f \"\$CONF/\$f\" ] && cp \"\$CONF/\$f\" '$WORKDIR/repack/nginx/nginx/'
    done
    for f in mime.types fastcgi_params fastcgi.conf scgi_params uwsgi_params; do
        [ -f \"\$CONF/\$f\" ] && cp \"\$CONF/\$f\" '$WORKDIR/repack/nginx/nginx/'
        [ -f \"\$CONF/\$f\" ] && cp \"\$CONF/\$f\" \"$WORKDIR/repack/nginx/nginx/\$f.default\"
    done
    # installer.sh overwrites this with its own template right after extraction,
    # but ship a real one anyway so the tarball is self-consistent on its own.
    cp \"\$CONF/nginx.conf\" '$WORKDIR/repack/nginx/nginx/nginx.conf' 2>/dev/null || true
    cp \"\$CONF/nginx.conf\" '$WORKDIR/repack/nginx/nginx/nginx.conf.default' 2>/dev/null || true
    chmod 644 '$WORKDIR/repack/nginx/nginx/'*

    cat > '$WORKDIR/repack/nginx/logrotate.d/nginx' <<'LOGROTATE_EOF'
/var/log/nginx/*log {
	missingok
	create 640 http log
	sharedscripts
	compress
	postrotate
		test ! -r /var/run/nginx.pid || kill -USR1 \`cat /var/run/nginx.pid\`
	endscript
}
LOGROTATE_EOF
    chmod 644 '$WORKDIR/repack/nginx/logrotate.d/nginx'

    tar -C '$WORKDIR/repack' -czf '$WORKDIR/nginx-repacked.tar.gz' nginx
"
cp /tmp/openke-nginx-build-*/nginx-repacked.tar.gz "$OUT_TARBALL" 2>/dev/null || \
    docker run --rm -v /tmp:/tmp alpine cat "$WORKDIR/nginx-repacked.tar.gz" > "$OUT_TARBALL"

echo "=== Done ==="
echo "Wrote $OUT_TARBALL"
sha256sum "$OUT_TARBALL"
echo
echo "Next: bump the sha256/version note wherever scripts/vendor/nginx.tar.gz is"
echo "referenced (installer.sh, docs/VENDORING.md), then test on-device before committing."
