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
| Screen | 480×544 (smallscreen build) |
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
    - **Why**: Stock Creality S50dropbear has dependencies on display-server/app services.
      When those are removed, stock script can fail and leave the printer without SSH.
      Guppy version is plain dropbear with no dependencies — starts unconditionally.
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

## KE-specific gotchas

- `[include gcode_macro.cfg]` may not be in the KE's printer.cfg — installer now
  prepends the GuppyScreen include as a fallback instead of silently doing nothing
- `S99start_app` may not exist on KE firmware — all references guarded with `[ -f ]`
- `/usr/bin/Monitor` and `/usr/bin/display-server` may not exist — guarded
- Hardware install is untested as of the last session; the smoke-test during install
  will be the first real execution of the binary on the printer
- `installer-deb.sh` is also present in the release tarball (stale, old version
  without safety fixes) — it won't be run by the standard install flow but could
  confuse someone who tries to run it from the extracted `/usr/data/guppyscreen/` dir
