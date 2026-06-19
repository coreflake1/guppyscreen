#!/bin/bash
# Cross-compile guppycam into a static mipsel binary for the Creality KE,
# inside the guppydev MIPS toolchain image (same as the other k1_mods).
# Output: ./guppycam  (ELF 32-bit MIPS, statically linked).
set -e
IMAGE=ballaswag/guppydev:latest
HERE="$(cd "$(dirname "$0")" && pwd)"

docker run --rm -v "$HERE":/work -w /work "$IMAGE" bash -lc '
set -e
TC=/toolchains/mips32el--musl--stable-2024.02-1
GCC=$TC/bin/mipsel-linux-gcc
"$GCC" -O2 -static -Wall -Wextra -o /work/guppycam /work/guppycam.c
"$TC"/bin/mipsel-linux-strip /work/guppycam
'
echo "=== built: $HERE/guppycam ==="
file "$HERE/guppycam" 2>/dev/null || true
ls -la "$HERE/guppycam"
