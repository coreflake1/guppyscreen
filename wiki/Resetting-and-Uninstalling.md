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
- Klipper mods: KAMP configs, Axis Twist module, TMC Autotune config, Skew config
- System patches: `probe.py` is restored from the backup the installer took
- Init scripts added by OpenKE (the factory reset service, any camera scripts)

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

1. Format a USB drive as FAT32.
2. Create an **empty file** named exactly `emergency_factory_reset` in the root of the drive (no
   extension, no `.txt`).
3. Eject the drive, then plug it into the printer while it is **powered off**.
4. Power the printer on. The service detects the file, renames it to `.old` (so it won't repeat), wipes
   everything, and reboots into stock firmware.

### Method 2 — Screen (Settings panel)

Open **Settings → System Info → Reset Options → Factory Reset Printer** → red Confirm button.

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
| `/usr/data/printer_data/logs/` | **Kept** — logs survive for diagnosis |
| `/usr/data/wpa_supplicant.conf` | **Kept** — WiFi credentials survive |
| `/usr/data/creality/` | **Kept** — Creality cloud account & device identity |

### After the reset

The printer boots into stock Creality firmware. To get OpenKE back:

```sh
sh -c "$(wget --no-check-certificate -qO - https://raw.githubusercontent.com/coreflake1/guppyscreen/main/scripts/installer.sh)"
```

See [Installation](Installation) for the full guided walkthrough.
