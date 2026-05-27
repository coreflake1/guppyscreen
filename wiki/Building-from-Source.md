# Building from Source

GuppyScreen uses a plain `Makefile`. There are three practical targets: the **SDL simulator** (x86
desktop), the **MIPS/mipsel** build (the Ender-3 V3 KE hardware), and an **aarch64** build (other
boards; produced by CI). Dependencies are git submodules plus a vendored `wpa_supplicant`.

## Prerequisites

- A C++17 toolchain (GCC/G++ 7.2+), `make`, and `cmake`
- Git (the project uses submodules)
- For the simulator: SDL2 development headers
- For the MIPS/aarch64 cross-builds: Docker (uses a prebuilt toolchain image — see below)

## 1. Clone with submodules

```bash
git clone --recurse-submodules https://github.com/coreflake1/guppyscreen.git
cd guppyscreen
```

Submodules: `lvgl` (LVGL **v8**), `lv_drivers`, `libhv`, `spdlog`. `wpa_supplicant` is vendored in-tree.

## 2. Apply submodule patches

```bash
(cd lv_drivers && git apply ../patches/0001-lv_driver_fb_ioctls.patch)
(cd spdlog     && git apply ../patches/0002-spdlog_fmt_initializer_list.patch)
(cd lvgl       && git apply ../patches/0003-lvgl-dpi-text-scale.patch)
```

| Patch | Purpose |
|---|---|
| `0001-lv_driver_fb_ioctls.patch` | Framebuffer ioctls the KE display needs |
| `0002-spdlog_fmt_initializer_list.patch` | spdlog/fmt compatibility fix |
| `0003-lvgl-dpi-text-scale.patch` | DPI text-scaling adjustment |

## 3. Build the bundled libraries

```bash
make wpaclient
make libhv.a
make libspdlog.a
```

## 4a. Simulator build (x86 desktop)

Install SDL2 headers, then build with the simulator flags (no `CROSS_COMPILE`):

```bash
# Fedora / Nobara:   sudo dnf install sdl2-compat-devel
# Debian / Ubuntu:   sudo apt install libsdl2-dev
# Arch:              sudo pacman -S sdl2

make -j"$(nproc)" \
  CFLAGS="-O3 -g0 -MD -MP -I./ -I./lvgl/ -I/usr/include/SDL2 -D_GNU_SOURCE=1 -Wno-incompatible-pointer-types" \
  LDFLAGS="-lm -Llibhv/lib -Lspdlog/build -l:libhv.a -latomic -lpthread -Lwpa_supplicant/wpa_supplicant/ -l:libwpa_client.a -lstdc++fs -l:libspdlog.a"
```

> `-Wno-incompatible-pointer-types` is required on GCC 14+ due to an upstream strictness issue in
> `lv_touch_calibration`; it is unrelated to this fork's changes.

To build the small-screen layout in the simulator, add `GUPPY_SMALL_SCREEN=1` to the `make` invocation.

Output: `build/bin/guppyscreen`. See **[Development and Simulator](Development-and-Simulator.md)** to run it.

## 4b. Cross-compile for the Ender-3 V3 KE (MIPS)

The MIPS toolchain ships in the prebuilt `ballaswag/guppydev:latest` Docker image (the same image CI
uses). Run the provided script **inside that container**; it rebuilds each dependency for MIPS and then
builds GuppyScreen with `GUPPY_SMALL_SCREEN=1`:

```bash
docker run --rm -it -v "$PWD":/work -w /work ballaswag/guppydev:latest \
  bash scripts/build-mips.sh
```

- `scripts/build-mips.sh` defaults `CROSS_COMPILE` to `mipsel-linux-` (matches the toolchain on the
  image's `PATH`). CI uses the fuller triple `mipsel-buildroot-linux-musl-`.
- The script backs up any existing native dependency archives, rebuilds `libhv`, `spdlog`, and
  `libwpa_client` for MIPS, then restores the native archives on exit.
- Output: `build/bin/guppyscreen`, verified as
  `ELF 32-bit LSB executable, MIPS, MIPS32 version 1 (SYSV), statically linked` (~6.5 MB, no `NEEDED`
  entries).

### Docker note

This repository does **not** contain a `Dockerfile` or `docker-compose.yml`. Docker is used only to
provide the cross-compilation *toolchain* via the external, prebuilt `ballaswag/guppydev:latest` image
— there is no project-defined container image to build.

## Build internals (reference)

- `make build` runs the full sequence: clean + rebuild `wpaclient`, `libhv.a`, `libspdlog.a`, then the app.
- `make clean` removes `build/`.
- For native (non-cross) builds the Makefile auto-adds `-DSIMULATOR` and links `-lSDL2`. Defining
  `CROSS_COMPILE` switches to a static cross build (`-static`).
- The `GUPPY_SMALL_SCREEN` define selects the `material_46` asset set and the small-screen layout paths.

See **[Releases and Deployment](Releases-and-Deployment.md)** for packaging the result into a release tarball.
