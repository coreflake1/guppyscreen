# Architecture

## Components

- **UI layer** — LVGL **v8** widgets. Each screen is a `*_panel.cpp` in `src/` (e.g.
  `print_status_panel`, `bedmesh_panel`, `inputshaper_panel`, `finetune_panel`). `src/main.cpp`
  bootstraps the display/input drivers and theme; `src/guppyscreen.cpp` wires the app together.
- **Display / input** — `lv_drivers` provides the framebuffer + evdev backends on the device and SDL2
  in the simulator. A local patch (`0001-lv_driver_fb_ioctls.patch`) adds the framebuffer ioctls the
  KE needs.
- **Networking** — `libhv` provides the websocket client that talks to Moonraker.
  `src/notify_consumer.cpp` dispatches Moonraker notifications to the panels.
- **WiFi** — a vendored `wpa_supplicant` client (`libwpa_client.a`) handles scan / connect / forget.
- **Logging** — `spdlog`.
- **State / config** — `src/config.cpp` loads and persists `guppyconfig.json`; `src/utils.cpp` holds
  shared helpers including the print-state safety checks (`is_printing()`, `notify_locked()`,
  `confirm_if_printing()`).
- **Packaging** — built as a single **statically linked** executable (musl on MIPS), so there are no
  runtime shared-library dependencies on the printer.

## Print-state safety model

Panels are gated by print state (`print_stats/state` is `printing` or `paused`):

- **Blocked with a toast** mid-print (nothing useful to do): Homing, Extrude, Bed Mesh, Input Shaper,
  TMC Autotune.
- **Confirm-with-override** mid-print: Console (per command), Macros (per run), Limits, Power.
- **Always available** (designed for mid-print use): Fine Tune, Fan, LED, Print Status, file browsing.

## Repository structure

```
.
├── src/                 GuppyScreen application source (panels, config, networking, utils)
├── assets/              Compiled fonts and icon sets (material, material_46, zbolt, svg)
├── themes/              Colour theme JSON files
├── lvgl/                LVGL graphics library (submodule, v8)
├── lv_drivers/          Display/input drivers (submodule; patched)
├── libhv/               Network library (submodule)
├── spdlog/              Logging library (submodule; patched)
├── wpa_supplicant/      Vendored WiFi control client
├── patches/             Patches applied to submodules before building
├── scripts/             Build, release, install, and update scripts
│   ├── build-mips.sh        MIPS cross-build (run in the toolchain container)
│   ├── release.sh           Packages a release tarball
│   ├── installer.sh         On-printer installer/uninstaller (MIPS/BusyBox path)
│   ├── installer-deb.sh     aarch64/systemd/Debian installer (NOT for the KE)
│   └── update.sh            On-printer updater
├── k1/                  KE/K1 on-device files
│   ├── k1_mods/             Init scripts (S99guppyscreen, S50dropbear), Klipper modules, respawn/
│   └── scripts/            Calibration helpers and guppy_cmd.cfg
├── debian/              guppyconfig.json template, systemd unit, helper config
├── docs/dev-notes.md    Detailed on-device/build discovery notes
├── lv_conf.h            LVGL configuration (font/feature toggles)
├── lv_drv_conf.h        LVGL driver configuration
├── Makefile             Build entry point
└── .github/workflows/   CI build + release pipeline
```

## Data & service layout on device

| Path | Purpose |
|---|---|
| `/usr/data/guppyscreen/` | Binary, themes, config, thumbnails |
| `/usr/data/guppyscreen/guppyconfig.json` | Runtime config |
| `/usr/data/printer_data/logs/guppyscreen.log` | Log file |
| `/etc/init.d/S99guppyscreen` | Service init script (supervised by `supervise-daemon`) |
| `/usr/data/guppyify-backup/` | Backups taken by the installer before destructive changes |
