# Contributing

Issues and pull requests are welcome.

## Workflow

1. Develop and test against the **[simulator](Development-and-Simulator)** first, then verify on-device
   where relevant.
2. Keep changes consistent with the existing panel structure and LVGL v8 idioms in `src/` (each screen
   is a `*_panel.cpp`; shared helpers live in `src/utils.cpp`).
3. Build the targets your change affects before opening a PR — at minimum the simulator, and the MIPS
   build for anything that touches on-device behavior or layout.
4. For small-screen work, build with `GUPPY_SMALL_SCREEN=1` and check the 480×272 layout.

Issue templates are provided under `.github/ISSUE_TEMPLATE/`. CI (`.github/workflows/build.yml`) runs
the full build matrix on PRs and pushes.

## License

This project is licensed under the **GNU General Public License v3.0** (see the `LICENSE` file at the
repository root). Contributions are accepted under the same license.

## Credits & attribution

This project builds on the work of:

- [ballaswag/guppyscreen](https://github.com/ballaswag/guppyscreen) — the original GuppyScreen
- [probielodan/guppyscreen](https://github.com/probielodan/guppyscreen) — KE-focused UI rework
- [pellcorp/grumpyscreen](https://github.com/pellcorp/grumpyscreen) — bug fixes and improvements
- [prestonbrown/guppyscreen](https://github.com/prestonbrown/guppyscreen) — 3D bed mesh feature
- [Moonraker](https://github.com/Arksine/moonraker), [Mainsail](https://github.com/mainsail-crew/mainsail),
  [Klippain-shaketune](https://github.com/Frix-x/klippain-shaketune)
- [nginx](https://nginx.org/) — vendored MIPS binary (1.29.3), serves the optional Mainsail web UI.
  Cross-compiled from real nginx source by `scripts/build-nginx-mipsel.sh`, not a copied prebuilt
  binary — see [`docs/VENDORING.md`](../docs/VENDORING.md) for how to rebuild it against a newer
  version.
- [Guilouz/Creality-Helper-Script](https://github.com/Guilouz/Creality-Helper-Script) — the
  `useful-macros.cfg`/`M600-support.cfg`/`save-zoffset.cfg` templates are modeled on theirs. Moonraker
  and Nginx were originally fetched from their repo at install time too, but are now vendored directly
  by OpenKE instead (a real clone of the official upstream Moonraker, and a from-source rebuild of
  nginx — no longer install-time-dependent on that repo staying online). As of this vendoring, the
  installer has no remaining runtime dependency on Guilouz's repo for anything.
- [kyleisah/Klipper-Adaptive-Meshing-Purging](https://github.com/kyleisah/Klipper-Adaptive-Meshing-Purging) —
  `Smart_Park.cfg`/`Line_Purge.cfg` are still adapted from KAMP (GPL-3.0); the adaptive-mesh macro itself
  is now original OpenKE code delegating to Klipper's own native adaptive meshing, no longer vendored KAMP
- [Klipper3d/klipper](https://github.com/Klipper3d/klipper) — Axis Twist Compensation and Screws Tilt
  Adjust modules, vendored verbatim from `klippy/extras/` (the KE's stock Klipper build predates/strips
  both; Skew Correction uses Klipper's native `[skew_correction]` directly, no vendored file needed)
- [evgarthub/klipper_tmc_autotune_k1](https://github.com/evgarthub/klipper_tmc_autotune_k1) (a K1/KE fork
  of [andrewmcgr/klipper_tmc_autotune](https://github.com/andrewmcgr/klipper_tmc_autotune)) — vendored
  TMC Autotune
- Icons: [Material Design Icons](https://pictogrammers.com/library/mdi/), [Z-Bolt](https://github.com/Z-Bolt/OctoScreen)

See [`docs/VENDORING.md`](../docs/VENDORING.md) for pinned commits/tags and file-level provenance of
each vendored mod.

Upstream documentation: <https://ballaswag.github.io/docs/guppyscreen/configuration/>
