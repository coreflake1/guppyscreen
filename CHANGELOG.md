# Changelog

All notable changes to **OpenKE** (the Ender-3 V3 KE project, formerly *GuppyKE*) are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project aims to
follow [Semantic Versioning](https://semver.org/). Release tags use the `vMAJOR.MINOR.PATCH-OpenKE`
scheme (older tags use `-GuppyKE`). Each released entry corresponds to a GitHub release with a
`guppyscreen-smallscreen.tar.gz` asset.

## [Unreleased] — v0.6.0-OpenKE (planned)

The "self-contained project + real docs" release: vendored mods, a smarter installer, and a rebrand.

### Added
- **Vendored Klipper mods** under `k1/k1_mods/klipper_mods/` (ship in the release tarball): **KAMP**
  (adaptive mesh + purge, pre-edited for the KE), **Axis Twist Compensation** (Klipper v0.12.0 module +
  idempotent `probe.py` patcher), and **TMC Autotune** (evgarthub K1 fork). Each carries its upstream
  `LICENSE`/`NOTICE`; see [`docs/VENDORING.md`](docs/VENDORING.md). No more cloning other repos by hand.
- **Installer print-quality-mods prompt** — one opt-in step sets up KAMP, Axis Twist, TMC Autotune, and
  Skew Correction (sections added via the `GuppyScreen/*.cfg` include mechanism; only the Axis-Twist
  `probe.py` patch touches Klipper core, and it's backed up first).
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

### Safety
- A fresh **timestamped `printer.cfg` backup on every installer run**; `probe.py` backed up before the
  Axis-Twist patch. Re-running the installer does **not** rewrite saved calibration values.
- **Duplicate-section guards**: the installer detects an existing `[axis_twist_compensation]`,
  `[skew_correction]`, or KAMP setup and leaves it alone instead of adding a conflicting copy.

## [0.5.5-GuppyKE] — 2025

WiFi **Low Latency** toggle expanded (power-save off, roam-scans off, Bluetooth stopped). See
`RELEASE_NOTES_v0.5.5-GuppyKE.md`.

## [0.5.0-GuppyKE] — 2025

TMC Autotune panel, Skew Correction panel, TMC2208 metrics crash fix, 0.001 mm Z-offset baby-step, the
first end-user wiki pages.

## [0.4.0-GuppyKE] — 2025

Axis Twist Compensation guided wizard, WiFi power-save toggle, print-overlay jitter fix.

## [0.3.0-GuppyKE] — 2025

KAMP bed-mesh rendering fixes, OTA `update.sh` fix, Power Settings + Power-Loss Recovery.

## [0.2.0-GuppyKE] — 2025

Notifications/toasts, console drill-down redesign, macros panel redesign, firmware-retraction guard,
Z-offset baby-stepping aid.

## [0.1.2-ke-fixes] · [0.1.1-ke-gui-fixes] · [0.1.0-ke-bedmesh] — 2025

Initial KE fork line: 480×272 small-screen layout, interactive 3D bed mesh, print-state safety locks,
hardened installer. See the `RELEASE_NOTES_*.md` files for details.

[Unreleased]: https://github.com/coreflake1/guppyscreen/compare/v0.5.5-GuppyKE...HEAD
[0.5.5-GuppyKE]: https://github.com/coreflake1/guppyscreen/releases/tag/v0.5.5-GuppyKE
[0.5.0-GuppyKE]: https://github.com/coreflake1/guppyscreen/releases/tag/v0.5.0-GuppyKE
[0.4.0-GuppyKE]: https://github.com/coreflake1/guppyscreen/releases/tag/v0.4.0-GuppyKE
[0.3.0-GuppyKE]: https://github.com/coreflake1/guppyscreen/releases/tag/v0.3.0-GuppyKE
[0.2.0-GuppyKE]: https://github.com/coreflake1/guppyscreen/releases/tag/v0.2.0-GuppyKE
