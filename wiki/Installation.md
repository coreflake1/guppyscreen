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

Backups are written to `/usr/data/guppyify-backup/` **before** any destructive change, including a copy
of `printer.cfg` taken before the `[include]` line is added.

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

A reboot is required after uninstall to restore display services.

## On-device service

- Init script: `/etc/init.d/S99guppyscreen`
- Binary + data: `/usr/data/guppyscreen/`
- Log: `/usr/data/printer_data/logs/guppyscreen.log`
- Auto-restart on crash is handled by `supervise-daemon` (from `k1/k1_mods/respawn/`), which needs the
  `libeinfo.so.1` / `librc.so.1` symlinks created during install.

> ⚠️ **Caveat:** because the service is supervised, simply `kill`-ing the process will cause it to
> respawn. Use `/etc/init.d/S99guppyscreen stop` to stop it cleanly.

See **[Known Issues](Known-Issues.md)** for the current installer version caveats.
