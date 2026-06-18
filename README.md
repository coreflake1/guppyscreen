# OpenKE — perfect prints on the Ender-3 V3 KE

**OpenKE turns a stock Creality Ender-3 V3 KE into a properly dialed-in Klipper printer** — a fast
touchscreen UI, the print-quality mods that actually matter, and plain-English guides, all set up by one
installer.

<p align="center">
  <a href="https://github.com/coreflake1/guppyscreen/releases"><img alt="Release" src="https://img.shields.io/github/v/release/coreflake1/guppyscreen?style=flat-square&include_prereleases"></a>
  <a href="https://github.com/coreflake1/guppyscreen/actions"><img alt="Build" src="https://img.shields.io/github/actions/workflow/status/coreflake1/guppyscreen/build.yml?style=flat-square"></a>
  <a href="./LICENSE"><img alt="License" src="https://img.shields.io/github/license/coreflake1/guppyscreen?style=flat-square"></a>
</p>

It bundles three things people usually hunt down separately:

- 🖥️ **A fast touch UI** — replaces the stock screen with full print control, an interactive 3D bed
  mesh, and an on-screen calibration suite. Runs right on the printer's display (no X11, Wayland, or
  display server) on top of [Klipper](https://www.klipper3d.org/) and
  [Moonraker](https://github.com/Arksine/moonraker). The UI is a KE-focused fork of
  [GuppyScreen](https://github.com/ballaswag/guppyscreen).
- 🔧 **The Klipper mods that actually improve prints** — adaptive meshing + purge (KAMP), Axis Twist
  Compensation, TMC Autotune, skew correction, and more — vendored in and set up by the installer, not
  scattered across a dozen repos.
- 📚 **Plain-English guides** — how to *dial the printer in*, not just which button does what.

## Features

- 🖨️ **Print control & status** — temps, fans, LED, movement/homing, file browser (incl. USB sticks), Spoolman
- 🟦 **Interactive 3D bed mesh** — rotate / zoom / pan colour height map (plus a table view)
- 🎯 **On-screen calibration suite** — Axis Twist wizard, Skew Correction, TMC Autotune, live Z-offset baby-stepping, input-shaper & belt graphs
- 🎚️ **Fine-tune mid-print** — speed, flow, Z-offset, pressure advance, firmware retraction
- 📷 **Camera** — persistent image tuning, plus an optional low-bandwidth hardware **H.264** stream
- 🔔 **Buzzer beeps & songs** — real-pitch `M300`, `PLAY_TUNE` jingles (editable `songs.conf`), soft touchscreen click
- 🔌 **Power-loss recovery**, **WiFi low-latency** toggle, on-screen notifications
- 🔒 **Print-state safety locks** — anything that could ruin a running job is blocked or asks first
- 📐 Tuned **480×272** layout, with the screen mounted the right way up

> Full screen tour: **[Using OpenKE](wiki/Using-GuppyKE.md)** · complete change history: **[Releases](https://github.com/coreflake1/guppyscreen/releases)**

## Install

> ⚠️ **Back up your printer config first.** The installer changes init scripts, `printer.cfg`, and some
> Klipper extras. It keeps backups in `/usr/data/guppyify-backup/`, but keep your own too.

SSH into your printer and run:

```sh
sh -c "$(wget --no-check-certificate -qO - https://raw.githubusercontent.com/coreflake1/guppyscreen/main/scripts/installer.sh)"
```

> Use `installer.sh` — **not** `installer-deb.sh` (that one is for aarch64/Debian and refuses to run on the KE).

**It also offers the print-quality extras** (install all / skip all / choose each): KAMP, Axis Twist
Compensation, TMC Autotune, Skew Correction, the Creality Nebula camera (image tuning + H.264 stream), the
Pause/Resume layer-shift fix, and the Creality macros (M600, Save Z-Offset, useful macros, Exclude Object).
Already set some up by hand or via the **Creality Helper Script**? It detects and **skips** those — safe to
run on an existing setup, and it **never rewrites your saved calibrations**.

**Updating:** from the screen, **Settings → Update Guppy**. Coming from an older version (or "GuppyKE")?
See **[Upgrading](https://github.com/coreflake1/guppyscreen/wiki/Upgrading)**.

**Uninstall:** re-run the command above with `uninstall` appended. Details: **[Installation](https://github.com/coreflake1/guppyscreen/wiki/Installation)**.

## Compatibility

| | |
|---|---|
| **Printer** | Creality Ender-3 V3 KE |
| **SoC / arch** | Ingenic XBurst2 X2000 — **MIPS (mipsel)**, *not* aarch64 |
| **Display** | 480×272 |

Built and verified for the **Ender-3 V3 KE**. Other boards/screens can be built from source but aren't the focus.

**Mounting the screen:** the 3D-printable bracket I use to attach the display to the printer is on
Thingiverse — **[Ender-3 V3 KE screen mount](https://www.thingiverse.com/thing:6617266)**.

## Screenshots

> Captured in the desktop simulator at the KE's native 480×272.

| | |
|:---:|:---:|
| **Home** | **Live Z-Offset** |
| ![Home screen](docs/screenshots/home.png) | ![Z-Offset baby-stepping](docs/screenshots/z-offset.png) |
| **Firmware Retraction** | **Tune menu** |
| ![Firmware Retraction](docs/screenshots/firmware-retraction.png) | ![Tune menu](docs/screenshots/tune-menu.png) |
| **Fans** | **Extruder** |
| ![Fans](docs/screenshots/fans.png) | ![Extruder heating](docs/screenshots/extruder.png) |

## Documentation

Full documentation lives on the **[GitHub Wiki](https://github.com/coreflake1/guppyscreen/wiki)**. Highlights:

- [Perfect prints — start here](https://github.com/coreflake1/guppyscreen/wiki/Perfect-Prints) and the [calibration walkthrough](https://github.com/coreflake1/guppyscreen/wiki/Calibration-Explained)
- [Installation](https://github.com/coreflake1/guppyscreen/wiki/Installation) · [Upgrading from an older version](https://github.com/coreflake1/guppyscreen/wiki/Upgrading)
- [Perfect first layer (KAMP + Axis Twist)](https://github.com/coreflake1/guppyscreen/wiki/KAMP-and-Axis-Twist-Compensation) · [Skew Correction](https://github.com/coreflake1/guppyscreen/wiki/Skew-Correction) · [TMC Autotune](https://github.com/coreflake1/guppyscreen/wiki/TMC-Autotune)
- [Auto Z-offset: the load-sensor caveat](https://github.com/coreflake1/guppyscreen/wiki/Auto-Z-Offset)
- [Camera tuning](https://github.com/coreflake1/guppyscreen/wiki/Camera-Image-Tuning) · [H.264 stream](https://github.com/coreflake1/guppyscreen/wiki/Camera-H264-Stream) · [Layer-shift fix](https://github.com/coreflake1/guppyscreen/wiki/Pause-Park-Layer-Shift-Fix)
- [Troubleshooting](https://github.com/coreflake1/guppyscreen/wiki/Troubleshooting) · developer docs: [Building from Source](https://github.com/coreflake1/guppyscreen/wiki/Building-from-Source), [Architecture](https://github.com/coreflake1/guppyscreen/wiki/Architecture)

> The wiki pages are also maintained as Markdown in [`wiki/`](wiki/) in this repo and auto-published to the Wiki tab.

## Build from source

```bash
git clone --recurse-submodules https://github.com/coreflake1/guppyscreen.git
```

The desktop simulator (try the UI with no printer) and the MIPS cross-build for the KE are both covered in
**[Building from Source](https://github.com/coreflake1/guppyscreen/wiki/Building-from-Source)**. The cross-build runs in this repo's toolchain
container (`docker/Dockerfile`, published as `ghcr.io/coreflake1/guppydev`).

## License & credits

**GPL-3.0** — see [LICENSE](./LICENSE). The touch UI builds on
[ballaswag/guppyscreen](https://github.com/ballaswag/guppyscreen) and
[probielodan/guppyscreen](https://github.com/probielodan/guppyscreen), with the 3D bed mesh from
[prestonbrown/guppyscreen](https://github.com/prestonbrown/guppyscreen). Vendored Klipper mods keep their
own upstream licenses and credits — see [Contributing](https://github.com/coreflake1/guppyscreen/wiki/Contributing). *(Formerly "GuppyKE".)*
