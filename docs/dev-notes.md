# Developer Notes — ke-advanced-3d-bedmesh

Accumulated discoveries from building and packaging this branch for the Ender-3 V3 KE.

## Fork lineage

```
ballaswag/guppyscreen          (original)
    └── probielodan/guppyscreen (KE-focused UI rework)
            └── coreflake1/guppyscreen
                    └── ke-advanced-3d-bedmesh  ← this branch
```

3D bed mesh panel cherry-picked from prestonbrown/guppyscreen commit `bced7f7`.

---

## Target hardware — Ender-3 V3 KE

| Property | Value |
|---|---|
| SoC | Ingenic XBurst2 X2000 |
| Architecture | **MIPS (mipsel little-endian)** — NOT aarch64 |
| Kernel | 4.4.94 |
| Screen | 480×272 physical (framebuffer geometry 480×544 — double-buffer, two 272-line halves) |
| libc | /lib/ld-2.29.so (musl 2.29) |
| Init system | /etc/init.d/ BusyBox-style (S##name scripts) |
| systemctl | Present at /usr/bin/systemctl but NOT the primary init |
| Data root | /usr/data/ |
| Printer config | /usr/data/printer_data/config/printer.cfg |

`systemctl` is present but a shim — the real init is BusyBox `/etc/init.d/`.

---

## Binary

- **Build script**: `scripts/build-mips.sh` (handles all library rebuilds automatically)
- **Docker image**: `ballaswag/guppydev:latest`
- **Toolchain**: `/toolchains/mips32el--musl--stable-2024.02-1/bin/mipsel-linux-`
- **Flags**: `GUPPY_SMALL_SCREEN=1` required for 480×544
- **Output**: `build/bin/guppyscreen`
- **Verified**: `ELF 32-bit LSB executable, MIPS, MIPS32 version 1 (SYSV), statically linked` (~6.5 MB)
- **No dynamic deps** — `readelf -d` shows no NEEDED entries (musl static)

---

## Release

- **Tag**: `v0.1.0-ke-bedmesh`
- **Correct asset**: `guppyscreen-smallscreen.tar.gz` (MIPS)
- **Wrong asset**: `guppyscreen-arm.tar.gz` (aarch64, will not run on KE)
- **Package command**:
  ```sh
  GUPPYSCREEN_VERSION=0.1.0-ke-bedmesh GUPPY_THEME=blue bash scripts/release.sh guppyscreen-smallscreen
  ```

---

## Installer

- **Correct installer**: `scripts/installer.sh` (K1/MIPS/BusyBox path)
- **Wrong installer**: `scripts/installer-deb.sh` — immediately exits with
  `"Found arch mips / Terminating"` on the KE. Do not use on this printer.

**Install**:
```sh
sh -c "$(wget --no-check-certificate -qO - https://raw.githubusercontent.com/coreflake1/guppyscreen/ke-advanced-3d-bedmesh/scripts/installer.sh)"
```

**Uninstall**:
```sh
sh -c "$(wget --no-check-certificate -qO - https://raw.githubusercontent.com/coreflake1/guppyscreen/ke-advanced-3d-bedmesh/scripts/installer.sh)" uninstall
```

The installer always fetches from the branch URL (not from the tarball), so it is
always the latest version of the script. `PINNED_RELEASE="v0.1.0-ke-bedmesh"` in the
script pins which tarball it downloads.

---

## What installer.sh does (complete)

1. Checks arch == mips, screen < 800px → selects smallscreen asset
2. Checks `/lib/ld-2.29.so` present
3. Checks Moonraker reachable; queries klipper_path and config_file
4. Downloads tarball → extracts to `/usr/data/`
5. Substitutes `<GUPPY_DIR>`/`<PRINTER_DATA_DIR>` in `debian/guppyconfig.json` → `/usr/data/guppyscreen/guppyconfig.json`
6. Creates `/usr/data/guppyscreen/thumbnails/`
7. Smoke-tests the binary (runs 1s, checks `ps`)
8. Copies `gcode_shell_command.py` to Klipper extras (if not already present)
9. Creates `printer_data/config/GuppyScreen/scripts/`
10. Copies `scripts/*.cfg` and `*.py` to GuppyScreen config dir
11. **Backs up `printer.cfg`** → `/usr/data/guppyify-backup/printer.cfg.bak` (before modifying)
12. Adds `[include GuppyScreen/*.cfg]` to `printer.cfg`
    - If `[include gcode_macro.cfg]` exists: inserts after that line
    - Otherwise: **prepends** to the file (KE may not have that line)
13. Symlinks `/tmp/udisk` → gcodes dir `/usb`
14. Backs up (first run only, guarded by absence of `S50dropbear` in backup):
    - **Moves** `S12boot_display` → backup (disables boot display init)
    - Copies `S50dropbear` → backup
    - Copies `S99start_app` → backup (if file exists; KE may not have it)
15. Backs up `ft2font.so` (moves to backup)
16. **Overwrites** `/etc/init.d/S50dropbear` with guppy version
    - **Why**: The KE's stock `S50dropbear` has the `start` call inside `case start)` **commented
      out** (`#       start`). This means `S50dropbear start` is a no-op — SSH does **not**
      auto-start on a clean reboot. Guppy's version has the `start` uncommented, so SSH reliably
      starts on every boot after install. Currently SSH works on this printer because the
      helper-script started dropbear directly (not via the init script).
17. Interactive: disable all Creality services?
    - Y → removes `S99start_app` (guarded: only if exists)
    - N → renames `Monitor` and `display-server` to `.disable` (guarded: only if exist)
18. Installs `/etc/init.d/S99guppyscreen`
19. Backs up existing `calibrate_shaper_config.py` → backup dir; then overwrites it
20. Symlinks `guppy_module_loader.py`, `guppy_config_helper.py`, `tmcstatus.py` → Klipper extras
21. Replaces `ft2font.cpython-38-mipsel-linux-gnu.so` if matplotlib is installed
22. Symlinks `libeinfo.so.1`, `librc.so.1` → `/lib/` (needed by supervise-daemon)
23. Optionally restarts Klipper
24. Kills Creality processes if chosen
25. Starts `/etc/init.d/S99guppyscreen`; partial rollback if final ps check fails

---

## Backup directory: `/usr/data/guppyify-backup/`

| File | How saved | Notes |
|---|---|---|
| `printer.cfg.bak` | copied | Taken before include line is added |
| `S50dropbear` | copied | Original Creality SSH init |
| `S12boot_display` | **moved** | Only copy is in backup |
| `S99start_app` | copied | May be absent on KE — guarded |
| `ft2font.cpython-38-mipsel-linux-gnu.so` | **moved** | Only copy is in backup |
| `calibrate_shaper_config.py.bak` | copied | Taken before unconditional overwrite |

---

## What uninstall does NOT restore

- `gcode_shell_command.py` in Klipper extras (left in place)
- `/etc/init.d/S50dropbear` (guppy version stays; restore manually: `cp /usr/data/guppyify-backup/S50dropbear /etc/init.d/S50dropbear`)
- `Monitor` / `display-server` if renamed to `.disable` (restore manually: `mv /usr/bin/Monitor.disable /usr/bin/Monitor`)
- `libeinfo.so.1`, `librc.so.1` symlinks in `/lib/`
- Reboot required after uninstall to restore display services

---

## guppyconfig.json

- In tarball at `./guppyscreen/debian/guppyconfig.json`
- Contains placeholders `<GUPPY_DIR>` and `<PRINTER_DATA_DIR>`
- installer.sh substitutes both before copying to `/usr/data/guppyscreen/guppyconfig.json`
- `guppy_init_script`: `"service guppyscreen"` (invokes the init script via service command)
- `log_path`: resolves to `/usr/data/printer_data/logs/guppyscreen.log`

---

## S99guppyscreen

- Hardcoded paths: `GUPPY_DIR=/usr/data/guppyscreen`, log at `/usr/data/printer_data/logs/guppyscreen.log`
- Uses `supervise-daemon` from `k1_mods/respawn/` for auto-restart on crash
- Requires `libeinfo.so.1` and `librc.so.1` symlinked in `/lib/`
- `RC_DIR=/run/openrc` created on start (needed by OpenRC supervise-daemon)

---

## Simulator build (Linux desktop)

```sh
sudo dnf install sdl2-compat-devel   # Fedora/Nobara
make libhv.a && make libspdlog.a && make wpaclient
make -j$(nproc) \
  CFLAGS="-O3 -g0 -MD -MP -I./ -I./lvgl/ -I/usr/include/SDL2 -D_GNU_SOURCE=1 -Wno-incompatible-pointer-types" \
  LDFLAGS="-lm -Llibhv/lib -Lspdlog/build -l:libhv.a -latomic -lpthread -Lwpa_supplicant/wpa_supplicant/ -l:libwpa_client.a -lstdc++fs -l:libspdlog.a"
```

- Binary needs `/usr/data/printer_data/logs/` to exist or it crashes on startup
  (`spdlog_ex: Failed opening file ... for writing`)
- Without a printer connected, websocket reconnects every 2s — this is normal
- Simulator shows 480×800 (not 480×544); smallscreen layout only applies at runtime
  when `GUPPY_SMALL_SCREEN=1` is compiled in
- Mouse wheel zoom works in simulator (SIMULATOR flag enables it with 50ms timer)

---

## Confirmed on-device state (verified via SSH 2026-05-25)

Hostname: `Ender3V3KE-4C14`

### printer.cfg
- `[include gcode_macro.cfg]` **IS present** (line 8) — installer sed path will work; prepend fallback is unused but kept as insurance
- Config dir confirmed: `/usr/data/printer_data/config/`
- Klipper path confirmed: `/usr/share/klipper`
- Has `[bed_mesh]`, `[input_shaper]`, `[bltouch]`, `[prtouch_v2]`, `[adxl345]`
- Saved 5×5 bed mesh already present in `SAVE_CONFIG` block
- Saved input shaper: X=ei/55.4Hz, Y=ei/37.6Hz

### Init scripts — all confirmed present
- `S12boot_display` — minimal: just runs `/usr/bin/boot_display display &`
- `S50dropbear` — **start case is COMMENTED OUT** (see S50dropbear section above)
- `S99start_app` — substantial script starting 8 Creality services (master-server, audio-server, wifi-server, app-server, display-server, upgrade-server, web-server, Monitor)

### Binaries — all confirmed present
- `/usr/bin/Monitor` — 45 KB
- `/usr/bin/display-server` — 6.5 MB
- `/usr/sbin/dropbear` — running as PID 2124 (started by helper-script, not by init.d)

### Klipper extras — already present
- `gcode_shell_command.py` — already installed (installer will skip, if-not-exists guard)
- `calibrate_shaper_config.py` — already installed (will be backed up then overwritten)
- No guppy modules yet (`guppy_module_loader`, `guppy_config_helper`, `tmcstatus`)

### Matplotlib
- **Version 2.2.3 installed** → `ft2font.so` replacement will fire
- `ft2font.cpython-38-mipsel-linux-gnu.so` dated **Apr 15 2025** — post-firmware date,
  meaning it was already replaced once (likely by helper-script). Our install will back
  it up and replace it again with the guppy MIPS version.

### Helper script
- `/usr/data/helper-script/` is present (KIAUH-equivalent for K1/KE)
- This is what enabled SSH (started dropbear directly; init.d start case is commented out)
- `/usr/data/helper-script-backup/` also present

---

## Display orientation — confirmed fix (2026-05-25)

The KE screen is physically mounted **upside-down (180° rotated)** inside the chassis.
The kernel display driver compensates by rotating the framebuffer 180°.
GuppyScreen/LVGL must therefore **pre-rotate 180°** so the kernel's rotation cancels out.

**Fix**: `display_rotate: 2` in `debian/guppyconfig.json` (committed `8bed38d`).

How it propagates:
- `display_rotate: 2` → `sw_rotate=1`, `rotated=LV_DISP_ROT_180`
- LVGL renders content upside-down into the framebuffer
- Kernel rotates 180° → net 0° on the physical screen

Debugging history:
- `display_rotate:0` → screen upside-down (LVGL and kernel both unrotated, but hardware is inverted)
- `display_rotate:1` → 90° rotated (wrong axis)
- `display_rotate:2` → correct orientation ✓
- White screen on restart (without full reboot) was a framebuffer state artefact — full reboot clears it

`lv_disp_get_physical_hor_res()` returns **480** (not 272) for ROT_180 because a
180° rotation does not swap width/height, unlike 90°/270°.

---

## Font and layout tuning (2026-05-25)

### LVGL font architecture — critical lesson

GuppyScreen is compiled with `GUPPY_SMALL_SCREEN=1`. Before the fix, all widget
text rendered at **16 px** (`lv_font_montserrat_16`), which is too large for a
272 px tall screen — labels wrapped, buttons truncated text, table cells were
unreadable.

**Root cause**: `LV_FONT_DEFAULT` in `lv_conf.h` was unconditionally
`&lv_font_montserrat_16` — no `GUPPY_SMALL_SCREEN` guard existed.

The `font` parameter passed to `lv_theme_default_init()` in `src/main.cpp` is
almost irrelevant: the LVGL v8 default theme only applies that font to the
checked-state marker of checkbox widgets (`cb_marker_checked`). It does **not**
set a default `text_font` on labels, buttons, tables, or any other widget. Those
all fall back to `LV_FONT_DEFAULT`.

**Fix** (`lv_conf.h`, line ~379):
```c
// Before:
#define LV_FONT_DEFAULT &lv_font_montserrat_16

// After:
#ifdef GUPPY_SMALL_SCREEN
    #define LV_FONT_DEFAULT &lv_font_montserrat_12
#else
    #define LV_FONT_DEFAULT &lv_font_montserrat_16
#endif
```

This is a 25% reduction (16 → 12 px) that propagates to every widget which does
not have an explicit `lv_obj_set_style_text_font()` call.

### Scale factors (reference)

```
width_scale  = hor_res  / 800.0 = 480/800 = 0.600
height_scale = ver_res  / 480.0 = 272/480 = 0.567
```

Widgets with explicit hardcoded fonts (NOT affected by `LV_FONT_DEFAULT`):
| File | Widget | Font |
|---|---|---|
| `console_panel.cpp` | keyboard, send button, log label | `montserrat_16` |
| `macro_item.cpp` | run (play) button | `montserrat_16` |
| `macros_panel.cpp` | keyboard | `montserrat_16` |
| `bedmesh_panel.cpp` | 3D mesh labels | `montserrat_12` / `montserrat_16` |

### Remaining layout issues (lower priority)

| Screen | Issue | File | Notes |
|---|---|---|---|
| Temperature chart | Y-axis tick labels overlap at 6 ticks on 272px height | `src/main_panel.cpp` L227 | Reduce `major_cnt` 6→3 in `lv_chart_set_axis_tick`; set `LV_PART_TICKS` font to `montserrat_8` |
| Limits sliders | 4 sliders in COLUMN layout overflow 272px; bottom slider(s) clipped | `src/limits_panel.cpp` | Switch `limit_cont` to 2×2 grid (2 FR cols × 2 FR rows) |
| ~~Bed mesh table~~ | ~~Cells unreadable~~ | `src/bedmesh_panel.cpp` | **Resolved** — table view is now the default; see GUI pass below |

---

## GUI & functional pass (2026-05-26)

A round of usability fixes driven by on-device review. Verified in the SDL simulator
(`build-sim/`, `GUPPY_SMALL_SCREEN=1`).

### Print-state safety locks
Panels are now gated by `KUtils::is_printing()` (true when `print_stats/state` is
`printing` or `paused`) so a touch mid-print can't ruin the job. Helpers live in
`src/utils.cpp`: `is_printing()`, `notify_locked()` (modal toast), and
`confirm_if_printing(msg, cb)` (Confirm/Cancel override; runs `cb` immediately when idle).

- **Block entry + toast** (nothing useful to see mid-print): Homing, Extrude
  (`main_panel.cpp`); Bed Mesh, Input Shaper, Belts/Shake, TMC Autotune
  (`printertune_panel.cpp`).
- **Confirm-with-override**: Console (per command, `console_panel.cpp`), Macros
  (per run, `macro_item.cpp`), Limits & Power (on entry, `printertune_panel.cpp`).
- **Left open** (designed for mid-print use): Fine Tune, Fan, LED, Print Status, file browsing.
- Note: a new-print start was *already* guarded in `print_panel.cpp` — it shows a
  "Printing in progress…" dialog instead of starting a second job. (`View Job` / `Queue Job`
  in that dialog are unimplemented stubs.)

### Layout / UX
- **LED ↔ Fine Tune swap**: Fine Tune moved to the home screen (used during prints);
  LED moved to the Tune tab. `LedPanel` is owned by `MainPanel` and now passed by
  reference into `PrinterTunePanel` (where `FineTunePanel&` used to go).
- **Bed Mesh redesign** (`bedmesh_panel.cpp`): **table view is now the default**. The
  3D view is an opt-in fullscreen mode — pressing "3D View" hides the profile list and
  the other controls; the canvas fills the left and a narrow right-side strip holds the
  zoom +/- and Back buttons. Back from 3D returns to table view; Back from table exits.
  `resize_canvas()` floor lowered for <800px screens (the old 300px min overflowed 272px).
- **Fine Tune readouts** (`finetune_panel.cpp`): Z/PA precision trimmed `{:.5}`→`{:.3f}`
  and readout icon zoom 150→100 so long values (e.g. `0.123 mm/s`) no longer collide
  with the icon.
- **Belt shake** (`belts_calibration_panel.cpp`): excite-frequency readout moved from
  35px below the slider (clipped by the cramped grid cell) to the top-right of the control.
- **System panel** (`sysinfo_panel.cpp`): Factory Reset moved to the top-right corner
  (away from Back / settings rows to avoid accidental presses); network/version text
  width-constrained so it can't slide under the button. Factory Reset already had a
  "Reset GuppyScreen to default settings?" confirm prompt.
- **Mini print-status popup** (`mini_print_status.cpp`): aligned `TOP_RIGHT` (over the
  now-locked Home/Fine-Tune buttons) instead of `TOP_LEFT` where it overlapped the temps.
- **MCU temp default** (`src/config.cpp`): shipped default sensor changed from
  `temperature_sensor chamber_temp`/"Chamber" to `temperature_sensor mcu_temp`/"MCU Temp".
  Affects fresh installs only — existing on-device `guppyconfig.json` must be edited to
  update a printer that already has a config.

---

## KE-specific gotchas (resolved vs remaining)

- ~~`[include gcode_macro.cfg]` may not be in the KE's printer.cfg~~ **CONFIRMED PRESENT**
- ~~`S99start_app` may not exist on KE firmware~~ **CONFIRMED PRESENT**
- ~~`/usr/bin/Monitor` and `/usr/bin/display-server` may not exist~~ **BOTH CONFIRMED PRESENT**
- ~~Hardware install is untested~~ **CONFIRMED WORKING** — GuppyScreen installed and running
- `installer-deb.sh` is also present in the release tarball (stale, old version
  without safety fixes) — it won't be run by the standard install flow but could
  confuse someone who tries to run it from the extracted `/usr/data/guppyscreen/` dir
- ft2font.so was already replaced once by helper-script (Apr 2025); our install will
  replace it again — the backup step ensures the current version is preserved
