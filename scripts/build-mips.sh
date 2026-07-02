#!/bin/bash
# Cross-compile guppyscreen for MIPS (Ingenic X2000, K1/KE)
# Run inside ballaswag/guppydev:latest Docker container
set -e

CROSS_COMPILE="${CROSS_COMPILE:-mipsel-linux-}"
GUPPYSCREEN_VERSION="${GUPPYSCREEN_VERSION:-v1.3.2-OpenKE}"
GUPPY_THEME="${GUPPY_THEME:-blue}"
JOBS="$(nproc)"
WORKDIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$WORKDIR"

echo "=== MIPS build: CROSS_COMPILE=$CROSS_COMPILE ==="

# Backup aarch64 libraries
cp libhv/lib/libhv.a /tmp/libhv.a.bak
cp spdlog/build/libspdlog.a /tmp/libspdlog.a.bak
cp wpa_supplicant/wpa_supplicant/libwpa_client.a /tmp/libwpa_client.a.bak

restore_libs() {
    echo "=== Restoring original libraries ==="
    cp /tmp/libhv.a.bak libhv/lib/libhv.a
    cp /tmp/libspdlog.a.bak spdlog/build/libspdlog.a
    cp /tmp/libwpa_client.a.bak wpa_supplicant/wpa_supplicant/libwpa_client.a
}
trap restore_libs EXIT

# --- libhv ---
echo "=== Building libhv for MIPS ==="
mkdir -p libhv/build-mips
cd libhv/build-mips
cmake .. \
    -DCMAKE_C_COMPILER="${CROSS_COMPILE}gcc" \
    -DCMAKE_CXX_COMPILER="${CROSS_COMPILE}g++" \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=mipsel \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_STATIC_LIBS=ON \
    -DCMAKE_BUILD_TYPE=Release \
    -DWITH_SSL=OFF \
    2>&1 | tail -5
make -j"$JOBS" hv_static 2>&1 | tail -5
mkdir -p "$WORKDIR/libhv/lib"
cp lib/libhv_static.a "$WORKDIR/libhv/lib/libhv.a"
cd "$WORKDIR"

# --- spdlog ---
echo "=== Building spdlog for MIPS ==="
mkdir -p spdlog/build-mips
cd spdlog/build-mips
cmake .. \
    -DCMAKE_C_COMPILER="${CROSS_COMPILE}gcc" \
    -DCMAKE_CXX_COMPILER="${CROSS_COMPILE}g++" \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=mipsel \
    -DSPDLOG_BUILD_SHARED=OFF \
    -DCMAKE_BUILD_TYPE=Release \
    2>&1 | tail -5
make -j"$JOBS" 2>&1 | tail -5
cp libspdlog.a "$WORKDIR/spdlog/build/libspdlog.a"
cd "$WORKDIR"

# --- libwpa_client ---
echo "=== Building libwpa_client for MIPS ==="
cd wpa_supplicant/wpa_supplicant
WPA_CC="${CROSS_COMPILE}gcc"
WPA_CFLAGS="-O2 -D_GNU_SOURCE -DCONFIG_CTRL_IFACE -DCONFIG_CTRL_IFACE_UNIX -I../src -I../src/utils -I../src/common"
$WPA_CC $WPA_CFLAGS -c ../src/common/wpa_ctrl.c -o wpa_ctrl.o
$WPA_CC $WPA_CFLAGS -c ../src/utils/os_unix.c -o os_unix.o
$WPA_CC $WPA_CFLAGS -c ../src/utils/common.c -o common.o
$WPA_CC $WPA_CFLAGS -c ../src/utils/wpa_debug.c -o wpa_debug.o
${CROSS_COMPILE}ar crs libwpa_client.a wpa_ctrl.o os_unix.o common.o wpa_debug.o
cd "$WORKDIR"

# --- guppyscreen ---
echo "=== Building guppyscreen for MIPS (GUPPY_SMALL_SCREEN=1) ==="
make clean
make -j"$JOBS" \
    CROSS_COMPILE="$CROSS_COMPILE" \
    GUPPY_SMALL_SCREEN=1 \
    GUPPYSCREEN_VERSION="$GUPPYSCREEN_VERSION" \
    GUPPY_THEME="$GUPPY_THEME" \
    EVDEV_CALIBRATE=1

# --- guppybeep (hardware-PWM buzzer player) ---
echo "=== Building guppybeep (buzzer) for MIPS ==="
"${CROSS_COMPILE}gcc" -O2 -static -o build/bin/guppybeep k1/k1_mods/buzzer/guppybeep.c

echo "=== Build complete ==="
file build/bin/guppyscreen
file build/bin/guppybeep
