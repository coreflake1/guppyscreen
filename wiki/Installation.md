# Installation (Ender-3 V3 KE)

> ⚠️ **Back up your printer config before installing.** The installer modifies init scripts,
> `printer.cfg`, and some Klipper extras. It saves backups to `/usr/data/guppyify-backup/`, but you
> should keep your own copy as well.

> 🖨️ **Mounting the screen:** the 3D-printable bracket I use to attach the display to the printer is on
> Thingiverse — **[Ender-3 V3 KE screen mount](https://www.thingiverse.com/thing:6617266)**. Print it
> before you start if you don't already have a mount.
>
> 🖨️ **Alternative mount:** if you'd rather keep the screen closer to its original stock position,
> **@DylanUnofficial** built **[Nebula screen mount](https://www.printables.com/model/1770386-creality-ender-3-v3-ke-openke-nebula-screen-mount)**
> on Printables. Thanks Dylan!

## Install

SSH into your printer and run:

```sh
sh -c "$(wget --no-check-certificate -qO - https://raw.githubusercontent.com/coreflake1/guppyscreen/main/scripts/installer.sh)"
```

> **Use `installer.sh`, not `installer-deb.sh`.** The `-deb` variant targets aarch64/systemd/Debian
> and exits immediately with `Terminating... Your OS Platform has not been tested with Guppy Screen` on
> the KE.

The installer:

1. Confirms the architecture is `mips` and the screen is &lt;800 px, then selects the small-screen asset.
2. Verifies `/lib/ld-2.29.so` is present and that Moonraker is reachable (`localhost:7125`).
3. Downloads the release tarball and extracts it to `/usr/data/guppyscreen/`. If you still have the old
   H.264 camera add-on (go2rtc) installed from a much older version, it's removed automatically at this
   point to free up memory — this happens every time, no prompt.
4. Sets up `guppyconfig.json` (your app settings — rotation, sensor names, touch calibration, toggles,
   etc.). **If this is a fresh install**, it just writes the packaged defaults. **If you already have a
   settings file from a previous install**, it *merges* the packaged defaults underneath it instead of
   overwriting — anything you've already customized is kept exactly as it was. If one of a few
   specific settings (like `invert_z_direction` or the log level) differs from the recommended default,
   it prints a note in the install log so you know it noticed — it does **not** change your value.
5. Smoke-tests the binary, wires up Klipper extras, and installs the init script.
6. Starts GuppyScreen via `/etc/init.d/S99guppyscreen`.
7. **Offers the optional features** (see below).

## Disabling Creality services (a prompt during install)

Partway through, the installer asks:

```
=== Do you want to disable all Creality services (revertible) with GuppyScreen installation? ===
Disable all Creality Services? (y/n):
```

**This prompt only appears if Creality's services aren't already disabled.** If you've already
answered `y` to this on a previous install, the installer detects that and skips straight past it,
printing "Creality services are already disabled — leaving them that way" instead of asking again.

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
- **`o`** walks you through each one with its own `y/N` prompt, so you can take Axis Twist but skip
  adaptive mesh + purge, add the camera but not TMC, etc.

What's on offer:

| Feature | What it does | Guide |
|---|---|---|
| **Adaptive mesh + purge + park** | mesh only the print footprint, adaptive purge line, smart park | [guide](Adaptive-Print-Setup) |
| **Axis Twist Compensation** | left/right first-layer fix | [guide](Axis-Twist-Compensation) |
| **TMC Autotune** | quieter, cooler steppers | [guide](TMC-Autotune) |
| **Skew Correction** | square parts | [guide](Skew-Correction) |
| **Firmware Retraction** | enables `G10`/`G11` firmware-level retraction commands, if your slicer uses them | — |
| **Screws Tilt Adjust** | adds the `SCREWS_TILT_CALCULATE` bed-levelling helper | — |
| **Creality Nebula camera** | image tuning that **sticks across reboots** | [guide](Camera-Image-Tuning) |
| **Creality macros** | M600 filament change (shows a Resume/Ignore/Cancel prompt with Unload/Load/Purge More options — errors from any of its buttons now surface as an on-screen message instead of silently doing nothing), **Save Z-Offset** (persists z-offset), useful macros (backup/PID/bed-level), Exclude Object | — |
| **E-Steps Calibration** | guided mark-extrude-measure rotation-distance wizard | [guide](Calibration-Explained) |
| **Pause/Resume/Cancel layer-shift fix** | stops the bed crashing into the rail on resume or cancel (`y_park` 222→220 in both `PAUSE` and `CANCEL_PRINT`, which each hardcode their own copy) | [Troubleshooting](Troubleshooting) |

> Some of these still need a **one-time calibration / slicer setup** afterward (the installer can't do the
> physical part) — each guide walks you through it. Axis Twist is the only one that edits a Klipper core
> file (`probe.py`); the installer backs it up first and the edit is reversible. The layer-shift fix
> edits `gcode_macro.cfg` (backed up first, and skipped automatically for either PAUSE or CANCEL_PRINT
> if your config doesn't have that macro's own stock `Y222` value).

> **About the Creality macros:** these used to come from the Creality Helper Script. **Save Z-Offset**,
> **M600**, **useful macros**, and **E-Steps Calibration** redefine sections a stock or Helper-Script
> config may already own (`[save_variables]`, `[filament_switch_sensor filament_sensor]`,
> `SET_GCODE_OFFSET`, `[idle_timeout]`, the backup/restore/reload-camera macros, extruder
> `rotation_distance` helpers). To avoid a duplicate-section crash, the installer checks first — if none
> of those sections exist yet, you get OpenKE's own versions outright. If they're already defined
> elsewhere (e.g. an existing Helper-Script setup), it **offers to safely replace them**: it checks
> whether the existing sections are read *anywhere else in your active config* — not just the same
> file, since a real setup can easily have a macro's definition in one `[include]`d file and whatever
> reads or calls it in a completely different one — including a plain, unquoted gcode-command call from
> another macro's body, not just a `printer[...]` lookup. If any reference turns up, it's a sign of a
> genuinely working, self-contained setup, not a stray duplicate, and it leaves everything untouched;
> otherwise, with your confirmation, it backs up the old file, comments out just the conflicting sections,
> and installs OpenKE's version in their place. Say no to the prompt (or it detects a working setup) and
> your existing macros keep running exactly as they were. Exclude Object also flips
> `enable_object_processing` on in `moonraker.conf` when safe — including inserting it under an existing
> `[file_manager]` section, not just when that section is missing entirely.
>
> **Moonraker and Nginx are vendored by OpenKE directly** (a real clone of the official upstream
> Moonraker, and the same nginx binary the community K1/KE lineage has used for a while) — the installer
> no longer fetches either from the Creality Helper Script's repo at install time. Mainsail is also
> registered with Moonraker's `update_manager` automatically, so it shows up in Mainsail's own "Machine
> Update" panel instead of needing to be updated by hand.

> **Restarting after changes:** the installer restarts Klipper for you. On the KE, the *first* restart
> after a config change occasionally shuts down with a `serialqueue … NoneType` error — this is a harmless
> host-MCU reconnect race, **not** a config problem. Just **Restart Klipper** again (Mainsail → *Restart*,
> which is more reliable than a bare `FIRMWARE_RESTART`); a cold boot always clears it. It can take more
> than one restart. See [Troubleshooting](Troubleshooting).

**Coexists with the Creality Helper Script.** If you already set any of these up — by hand or via the
Helper Script — the installer **detects it** and, for M600/Save Z-Offset/useful macros/E-Steps
Calibration specifically, **offers to safely swap in OpenKE's own version** (see above) rather than just
leaving the old one in place forever; everything else it skips adding a config section you already have
(no duplicate-section crash), and it backs up any Klipper-extras module it overwrites (e.g. an existing
TMC Autotune) to `/usr/data/guppyify-backup/` first. The everyday macros — `M600`/filament, Save
Z-Offset, useful macros, E-Steps Calibration, `[exclude_object]` — are now **shipped by OpenKE** (the
**Creality macros** and **E-Steps Calibration** options), so a fresh printer gets them outright; on a
printer that already has them (e.g. an existing Helper-Script setup), M600, Save Z-Offset, useful
macros, and E-Steps Calibration offer the safe-replace prompt, Exclude Object is skipped. Either way
it's safe to run on top of
an existing setup — nothing is changed without either a clean "nothing conflicts" state or your explicit
yes at the prompt.

## What the installer changes

| Change | Reverted by uninstall? |
|---|---|
| Extracts release to `/usr/data/guppyscreen/` | Yes — the uninstaller deletes this whole directory (with a confirmation prompt) |
| Installs `/etc/init.d/S99guppyscreen` | Yes |
| Replaces `S50dropbear` SSH init script¹ | No — original saved to backup |
| Disables boot display (`S12boot_display` moved to backup) | Yes (restored from backup) |
| Backs up + optionally removes `S99start_app` | Partially (restored from backup) |
| Renames `Monitor` + `display-server` to `.disable` (if chosen) | Yes (renamed back automatically) |
| Adds `[include GuppyScreen/*.cfg]` to `printer.cfg` | Yes (line removed) |
| Creates `printer_data/config/GuppyScreen/` | Yes |
| Overwrites `calibrate_shaper_config.py` in Klipper extras | No — original saved to backup |
| Symlinks `guppy_module_loader.py`, `guppy_config_helper.py`, `tmcstatus.py` | Yes |
| Replaces matplotlib `ft2font.so` | Partially (original moved to backup) |

¹ The KE's stock `S50dropbear` has its `start` call commented out, so SSH does not auto-start on a
clean reboot. The replacement fixes this — SSH will auto-start after install. This one is
**permanent by design**: even uninstalling OpenKE does not put the stock version back, because the
stock version has a separate startup race with the Creality display service that can leave SSH
unreachable — exactly when you'd most want remote access. See
[Resetting & Uninstalling](Resetting-and-Uninstalling).

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

## Testing bleeding-edge builds (`ke-next` nightly)

> ⚠️ **Unstable, for testers only.** `ke-next` is the active development branch — it can contain
> half-finished or experimental work between releases. Don't run this on a printer you rely on for
> real prints unless you're comfortable recovering it yourself (see [Uninstall](#uninstall) below,
> or just re-run the normal install command to go back to the latest stable release).

Every push to `ke-next` builds automatically and publishes to a moving `nightly-ke-next` prerelease
tag on GitHub. To install the latest one, fetch `installer.sh` from `ke-next` itself (not `main`) and
pin the release tag:

```sh
PINNED_RELEASE=nightly-ke-next sh -c "$(wget --no-check-certificate -qO - https://raw.githubusercontent.com/coreflake1/guppyscreen/ke-next/scripts/installer.sh)"
```

Check which commit you're actually running from the on-screen Tune → System Info panel — the
version string is `nightly-<commit sha>`. Cross-reference that sha against
[the nightly-ke-next release page](https://github.com/coreflake1/guppyscreen/releases/tag/nightly-ke-next)
or `ke-next`'s commit history to see what changed.

To go back to the latest stable release, just run the normal [install command](#install) again
(no `PINNED_RELEASE`) — it always resolves the latest actual tagged release, never a prerelease.

## Uninstall

```sh
sh -c "$(wget --no-check-certificate -qO - https://raw.githubusercontent.com/coreflake1/guppyscreen/main/scripts/installer.sh)" uninstall
```

Uninstall stops GuppyScreen, removes `/etc/init.d/S99guppyscreen`, restores `S12boot_display` and
`S99start_app` from backup, **renames `Monitor` / `display-server` back** if they were disabled, removes
the `[include GuppyScreen/*.cfg]` line from `printer.cfg`, removes the GuppyScreen config directory, and
removes the Klipper symlinks. So Creality's display/app stack comes back automatically on the next reboot,
whichever install option you picked.

**Not** automatically restored (originals live in `/usr/data/guppyify-backup/`):

- `gcode_shell_command.py` and `calibrate_shaper_config.py` in Klipper extras
- The `S50dropbear` replacement — restore with
  `cp /usr/data/guppyify-backup/S50dropbear /etc/init.d/S50dropbear`
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

See **[Troubleshooting](Troubleshooting)** for current installer/upgrade caveats.
