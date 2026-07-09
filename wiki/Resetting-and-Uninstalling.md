# Resetting & Uninstalling

OpenKE gives you two ways to go back to stock. They do very different things — pick the right one.

---

## Which one do you want?

| I want to… | Use |
|---|---|
| Remove OpenKE's screen and mods, but keep all my Klipper config and print files | **[Uninstall](#uninstall-openke)** |
| Wipe everything — configs, gcodes, OpenKE, all of it — and start from zero | **[Factory reset](#factory-reset)** |
| My screen is black / SSH is dead / nothing works | **[Factory reset via USB](#method-1--usb-drive-no-ssh-required)** (works even with no screen) |

---

## Uninstall OpenKE

The uninstaller removes OpenKE's screen binary, Klipper mods, and system changes. It **keeps** your
Klipper config (`printer.cfg`), print files, bed meshes, and calibration data — so you can reinstall
later without recalibrating.

### Run it

SSH into the printer and run the same installer with the `uninstall` argument:

```sh
sh -c "$(wget --no-check-certificate -qO - https://raw.githubusercontent.com/coreflake1/guppyscreen/main/scripts/installer.sh)" -- uninstall
```

### What it removes

- OpenKE binary and config (`/usr/data/guppyscreen/`)
- Klipper mod **config files**: adaptive print setup configs, the Axis Twist config, TMC Autotune config, Skew config.
  The underlying Python modules (`axis_twist_compensation.py`, `autotune_tmc.py`, etc.) are deliberately
  **left in place** in `klippy/extras` — removing them could break a `printer.cfg` section that still
  references them, since the uninstaller doesn't touch `printer.cfg` itself.
- The factory-reset init script (`S58factoryreset`)

### What it does NOT automatically restore

- **`probe.py`** — the uninstaller does not copy this back for you. If Axis Twist Compensation patched
  it, the original is saved at `/usr/data/guppyify-backup/probe.py.bak`; restore it yourself with
  `cp /usr/data/guppyify-backup/probe.py.bak /usr/share/klipper/klippy/extras/probe.py` if you need to.
- **`S50dropbear`** (the SSH startup script) — this is intentional, not an oversight. OpenKE's install
  replaces the stock version to fix a boot-time race that can prevent SSH from starting reliably.
  Reverting to the stock version on uninstall would bring that problem back right when you might most
  need SSH access (e.g. to reinstall). The fixed version is left in place permanently.

### What it keeps

- All your Klipper config (`/usr/data/printer_data/config/`)
- Print files and thumbnails
- WiFi credentials
- Creality cloud account

> If anything wasn't auto-restored, the installer left a backup at `/usr/data/guppyify-backup/` —
> restore files from there manually and reboot.

### After uninstalling

The stock Creality screen binary comes back on the next reboot. The Klipper interface (Mainsail/Fluidd)
still works as normal.

To reinstall OpenKE later, just run the [installer](Installation) again.

---

## Factory Reset

A factory reset wipes the printer to exactly how it came from the factory — no configs, no gcodes, no
mods. Use this only when you need a truly clean slate or the printer is in an unrecoverable state.

> ⚠️ **This is destructive and not reversible.** Your Klipper config, print files, calibration data, and
> all OpenKE mods are gone. WiFi credentials and the Creality cloud account are preserved.

### Method 1 — USB drive (no SSH required)

Works even when the screen is black, Klipper is dead, or SSH isn't responding.

1. Format a USB drive — **FAT32 is recommended** for the widest compatibility with the printer's USB
   auto-mount support (the reset script itself doesn't check the filesystem format, but unrecognized
   formats may not get mounted at all, so the trigger file would never be seen).
2. Create an **empty file** named exactly `emergency_factory_reset` in the root of the drive (no
   extension, no `.txt`).
3. Eject the drive, then plug it into the printer while it is **powered off**.
4. Power the printer on. The service detects the file, renames it to `.old` (so it won't repeat), wipes
   everything, and reboots into stock firmware.

### Method 2 — Screen (Settings panel)

![Reset Options dialog](images/reset-options.png)

Open **Settings → System → Reset Options → Factory Reset Printer** → red Confirm button.

### Method 3 — SSH

```sh
/etc/init.d/S58factoryreset reset
```

### What gets wiped

| Path | Outcome |
|---|---|
| `/overlay/upper/` | **Wiped** — all system-level modifications removed |
| `/usr/data/guppyscreen/` | **Wiped** — OpenKE binary and config |
| `/usr/data/printer_data/` | **Wiped** — Klipper config, gcodes, calibration |
| `/usr/data/printer_data/logs/` | **Mostly wiped** — only 3 specific files survive: `guppyscreen.log`, `grumpyscreen.log`, and `factoryreset.log`. Everything else in `logs/` is deleted. |
| `/usr/data/wpa_supplicant.conf` | **Kept** — WiFi credentials survive |
| `/usr/data/creality/` | **Partially kept** — only specific identity files survive (`system_config.json`, `user_data_not_deleted.json`, `user_agree_root`); most of the rest of this folder's contents are wiped |

### After the reset

The printer boots into stock Creality firmware. To get OpenKE back:

```sh
sh -c "$(wget --no-check-certificate -qO - https://raw.githubusercontent.com/coreflake1/guppyscreen/main/scripts/installer.sh)"
```

See [Installation](Installation) for the full guided walkthrough.
