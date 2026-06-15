# Development and Simulator

The SDL simulator lets you iterate on the UI on a desktop, without a printer or the embedded hardware.
This is the recommended way to develop.

## Run the simulator

1. Build the simulator (see **[Building from Source](Building-from-Source)** → step 4a). Add
   `GUPPY_SMALL_SCREEN=1` to the build if you want the 480×272 layout.
2. Create / point a local `guppyconfig.json` at a reachable Moonraker instance, and set `log_path`
   and `thumbnail_path` to **existing** local directories.
3. Run:
   ```bash
   ./build/bin/guppyscreen
   ```

> The binary **crashes on startup if the directory in `log_path` does not exist**
> (`spdlog_ex: Failed opening file ... for writing`). Create it first.

## Notes & gotchas

- Without a printer connected, the websocket reconnects roughly every 2 s — this is expected.
- The simulator window is **480×800**; the small-screen layout only takes effect when compiled with
  `GUPPY_SMALL_SCREEN=1`.
- Mouse-wheel zoom works in the 3D bed mesh in the simulator (enabled by the `SIMULATOR` define).
- GuppyScreen typically expects to run as `root` on the printer because it talks directly to
  wpa_supplicant; in the simulator, WiFi features are not exercised.

## No printer? Use a virtual one

[virtual-klipper-printer](https://github.com/mainsail-crew/virtual-klipper-printer) runs Klipper +
Moonraker in Docker. After starting it, set `moonraker_host` to `127.0.0.1` and `moonraker_port` to
`7125` in your simulator `guppyconfig.json`.

## Iterating

After an initial full build, edit files under `src/` and re-run `make` (with the same flags) to
recompile only what changed. Each screen is a `*_panel.cpp`; shared helpers (print-state checks,
toasts, confirmation dialogs) live in `src/utils.cpp`.

## Further reading

- `DEVELOPMENT.md` — upstream development notes (toolchains, environment variables, virtual Klipper).
- `docs/dev-notes.md` — detailed, KE-specific discoveries from building, packaging, and on-device SSH
  inspection (hardware facts, installer behavior, display orientation, font/layout tuning). This is the
  most thorough internal reference.
