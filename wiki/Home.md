# GuppyScreen Wiki — Ender-3 V3 KE Edition

GuppyScreen is a standalone touch UI for [Klipper](https://www.klipper3d.org/) 3D printers, built on
[LVGL](https://lvgl.io/) and driven entirely through [Moonraker](https://github.com/Arksine/moonraker)'s
websocket API. It renders directly to the framebuffer and has **no dependency on X11, Wayland, or any
display server**, which makes it suitable for resource-constrained printer mainboards.

This repository is a **Creality Ender-3 V3 KE–focused fork**. The current release baseline is `main`
(tag `v0.1.1-ke-gui-fixes`): a 480×272 small-screen layout, an interactive 3D bed mesh, print-state
safety locks, wake/sleep display fixes, and a hardened on-printer installer.

## Wiki pages

- **[Installation](Installation.md)** — install/uninstall on the Ender-3 V3 KE
- **[Configuration](Configuration.md)** — `guppyconfig.json` fields and build-time options
- **[KAMP & Axis Twist Compensation](KAMP-and-Axis-Twist-Compensation.md)** — first-layer mods for the KE (reinstall guide; survives firmware flashes)
- **[Building from Source](Building-from-Source.md)** — submodules, patches, simulator, MIPS/aarch64
- **[Development and Simulator](Development-and-Simulator.md)** — local dev workflow and the SDL simulator
- **[Architecture](Architecture.md)** — components, repository layout, how it fits together
- **[Releases and Deployment](Releases-and-Deployment.md)** — CI matrix, assets, packaging
- **[Troubleshooting](Troubleshooting.md)** — common problems and fixes
- **[Known Issues](Known-Issues.md)** — limitations and stale-reference caveats
- **[Contributing](Contributing.md)** — how to contribute changes

## How this fork differs

Each layer of the fork chain is additive:

```
ballaswag/guppyscreen            original project
  └── probielodan/guppyscreen    KE-focused UI rework (Android support dropped)
        └── coreflake1/guppyscreen   ← this repo (main)
              • 3D bed mesh (cherry-picked from prestonbrown/guppyscreen @ bced7f7)
              • 480×272 small-screen layout + global font pass
              • bed mesh panel redesign (table default, 3D opt-in fullscreen)
              • print-state safety locks, panel reordering (Fine Tune ↔ LED)
              • wake/sleep display reliability fixes (Ingenic X2000 DSI)
              • display_rotate=2 default (KE screen mounted upside-down)
              • hardened installer (uninstall mode, config backups, missing-file guards)
```

**What probielodan added over ballaswag:** major UI rework (prompt/sensor/file/homing/print panels),
improved print status, auto-reloading file list, absolute positioning display, invert-Z option,
WiFi disconnect/forget buttons, chamber temperature display, configurable default extruder temperatures,
and dropping Android support.

## Compatibility

| Property      | Value |
|---------------|-------|
| Printer       | Creality Ender-3 V3 KE |
| SoC           | Ingenic XBurst2 X2000 |
| Architecture  | **MIPS (mipsel little-endian)** — *not* aarch64 |
| Kernel        | 4.4.94 |
| Display       | 480×272 (framebuffer geometry 480×544) |
| libc          | musl (statically linked binary, no runtime shared-lib deps) |
| Init system   | BusyBox `/etc/init.d/` (`S##name` scripts) |
| Data root     | `/usr/data/` |

The build system can also target aarch64 and x86 (simulator), but the install scripts and the
on-device fixes in this fork are written and verified for the KE.

## Features

- Print status & control (with chamber/MCU temperature on the print screen)
- Interactive **3D bed mesh** — colour-gradient height map with rotate/zoom/pan, plus a table view (default)
- Input shaper (PSD graphs) and belt calibration / excitation
- Temperature control, fans, LED, movement/homing
- Fine tune (speed, flow, Z-offset, pressure advance) — usable mid-print
- Machine limits (velocity, accel, square-corner velocity, …)
- Macro/console shell and file browser (auto-reload, USB thumb-drive support)
- Spoolman integration, TMC metrics, multi-printer support
- Print-state safety locks — panels that could disrupt a running job are gated/confirmed mid-print
- Cross-platform release artifacts (MIPS / ARM / x86)

## License & credits

Licensed under **GPL-3.0** (see `LICENSE`). Built on
[ballaswag/guppyscreen](https://github.com/ballaswag/guppyscreen),
[probielodan/guppyscreen](https://github.com/probielodan/guppyscreen), and the 3D bed mesh from
[prestonbrown/guppyscreen](https://github.com/prestonbrown/guppyscreen). See [Contributing](Contributing.md)
for the full credits list.
