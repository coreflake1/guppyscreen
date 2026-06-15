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
- [prestonbrown/guppyscreen](https://github.com/prestonbrown/guppyscreen) — 3D bed mesh feature
- [Moonraker](https://github.com/Arksine/moonraker), [KlipperScreen](https://github.com/KlipperScreen/KlipperScreen),
  [Fluidd](https://github.com/fluidd-core/fluidd), [Klippain-shaketune](https://github.com/Frix-x/klippain-shaketune)
- Icons: [Material Design Icons](https://pictogrammers.com/library/mdi/), [Z-Bolt](https://github.com/Z-Bolt/OctoScreen)

Upstream documentation: <https://ballaswag.github.io/docs/guppyscreen/configuration/>
