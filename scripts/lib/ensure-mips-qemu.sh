#!/bin/sh
# Shared by every scripts/build-*-mipsel.sh script: makes sure QEMU user-mode
# emulation for 32-bit mipsel is registered in the kernel's binfmt_misc, with the
# NaN2008 floating-point fix this toolchain needs. Idempotent - safe to source
# and call every time, only does real work the first time on a given machine
# (binfmt_misc registration is global kernel state, survives across separate
# `docker run` invocations, but is cleared on reboot).
#
# Usage: caller must set $K1_BASH_BUILD_IMAGE first (the pinned toolchain image
# digest - this script doesn't own that pin, each build script pins its own),
# then: . "$(dirname "$0")/lib/ensure-mips-qemu.sh" && ensure_mips_qemu
#
# ============================================================================
# THE GOTCHA THIS EXISTS TO AVOID REDISCOVERING
# ============================================================================
# Any cross-compile toolchain build (nginx's ./configure, CPython's ./configure,
# distutils/setuptools building C extensions) that needs to EXECUTE small MIPS
# test/helper binaries as part of a normal build needs QEMU user-mode emulation.
#
# The popular public registration images are NOT enough on their own:
#   - tonistiigi/binfmt doesn't support plain 32-bit MIPS at all (only mips64/mips64le).
#   - multiarch/qemu-user-static DOES register 32-bit mipsel, but its qemu's DEFAULT
#     MIPS CPU model doesn't support the NaN2008 floating-point encoding this
#     toolchain uses (-mnan=2008, matching what the KE's real production binaries
#     already use - confirmed via readelf, not an invented requirement). Running a
#     built MIPS test binary under it fails with:
#       "ELF binary's NaN mode not supported by CPU"
#     Passing `-cpu P5600` to qemu-mipsel-static fixes it (P5600 is a real MIPS32r5
#     core supporting both legacy and 2008 NaN encoding). binfmt_misc has no
#     mechanism to pass extra arguments to the registered interpreter, so the fix
#     is registering a tiny WRAPPER SCRIPT (not the raw qemu binary) that adds
#     -cpu P5600 automatically on every invocation.
#
# Reproduced identically on a completely clean machine/terminal (not just inside
# any sandboxed tooling) - a genuine gap in the standard tooling for this specific
# legacy MIPS ABI, not an environment quirk.
#
# IMPORTANT SECOND GOTCHA: every file created here that later needs to be visible
# INSIDE a docker container must be created BY a docker container using the same
# bind mount, never by this script's own shell via plain redirection - files
# written directly by a bare shell were found to NOT reliably appear inside
# containers bind-mounting the same host /tmp path (reproduced, not hypothetical).
# Reading a file a container wrote, from the host shell, is fine (only the
# reverse direction - host-write then container-read - is the broken one). This
# is why every write below goes through `docker run ... -v /tmp:/tmp`.
# ============================================================================

MIPS_QEMU_WRAPPER_DIR="/tmp/openke-mips-qemu"

ensure_mips_qemu() {
    if ! command -v docker >/dev/null 2>&1; then
        echo "docker is required." >&2
        return 1
    fi

    echo "=== Checking QEMU mipsel emulation (NaN2008-capable) ==="
    docker run --rm -v /tmp:/tmp "$K1_BASH_BUILD_IMAGE" sh -c "
        mkdir -p '$MIPS_QEMU_WRAPPER_DIR'
        echo 'int main(){return 42;}' > '$MIPS_QEMU_WRAPPER_DIR/probe.c'
        /opt/toolchains/mips-gcc720-glibc229/bin/mips-linux-gnu-gcc -static -o '$MIPS_QEMU_WRAPPER_DIR/probe' '$MIPS_QEMU_WRAPPER_DIR/probe.c'
    "

    RC=0
    docker run --rm -v /tmp:/tmp alpine "$MIPS_QEMU_WRAPPER_DIR/probe" >/dev/null 2>&1 || RC=$?
    if [ "$RC" -eq 42 ]; then
        echo "QEMU mipsel/NaN2008 emulation already correctly registered - skipping setup."
        return 0
    fi

    echo "Setting up QEMU mipsel emulation with the NaN2008 wrapper fix..."
    docker run --privileged --rm multiarch/qemu-user-static --reset -p yes >/dev/null

    docker run --rm --privileged -v /tmp:/tmp --entrypoint sh multiarch/qemu-user-static -c "
        cp /usr/bin/qemu-mipsel-static '$MIPS_QEMU_WRAPPER_DIR/qemu-mipsel-static'
        chmod +x '$MIPS_QEMU_WRAPPER_DIR/qemu-mipsel-static'
        printf '#!/bin/sh\nexec $MIPS_QEMU_WRAPPER_DIR/qemu-mipsel-static -cpu P5600 \"\\\$@\"\n' > '$MIPS_QEMU_WRAPPER_DIR/qemu-wrapper'
        chmod +x '$MIPS_QEMU_WRAPPER_DIR/qemu-wrapper'
    "

    # binfmt_misc registration is genuine global kernel state - register once,
    # visible to every subsequent container until reboot.
    docker run --rm --privileged -v /tmp:/tmp alpine sh -c "
        mount -t binfmt_misc none /proc/sys/fs/binfmt_misc 2>/dev/null || true
        echo -1 > /proc/sys/fs/binfmt_misc/qemu-mipsel 2>/dev/null || true
        printf ':qemu-mipsel:M::\x7f\x45\x4c\x46\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x08\x00:\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff:$MIPS_QEMU_WRAPPER_DIR/qemu-wrapper:F' > /proc/sys/fs/binfmt_misc/register
    "

    RC=0
    docker run --rm -v /tmp:/tmp alpine "$MIPS_QEMU_WRAPPER_DIR/probe" >/dev/null 2>&1 || RC=$?
    if [ "$RC" -ne 42 ]; then
        echo "QEMU mipsel/NaN2008 setup failed verification (expected exit 42, got $RC)." >&2
        return 1
    fi
    echo "QEMU mipsel/NaN2008 emulation registered and verified."
}
