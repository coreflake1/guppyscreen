# GuppyScreen Wiki — Ender-3 V3 KE Edition

GuppyScreen is a standalone touch UI for [Klipper](https://www.klipper3d.org/) 3D printers, built on
[LVGL](https://lvgl.io/) and driven entirely through [Moonraker](https://github.com/Arksine/moonraker)'s
websocket API. It renders directly to the framebuffer and has **no dependency on X11, Wayland, or any
display server**, which makes it suitable for resource-constrained printer mainboards.

This repository is a **Creality Ender-3 V3 KE–focused fork** (**GuppyKE**, current release
`v0.5.5-GuppyKE` and beyond). On top of the 480×272 small-screen layout, interactive 3D bed mesh, and
print-state safety locks, it adds a full suite of on-screen calibration tools: an Axis Twist Compensation
wizard, Skew Correction, TMC Autotune, live Z-offset baby-stepping, firmware-retraction tuning, and
power-loss recovery.

## Wiki pages

**For users — how to use it**
- **[Installation](Installation.md)** — install/uninstall on the Ender-3 V3 KE
- **[Using GuppyKE](Using-GuppyKE.md)** — a plain-English tour of the screen and every tool
- **[Perfect First Layer — Axis Twist & KAMP](KAMP-and-Axis-Twist-Compensation.md)** — the left-to-right first-layer fix (survives firmware flashes)
- **[Skew Correction](Skew-Correction.md)** — make functional parts come out truly square, from the screen
- **[TMC Autotune](TMC-Autotune.md)** — quieter, cooler steppers; enabling the greyed-out button
- **[Layer shift after pause/resume](Pause-Park-Layer-Shift-Fix.md)** — the one-line `y_park` fix for the stock KE pause crash
- **[Camera image tuning](Camera-Image-Tuning.md)** — improve the Nebula image and make the settings persist across reboots
- **[Hardware H.264 camera stream](Camera-H264-Stream.md)** — add a low-bandwidth WebRTC/RTSP feed (the hardware H.264) alongside the stock MJPEG
- **[Troubleshooting](Troubleshooting.md)** — common problems and fixes

**For developers**
- **[Configuration](Configuration.md)** — `guppyconfig.json` fields and build-time options
- **[Building from Source](Building-from-Source.md)** — submodules, patches, simulator, MIPS/aarch64
- **[Development and Simulator](Development-and-Simulator.md)** — local dev workflow and the SDL simulator
- **[Architecture](Architecture.md)** — components, repository layout, how it fits together
- **[Releases and Deployment](Releases-and-Deployment.md)** — CI matrix, assets, packaging
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
- **Axis Twist Compensation wizard** — guided 5-point fix for left-to-right first-layer unevenness
- **Skew Correction** — square up functional parts: print a calibration square, measure, enter on-screen
- **TMC Autotune** — quieter, cooler, smoother steppers; pick motor + goal on-screen
- **Live Z-offset baby-stepping** — 0.001–0.05 mm steps, auto-saved, mid-print
- Input shaper (PSD graphs) and belt calibration / excitation
- Firmware-retraction tuning, power-loss recovery, machine limits
- Temperature control, fans, LED, movement/homing; fine tune (speed/flow/Z/PA) mid-print
- Macro/console shell and file browser (auto-reload, USB thumb-drive support)
- Spoolman integration, TMC metrics, WiFi **Low Latency** toggle (power-save / roam / Bluetooth off), multi-printer support
- Print-state safety locks — panels that could disrupt a running job are gated/confirmed mid-print
- Cross-platform release artifacts (MIPS / ARM / x86)

## License & credits

Licensed under **GPL-3.0** (see `LICENSE`). Built on
[ballaswag/guppyscreen](https://github.com/ballaswag/guppyscreen),
[probielodan/guppyscreen](https://github.com/probielodan/guppyscreen), and the 3D bed mesh from
[prestonbrown/guppyscreen](https://github.com/prestonbrown/guppyscreen). See [Contributing](Contributing.md)
for the full credits list.
