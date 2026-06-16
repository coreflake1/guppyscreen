# Changelog

All notable changes to **OpenKE** (the Ender-3 V3 KE project, formerly *GuppyKE*) are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project aims to
follow [Semantic Versioning](https://semver.org/). Release tags use the `vMAJOR.MINOR.PATCH-OpenKE`
scheme (older tags use `-GuppyKE` / `-ke-*`). Each released entry corresponds to a GitHub release with a
`guppyscreen-smallscreen.tar.gz` asset.

## [Unreleased]

_Nothing yet._

## [1.0.0-OpenKE] — 2026-06-16

First **stable 1.0** release of OpenKE. The touch UI, the vendored print-quality mods, and a
self-contained one-shot installer now come together as a single package — and, from this release on,
every version is documented here. (Content-identical to the short-lived `v0.6.0-OpenKE` tag, which this
release supersedes.)

### Added
- **On-screen:** stoppable filament **Load** (stops cleanly instead of running forever); an **Invert Y**
  homing-jog toggle (bed-slinger feel); **auto-cooldown** of the hotend after a manual
  extrude/retract/load/unload; a **Spoolman "Use this filament?"** confirmation before a manual load; and
  a regrouped **Tune** tab + rebalanced **Settings** panel.
- **Guided on-screen input shaper** — calibrates X/Y **one axis at a time** with an
  accelerometer-placement confirmation before each axis (correct for the bed-slinger: toolhead for X, bed
  for Y), locks **Calibrate**/**Save** while a run is in progress, shows progress and recommendation
  toasts, presents the frequency-response **graph + a readable console side-by-side**, and has a watchdog
  so a hung run can't spin forever.
- **Hardware H.264 camera stream** (go2rtc) — a low-bandwidth WebRTC/RTSP feed for the Creality Nebula,
  alongside the stock MJPEG.
- **Vendored Klipper mods** under `k1/k1_mods/klipper_mods/` (ship in the release tarball): **KAMP**
  (adaptive mesh + purge, pre-edited for the KE), **Axis Twist Compensation** (Klipper v0.12.0 module +
  idempotent `probe.py` patcher), and **TMC Autotune** (evgarthub K1 fork). Each carries its upstream
  `LICENSE`/`NOTICE`; see [`docs/VENDORING.md`](docs/VENDORING.md). No more cloning other repos by hand.
- **Installer "optional features" prompt** with **install-all / skip-all / choose-each** — set up any
  combination of: KAMP, Axis Twist, TMC Autotune, Skew Correction, the **Creality Nebula camera**
  (persistent image tuning + H.264 stream, as one option), and the **Pause/Resume layer-shift fix**
  (`y_park` 222→220). Config installs via the `GuppyScreen/*.cfg` include mechanism; only the Axis-Twist
  `probe.py` patch and the layer-shift `gcode_macro.cfg` edit touch existing files, and both are backed
  up first and guarded (skipped if not applicable).
- **Self-contained Nebula camera persist macros** (`nebula_camera.cfg`) — re-applies image settings on
  every boot so they survive reboots, with or without the Helper Script's `CAM_*` macros.
- **Vendored Creality macros** (GPL-3.0, from the Creality Helper Script) as their own installer option:
  M600 filament change, **Save Z-Offset** (native z-offset persistence — no longer delegated to the
  Helper Script), useful macros (backup/restore, PID, bed-level, warmup), and Exclude Object
  (+ `enable_object_processing` in moonraker.conf). Each `.cfg` installs only if its sections don't
  already exist (section-conflict guard), so it never causes a duplicate-section crash.
- **Real, split wiki** — User vs Developer sections, plain "reddit-style" guides, including new
  **Perfect prints — start here** and **Calibration, explained** (recalibrate-vs-reset, hardware-change
  matrix) pages. Publishing mechanism documented in **Publishing this wiki**.
- This **CHANGELOG.md**.

### Changed
- **Rebrand GuppyKE → OpenKE** across the README and wiki (the touch UI is now framed as one part of a
  perfect-prints toolkit). Git repo name unchanged, so existing installs/updaters keep working.
- `guppy_cmd.cfg` now ships `[guppy_config_helper]`, so the on-screen TMC Autotune **Save** works without
  a manual edit.
- Mod/install wiki pages rewritten: the installer does the setup; manual steps kept as an *advanced*
  section on each page.

### Fixed
- **Filament-runout Cancel dialog** — now shows "Cancelling print…", disables its buttons, and stays up
  with feedback until the print actually stops (the runout sensor could otherwise re-fire and re-pop it).
- **Input shaper** — fixed a crash after each axis (a result toast read the wrong JSON key); the console
  shaper list now lists shapers in the **same order as the graph legend** (Klipper's `INPUT_SHAPERS`
  order) instead of alphabetically; ASCII-only toast text so it renders cleanly in the compact font; and
  a much longer calibration watchdog (the KE's numpy analysis is genuinely slow, ~3 min/axis).

### Safety
- A fresh **timestamped `printer.cfg` backup on every installer run**; `probe.py` backed up before the
  Axis-Twist patch. Re-running the installer does **not** rewrite saved calibration values.
- **Duplicate-section guards**: the installer detects an existing `[axis_twist_compensation]`,
  `[skew_correction]`, KAMP, M600, Save-Z-Offset, or Exclude-Object setup and leaves it alone instead of
  adding a conflicting copy. The check looks only at **actively-loaded** config files (`printer.cfg` + its
  `[include]` globs, nested) and only at **uncommented** section headers — so it correctly ignores the
  KE's commented-out stock sections (e.g. `#[filament_switch_sensor ...]`) and the `*.bak`/`printer-*.cfg`
  backups in the config dir. Verified against a live KE under its BusyBox.

## [0.5.5-GuppyKE] — 2026-06-11

### Changed
- WiFi power-save toggle **renamed "Low Latency"** and expanded into a full bundle — it now disables WiFi
  power-save, turns off background roam-scans, **and** stops Bluetooth (the BCM4343 shares one antenna for
  WiFi/BT, and the coexistence caused latency spikes).

### Added
- **TMC Autotune user guide** — explains the greyed-out **Save** button, documents all four tuning goals,
  and notes the Sensorless Threshold is hidden on the KE.

### Fixed
- WiFi "Low Latency" hint left-aligned and shrunk (montserrat 14→12) so it clears the Back button.
- Broken Skew documentation link; completed the README documentation index.

## [0.5.0-GuppyKE] — 2026-06-09

### Added
- **Skew Correction panel** in the Tune tab, plus a printable skew-calibration frame.
- **0.001 mm Z-offset baby-step** option.
- First end-user **wiki** pages + README refresh.

### Fixed
- **TMC Metrics crash on TMC2208** drivers (no CoolStep/StallGuard) — guarded; the CoolStep +/- adjusters
  are now hidden on drivers that don't have them.

### Changed
- Dropped the bundled skew-calibration g-code — slice your own square instead.

## [0.4.0-GuppyKE] — 2026-06-06

### Added
- **Guided Axis Twist Compensation calibration wizard** in the Tune tab.
- **WiFi power-save toggle** for lower latency — re-applied on every reconnect / network switch, and a
  boot-race fix so it survives a reboot.
- Documentation: KAMP + Axis Twist Compensation first-layer guide (wiki + README link).

### Fixed
- **"Now printing" overlay jitter** — steadied and made into a single tap target.

## [0.3.0-GuppyKE] — 2026-06-05

### Added
- **Power Settings** (renamed) with a **Power-Loss Recovery** section.

### Fixed
- **OTA in-app updater** now points at the coreflake1 fork, ignores `nightly`, and filters release assets
  correctly.
- **Bed-mesh table + 3D rendering** made robust for KAMP adaptive and dense meshes.

### Changed
- CI actions bumped to Node 24 majors; dropped the orphaned lv_drivers build-time patch.

## [0.2.0-GuppyKE] — 2026-06-05

First **GuppyKE**-branded release — the big on-screen feature wave.

### Added
- **Live Z-offset baby-stepping panel** (0.005 / 0.01 mm steps, guarded when not homed).
- **Firmware Retraction** live-tuning panel.
- **Console** redesigned as a drill-down command browser.
- **Macros** redesigned — favorites, collapsible rows, button navigation.
- **Notifications** split into toasts, modals, and a print-done screen; on-screen toasts for Klipper events.
- **Fans** — friendly names, read-only fans (heater_fan / output-pin fans), correct editable/read-only split.
- **Exclude Object** — tap-to-exclude bed map during a print.
- **Spoolman** filament confirmation at print start.
- Print-status overlay reworked — bigger, preview-led, with a "Paused" chip; Homing/Extrude allowed while
  paused; the mesh is viewable (not mutable) during a print.
- **Extruder** heats all filament actions to the selected temp, with clearer feedback.
- Mainsail-matched ETA (averages file / filament / slicer estimates).

### Changed
- **EMA-smoothed** resistive ns2009 touch input for smoother scrolling.
- Thread-safe `State::get_data`, tighter LVGL cadence, evdev tracking-id press fix.
- CI: `lv_drivers` now fetched from the coreflake1 fork (no build-time patch).

## [0.1.2-ke-fixes] — 2026-05-28

### Added
- **Print-status**: filename label, dynamic ETA, auto-dismiss when done.
- **Extruder**: non-blocking heat, auto-extrude on reaching target temp, busy spinner.
- `KUtils::notify_toast` transient feedback and a `gcode_script` response-callback overload (groundwork
  for later guided flows).

### Changed
- CI builds **only** the Ender-3 V3 KE release asset; added a self-hosted cross-toolchain Docker image.
- Busy spinners normalized to 80×80.
- User-focused README rewrite + in-repo wiki.
- Reverted the v0.1.1 "LED on home / Fine Tune in Tune tab" change.

## [0.1.1-ke-gui-fixes] — 2026-05-27

### Added
- **480×272 small-screen layout** + a global font pass for the KE.
- Bed-mesh panel redesign — table by default, 3D fullscreen.
- Panels gated behind print state (LED ↔ Fine Tune swap).

### Fixed
- **Display sleep on the Ingenic X2000 DSI** — disable `fbdev_blank` on sleep (the panel was otherwise
  unrecoverable) and invalidate the screen on wake (fixes the white screen after sleep).
- `display_rotate=2` default for the KE's upside-down screen mount.
- Reduced `LV_FONT_DEFAULT` to montserrat_12 on small screens.

### Changed
- **Installer**: uninstall mode, `printer.cfg` backup before modify, silent-include fix, and guards for
  files the KE doesn't ship.

## [0.1.0-ke-bedmesh] — 2026-05-24

Initial release of the **KE fork**, based on [ballaswag/guppyscreen](https://github.com/ballaswag/guppyscreen).

### Added
- **Advanced 3D bed-mesh visualization** with interactive controls.
- Chamber temp on the print screen, default extruder temps, and a WiFi "forget network" button.
- Installer / release scripts adapted for the coreflake1 fork; documented the full fork lineage.

---

> Releases before the KE fork — the `0.0.x-beta` tags and the original `v0.1.0` — belong to upstream
> [ballaswag/guppyscreen](https://github.com/ballaswag/guppyscreen) and are not tracked here.

[Unreleased]: https://github.com/coreflake1/guppyscreen/compare/v1.0.0-OpenKE...HEAD
[1.0.0-OpenKE]: https://github.com/coreflake1/guppyscreen/releases/tag/v1.0.0-OpenKE
[0.5.5-GuppyKE]: https://github.com/coreflake1/guppyscreen/releases/tag/v0.5.5-GuppyKE
[0.5.0-GuppyKE]: https://github.com/coreflake1/guppyscreen/releases/tag/v0.5.0-GuppyKE
[0.4.0-GuppyKE]: https://github.com/coreflake1/guppyscreen/releases/tag/v0.4.0-GuppyKE
[0.3.0-GuppyKE]: https://github.com/coreflake1/guppyscreen/releases/tag/v0.3.0-GuppyKE
[0.2.0-GuppyKE]: https://github.com/coreflake1/guppyscreen/releases/tag/v0.2.0-GuppyKE
[0.1.2-ke-fixes]: https://github.com/coreflake1/guppyscreen/releases/tag/v0.1.2-ke-fixes
[0.1.1-ke-gui-fixes]: https://github.com/coreflake1/guppyscreen/releases/tag/v0.1.1-ke-gui-fixes
[0.1.0-ke-bedmesh]: https://github.com/coreflake1/guppyscreen/releases/tag/v0.1.0-ke-bedmesh
