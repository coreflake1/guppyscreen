# GuppyScreen — ke-advanced-3d-bedmesh branch

> **This is a custom branch** for the **Creality Ender-3 V3 KE**, adding an advanced interactive 3D bed mesh visualization.

## Fork lineage

```
ballaswag/guppyscreen          (original)
    └── probielodan/guppyscreen (KE-focused fork, adds features below)
            └── coreflake1/guppyscreen  ← this repo
                    └── ke-advanced-3d-bedmesh  ← this branch (adds 3D bed mesh)
```

### What probielodan added over ballaswag

- **Major UI rework** — prompt panel, sensor/slider containers, file panel, homing panel, print panel, and more, all reworked for reliability and the KE's display
- **Better print status** — improved information display during a print
- **Auto-reload file list** — file list refreshes automatically
- **Absolute positioning display** — shows absolute axis coordinates
- **Invert Z direction** option
- **WiFi disconnect and forget-network buttons**
- **Chamber temperature** shown on the print screen
- **Default extruder temperatures** configurable
- **Android support dropped** (the KE runs on a dedicated MIPS/ARM SoC, not Android)

### What this branch adds on top

- 3D feature cherry-picked from [prestonbrown/guppyscreen](https://github.com/prestonbrown/guppyscreen) (commit `bced7f7`)

## What's different on this branch

- Real-time 3D rendered mesh surface with colour-gradient height map
- Interactive rotation, zoom (Z-scale and FOV), and pan via touch or mouse
- Toggle between 2D and 3D views with a single button
- Multitouch gesture support

**Changed files vs base:**
- `src/bedmesh_panel.cpp` — full 3D rendering engine
- `src/bedmesh_panel.h` — new structs (`Point3D`, `Vertex3D`, `Quad3D`) and method declarations
- `src/button_container.cpp` — added `get_label()` and `set_text()` helpers
- `src/button_container.h` — corresponding declarations

## Building from source

### 1. Clone with submodules

```bash
git clone --recurse-submodules https://github.com/coreflake1/guppyscreen.git
cd guppyscreen
git checkout ke-advanced-3d-bedmesh
```

### 2. Build bundled libraries

```bash
make libhv.a
make libspdlog.a
make wpaclient
```

### 3a. Simulator build (Linux desktop, for testing)

Install SDL2 dev headers:
```bash
# Fedora / Nobara
sudo dnf install sdl2-compat-devel

# Debian / Ubuntu / Raspbian
sudo apt install libsdl2-dev
```

Build:
```bash
make -j$(nproc) \
  CFLAGS="-O3 -g0 -MD -MP -I./ -I./lvgl/ -I/usr/include/SDL2 -D_GNU_SOURCE=1 -Wno-incompatible-pointer-types" \
  LDFLAGS="-lm -Llibhv/lib -Lspdlog/build -l:libhv.a -latomic -lpthread -Lwpa_supplicant/wpa_supplicant/ -l:libwpa_client.a -lstdc++fs -l:libspdlog.a"
```

> The `-Wno-incompatible-pointer-types` flag is required on GCC 14+ due to a pre-existing upstream
> strictness issue in `lv_touch_calibration`. It is unrelated to this branch's changes.

Binary: `build/bin/guppyscreen`

### 3b. Cross-compile for Ender-3 V3 KE (aarch64 target hardware)

The KE target requires the `ballaswag/guppydev` Docker image, which contains the
official `aarch64-none-linux-gnu-` toolchain.

**One-time setup** — rebuild `libwpa_client.a` for aarch64 (only needed after a
clean clone or `make clean`):
```bash
docker run --rm -u $(id -u):$(id -g) \
  -v "$PWD:$PWD" -w "$PWD" \
  -e PATH="/toolchains/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/bin:/usr/bin:/bin" \
  ballaswag/guppydev:latest \
  sh -c "make -C wpa_supplicant/wpa_supplicant clean && \
         make -C wpa_supplicant/wpa_supplicant \
           CC=aarch64-none-linux-gnu-gcc -j\$(nproc) libwpa_client.a"
```

**Main build:**
```bash
docker run --rm -u $(id -u):$(id -g) \
  -v "$PWD:$PWD" -w "$PWD" \
  -e CROSS_COMPILE=aarch64-none-linux-gnu- \
  -e PATH="/toolchains/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/bin:/usr/bin:/bin" \
  ballaswag/guppydev:latest \
  sh -c "make -j\$(nproc)"
```

Binary: `build/bin/guppyscreen` — verified as `ELF 64-bit LSB executable, ARM aarch64, statically linked`.

**Package a release tarball** (creates `guppyscreen-arm.tar.gz`):
```bash
GUPPYSCREEN_VERSION=0.1.0-ke-bedmesh GUPPY_THEME=blue bash scripts/release.sh guppyscreen-arm
```

---

# Guppy Screen for Klipper

Guppy Screen is a touch UI for Klipper using APIs exposed by Moonraker. It builds on LVGL as a standalone executable, has no dependency on any display servers such as X/Wayland.
<p align="center">
    <a aria-label="Downloads" href="https://github.com/probielodan/guppyscreen/releases">
      <img src="https://img.shields.io/github/downloads/probielodan/guppyscreen/total?style=flat-square">
  </a>
    <a aria-label="Stars" href="https://github.com/probielodan/guppyscreen/stargazers">
      <img src="https://img.shields.io/github/stars/probielodan/guppyscreen?style=flat-square">
  </a>
    <a aria-label="Forks" href="https://github.com/probielodan/guppyscreen/network/members">
      <img src="https://img.shields.io/github/forks/probielodan/guppyscreen?style=flat-square">
  </a>
    <a aria-label="License" href="https://github.com/probielodan/guppyscreen/blob/develop/LICENSE">
      <img src="https://img.shields.io/github/license/probielodan/guppyscreen?style=flat-square">
  </a>
    <a aria-label="Last commit" href="https://github.com/probielodan/guppyscreen/commits/">
      <img src="https://img.shields.io/github/last-commit/probielodan/guppyscreen?style=flat-square">
  </a>
</p>

## Installation / Update

> **Build status**: aarch64 cross-compiled binary verified in this repository.
> Printer install is **untested on hardware** as of this writing. Back up your
> printer config before proceeding.

SSH into your Ender-3 V3 KE and run:

```sh
sh -c "$(wget --no-check-certificate -qO - https://raw.githubusercontent.com/coreflake1/guppyscreen/ke-advanced-3d-bedmesh/scripts/installer-deb.sh)"
```

> **Do not use probielodan's installer** — it downloads from probielodan's
> releases and does not include the 3D bed mesh feature.

The installer downloads release `v0.1.0-ke-bedmesh` (pinned tag — not `latest`).

**What the installer changes on your printer:**

| Change | Rolled back by uninstall? |
|---|---|
| Extracts `~/guppyscreen/` | Optional (prompts) |
| Installs + enables `guppyscreen.service` | Yes |
| Installs + enables `disable_blinking_cursor.service` | Yes |
| Disables `KlipperScreen.service` | Yes — re-enabled |
| Modifies `wpa_supplicant.service` (adds `GROUP=netdev`) | Yes — restored from backup |
| Adds `[include GuppyScreen/*.cfg]` to `printer.cfg` | Yes — line removed |
| Creates `~/printer_data/config/GuppyScreen/` | Yes — directory removed |
| Copies `gcode_shell_command.py` to Klipper extras | **No** |
| Copies `calibrate_shaper_config.py` to Klipper extras | **No** |

A timestamped backup is saved to `~/guppyscreen-backup-YYYYMMDD-HHMMSS/`
before any changes, containing `printer.cfg`, `wpa_supplicant.service`, and
the GuppyScreen config directory (if upgrading).

## Uninstall

```sh
sh -c "$(wget --no-check-certificate -qO - https://raw.githubusercontent.com/coreflake1/guppyscreen/ke-advanced-3d-bedmesh/scripts/installer-deb.sh)" uninstall
```

Uninstall removes services, restores `wpa_supplicant.service`, removes
`[include GuppyScreen/*.cfg]` from `printer.cfg`, removes the GuppyScreen
config directory, and re-enables KlipperScreen. The only things **not**
automatically removed are Klipper extras (`gcode_shell_command.py`,
`calibrate_shaper_config.py`) — remove those manually if needed.

## Features
:white_check_mark: Console/Macro Shell\
:white_check_mark: Bedmesh\
:white_check_mark: Input Shaper (PSD graphs)\
:white_check_mark: Belt Calibration/Excitate\
:white_check_mark: Print Status\
:white_check_mark: Spoolman Integration\
:white_check_mark: Extrude/Retract\
:white_check_mark: Temperature Control\
:white_check_mark: Fans/LED/Move Control\
:white_check_mark: Fine Tune (speed, flow, z-offset, Pressure Advance)\
:white_check_mark: Limits (Velocity, Acel, Square Corner Velocity, etc.)\
:white_check_mark: File Browser\
:white_check_mark: Supports multiple screen resolutions\
:white_check_mark: Cross platform releases (MIPS/ARM/x86)\
:white_check_mark: TMC Metrics\
:white_check_mark: Multi-Printer support

## Roadmap
:bangbang: Exclude Object\
:bangbang: Firmware Retraction

Open for feature requests.

## Documentation
You can find various Guppy Screen documents [here](https://ballaswag.github.io/docs/guppyscreen/configuration/).

## Screenshot
### Material Theme
![Material Theme Guppy Screen](https://github.com/probielodan/guppyscreen/blob/main/screenshots/material/material_screenshot.png)

Earlier development screenshots can be found [here](https://github.com/probielodan/guppyscreen/blob/main/screenshots)

## Video Demo
https://www.reddit.com/r/crealityk1/comments/17jp59g/new_touch_ui_for_the_k1/

## Credits
[Guppyscreen](https://github.com/ballaswag/guppyscreen/) |
[Material Design Icons](https://pictogrammers.com/library/mdi/) |
[Z-Bolt Icons](https://github.com/Z-Bolt/OctoScreen) |
[Moonraker](https://github.com/Arksine/moonraker) |
[KlipperScreen](https://github.com/KlipperScreen/KlipperScreen) |
[Fluidd](https://github.com/fluidd-core/fluidd) |
[Klippain-shaketune](https://github.com/Frix-x/klippain-shaketune)
