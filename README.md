# GuppyScreen — Ender-3 V3 KE Edition

A fast, standalone touch UI for your **Creality Ender-3 V3 KE**, powered by [Klipper](https://www.klipper3d.org/)
and [Moonraker](https://github.com/Arksine/moonraker). It runs directly on the printer's screen — no
X11, Wayland, or display server required — and replaces the stock interface with full print control,
calibration tools, and an interactive 3D bed mesh.

This is a KE-focused fork of [GuppyScreen](https://github.com/ballaswag/guppyscreen). Current release:
**`v0.1.1-ke-gui-fixes`**.

<p align="center">
  <a href="https://github.com/coreflake1/guppyscreen/releases"><img alt="Release" src="https://img.shields.io/github/v/release/coreflake1/guppyscreen?style=flat-square&include_prereleases"></a>
  <a href="https://github.com/coreflake1/guppyscreen/actions"><img alt="Build" src="https://img.shields.io/github/actions/workflow/status/coreflake1/guppyscreen/build.yml?style=flat-square"></a>
  <a href="./LICENSE"><img alt="License" src="https://img.shields.io/github/license/coreflake1/guppyscreen?style=flat-square"></a>
</p>

---

## Features

- 🖨️ **Print control & status** — temperatures, fans, LED, movement, homing
- 🟦 **Interactive 3D bed mesh** — colour height map you can rotate, zoom, and pan (plus a table view)
- 📈 **Input shaper & belt calibration** with PSD graphs
- 🎚️ **Fine tune mid-print** — speed, flow, Z-offset, pressure advance
- 📂 File browser (with USB thumb-drive support), macro/console shell, Spoolman, TMC metrics
- 🔒 **Print-state safety locks** — panels that could ruin a running job are blocked or confirmed mid-print
- 📐 Tuned **480×272 layout** with the screen mounted the right way up

## Compatibility

| | |
|---|---|
| **Printer** | Creality Ender-3 V3 KE |
| **SoC / arch** | Ingenic XBurst2 X2000 — **MIPS (mipsel)**, *not* aarch64 |
| **Display** | 480×272 |

> This fork is built and verified for the **Ender-3 V3 KE**. Other boards/screens can be built from
> source but are not the focus here.

## Install

> ⚠️ **Back up your printer config first.** The installer changes init scripts, `printer.cfg`, and some
> Klipper extras (it keeps backups in `/usr/data/guppyify-backup/`, but keep your own too).

SSH into your printer and run:

```sh
sh -c "$(wget --no-check-certificate -qO - https://raw.githubusercontent.com/coreflake1/guppyscreen/main/scripts/installer.sh)"
```

> Use `installer.sh` — **not** `installer-deb.sh` (that one is for aarch64/Debian and will refuse to
> run on the KE).

### Uninstall

```sh
sh -c "$(wget --no-check-certificate -qO - https://raw.githubusercontent.com/coreflake1/guppyscreen/main/scripts/installer.sh)" uninstall
```

Full details (exactly what changes, what is/isn't restored): **[Installation](wiki/Installation.md)**.

## Documentation

Everything beyond installing lives in the **[`wiki/`](wiki/Home.md)** docs:

- [Installation](wiki/Installation.md) — install/uninstall and what it changes
- [Configuration](wiki/Configuration.md) — `guppyconfig.json` and build options
- [Building from Source](wiki/Building-from-Source.md) — submodules, simulator, MIPS cross-build
- [Development & Simulator](wiki/Development-and-Simulator.md) — local dev workflow
- [Architecture](wiki/Architecture.md) — how it's put together
- [Troubleshooting](wiki/Troubleshooting.md) · [Known Issues](wiki/Known-Issues.md) · [Contributing](wiki/Contributing.md)

## Build it yourself (quick start)

```bash
git clone --recurse-submodules https://github.com/coreflake1/guppyscreen.git
cd guppyscreen
```

- **Desktop simulator** (try the UI without a printer) and **MIPS build for the KE** are both covered in
  [Building from Source](wiki/Building-from-Source.md).
- The MIPS cross-build runs in a toolchain container. This repo ships its own
  [`docker/Dockerfile`](docker/Dockerfile), published as `ghcr.io/coreflake1/guppydev` — see
  [Building from Source](wiki/Building-from-Source.md).

## License & credits

Licensed under **GPL-3.0** — see [LICENSE](./LICENSE).

Built on [ballaswag/guppyscreen](https://github.com/ballaswag/guppyscreen) and
[probielodan/guppyscreen](https://github.com/probielodan/guppyscreen), with the 3D bed mesh from
[prestonbrown/guppyscreen](https://github.com/prestonbrown/guppyscreen). Full credits in
[Contributing](wiki/Contributing.md).
