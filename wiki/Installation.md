# Installation (Ender-3 V3 KE)

> ⚠️ **Back up your printer config before installing.** The installer modifies init scripts,
> `printer.cfg`, and some Klipper extras. It saves backups to `/usr/data/guppyify-backup/`, but you
> should keep your own copy as well.

## Install

SSH into your printer and run:

```sh
sh -c "$(wget --no-check-certificate -qO - https://raw.githubusercontent.com/coreflake1/guppyscreen/main/scripts/installer.sh)"
```

> **Use `installer.sh`, not `installer-deb.sh`.** The `-deb` variant targets aarch64/systemd/Debian
> and exits immediately with `Found arch mips / Terminating` on the KE.

The installer:

1. Confirms the architecture is `mips` and the screen is &lt;800 px, then selects the small-screen asset.
2. Verifies `/lib/ld-2.29.so` is present and that Moonraker is reachable (`localhost:7125`).
3. Downloads the release tarball and extracts it to `/usr/data/guppyscreen/`.
4. Substitutes the `<GUPPY_DIR>` / `<PRINTER_DATA_DIR>` placeholders in `guppyconfig.json`.
5. Smoke-tests the binary, wires up Klipper extras, and installs the init script.
6. Starts GuppyScreen via `/etc/init.d/S99guppyscreen`.
7. **Offers the optional features** (see below).

## Disabling Creality services (a prompt during install)

Partway through, the installer asks:

```
=== Do you want to disable all Creality services (revertible) with GuppyScreen installation? ===
Disable all Creality Services? (y/n):
```

- **`y` (recommended)** — frees up CPU/RAM for the things that matter (Klipper, Moonraker, the screen) by
  not starting Creality's cloud/app stack. This is what most KE users want.
- **`n`** — keeps Creality's services, but the installer still moves `Monitor` and `display-server` aside
  (renamed to `.disable`) so OpenKE can own the screen. The trade-off with `y` is that the Creality
  Cloud / Creality Print app integration stops working.

It's reversible either way — backups go to `/usr/data/guppyify-backup/`, and the uninstaller (plus the
notes it prints) restore the originals.

## Optional features (pick what you want)

Near the end the installer asks:

```
=== OpenKE optional features ===
  [Y] install all     [n] skip all     [o] choose each one
Choice (Y/n/o):
```

- **`Y`** installs everything below.
- **`n`** skips it all (you can re-run the installer later to add them — it's safe to run again).
- **`o`** walks you through each one with its own `y/N` prompt, so you can take Axis Twist but skip KAMP,
  add the camera but not TMC, etc.

What's on offer:

| Feature | What it does | Guide |
|---|---|---|
| **KAMP** | adaptive bed mesh + purge | [guide](KAMP-and-Axis-Twist-Compensation) |
| **Axis Twist Compensation** | left/right first-layer fix | [guide](KAMP-and-Axis-Twist-Compensation) |
| **TMC Autotune** | quieter, cooler steppers | [guide](TMC-Autotune) |
| **Skew Correction** | square parts | [guide](Skew-Correction) |
| **Creality Nebula camera** | image tuning that **sticks across reboots** + a hardware **H.264** stream | [image](Camera-Image-Tuning) · [H.264](Camera-H264-Stream) |
| **Creality macros** | M600 filament change, **Save Z-Offset** (persists z-offset), useful macros (backup/PID/bed-level), Exclude Object | — |
| **Pause/Resume layer-shift fix** | stops the bed crashing into the rail on resume (`y_park` 222→220) | [guide](Pause-Park-Layer-Shift-Fix) |

> Some of these still need a **one-time calibration / slicer setup** afterward (the installer can't do the
> physical part) — each guide walks you through it. Axis Twist is the only one that edits a Klipper core
> file (`probe.py`); the installer backs it up first and the edit is reversible. The layer-shift fix
> edits `gcode_macro.cfg` (backed up first, and skipped automatically if your config doesn't have the
> stock `y_park = 222`).

> **About the Creality macros:** these used to come from the Creality Helper Script. **Save Z-Offset** and
> **M600** redefine sections a stock or Helper-Script config may already own (`[save_variables]`,
> `[filament_switch_sensor filament_sensor]`, `SET_GCODE_OFFSET`, `[idle_timeout]`). To avoid a
> duplicate-section crash, the installer adds each macro file **only if your config doesn't already define
> those sections** — otherwise it skips that file and leaves your working setup alone. So on a fresh
> printer you get the lot; on top of an existing Helper-Script setup, the conflicting ones are skipped (you
> already have them). Exclude Object also flips `enable_object_processing` on in `moonraker.conf` when safe.

> **Restarting after changes:** the installer restarts Klipper for you. On the KE, the *first* restart
> after a config change occasionally shuts down with a `serialqueue … NoneType` error — this is a harmless
> host-MCU reconnect race, **not** a config problem. Just **Restart Klipper** again (Mainsail → *Restart*,
> which is more reliable than a bare `FIRMWARE_RESTART`); a cold boot always clears it. It can take more
> than one restart. See [Troubleshooting](Troubleshooting).

**Coexists with the Creality Helper Script.** If you already set any of these up — by hand or via the
Helper Script — the installer **detects it and leaves your version alone**: it skips adding a config
section you already have (no duplicate-section crash), and it backs up any Klipper-extras module it
overwrites (e.g. an existing TMC Autotune) to `/usr/data/guppyify-backup/` first. Macros OpenKE *relies
on* but doesn't ship — `M600`/filament, Save-Z-Offset, useful macros, `[exclude_object]` — are left
entirely to the Helper Script; OpenKE just uses them if present and shows an empty/disabled panel if not.
So it's safe to run on top of an existing Helper-Script setup.

## What the installer changes

| Change | Reverted by uninstall? |
|---|---|
| Extracts release to `/usr/data/guppyscreen/` | Optional (prompts) |
| Installs `/etc/init.d/S99guppyscreen` | Yes |
| Replaces `S50dropbear` SSH init script¹ | No — original saved to backup |
| Disables boot display (`S12boot_display` moved to backup) | Yes (restored from backup) |
| Backs up + optionally removes `S99start_app` | Partially (restored from backup) |
| Renames `Monitor` + `display-server` to `.disable` (if chosen) | No — restore manually |
| Adds `[include GuppyScreen/*.cfg]` to `printer.cfg` | Yes (line removed) |
| Creates `printer_data/config/GuppyScreen/` | Yes |
| Overwrites `calibrate_shaper_config.py` in Klipper extras | No — original saved to backup |
| Symlinks `guppy_module_loader.py`, `guppy_config_helper.py`, `tmcstatus.py` | Yes |
| Replaces matplotlib `ft2font.so` | Partially (original moved to backup) |

¹ The KE's stock `S50dropbear` has its `start` call commented out, so SSH does not auto-start on a
clean reboot. The replacement fixes this — SSH will auto-start after install.

Backups are written to `/usr/data/guppyify-backup/` **before** any destructive change. A fresh,
timestamped copy of `printer.cfg` (`printer.cfg.YYYYMMDD-HHMMSS.bak`) is taken on **every** run.

### Will reinstalling lose my calibrations?

**No.** Your calibration *values* — Z-offset, bed mesh, input shaper, skew, axis-twist, TMC Autotune —
live in `printer.cfg`'s `SAVE_CONFIG` block (and Z-offset in `variables.cfg`). The installer only *adds*
includes and copies files; it never rewrites those saved values, so re-running it does **not**
un-calibrate your printer. The probe-patch backup (`probe.py.bak`) and the timestamped `printer.cfg`
backups give you a restore point regardless.

> The one thing that *does* wipe calibrations is a **Creality firmware update** (it can reset
> `printer.cfg` and erase Klipper-side files) — that's true of any Klipper mod, not OpenKE specifically.
> After a firmware update, re-run the installer and re-run the affected calibrations.

## Uninstall

```sh
sh -c "$(wget --no-check-certificate -qO - https://raw.githubusercontent.com/coreflake1/guppyscreen/main/scripts/installer.sh)" uninstall
```

Uninstall stops GuppyScreen, removes `/etc/init.d/S99guppyscreen`, restores `S12boot_display` and
`S99start_app` from backup, removes the `[include GuppyScreen/*.cfg]` line from `printer.cfg`, removes
the GuppyScreen config directory, and removes the Klipper symlinks.

**Not** automatically restored (originals live in `/usr/data/guppyify-backup/`):

- `gcode_shell_command.py` and `calibrate_shaper_config.py` in Klipper extras
- The `S50dropbear` replacement — restore with
  `cp /usr/data/guppyify-backup/S50dropbear /etc/init.d/S50dropbear`
- `Monitor` / `display-server` if they were renamed to `.disable` — restore with
  `mv /usr/bin/Monitor.disable /usr/bin/Monitor` (and likewise for `display-server`)
- `libeinfo.so.1` / `librc.so.1` symlinks in `/lib/`
- The print-quality mod modules in Klipper extras (`autotune_tmc.py`, `motor_constants.py`,
  `axis_twist_compensation.py`) and the `probe.py` Axis-Twist edit. These are left in place on purpose —
  removing them would break a `printer.cfg` that still has saved `[autotune_tmc]` /
  `[axis_twist_compensation]` sections. To fully revert, restore
  `cp /usr/data/guppyify-backup/probe.py.bak /usr/share/klipper/klippy/extras/probe.py`, delete those
  modules, and remove the matching sections from `printer.cfg`. The uninstaller prints these exact steps.

A reboot is required after uninstall to restore display services.

## On-device service

- Init script: `/etc/init.d/S99guppyscreen`
- Binary + data: `/usr/data/guppyscreen/`
- Log: `/usr/data/printer_data/logs/guppyscreen.log`
- Auto-restart on crash is handled by `supervise-daemon` (from `k1/k1_mods/respawn/`), which needs the
  `libeinfo.so.1` / `librc.so.1` symlinks created during install.

> ⚠️ **Caveat:** because the service is supervised, simply `kill`-ing the process will cause it to
> respawn. Use `/etc/init.d/S99guppyscreen stop` to stop it cleanly.

See **[Known Issues](Known-Issues)** for the current installer version caveats.
