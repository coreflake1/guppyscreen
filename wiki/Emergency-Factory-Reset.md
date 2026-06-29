# Emergency Factory Reset

OpenKE installs a `S58factoryreset` init service that gives you two ways to wipe the printer back to stock — even if the screen and SSH are unreachable.

> **⚠️ This is destructive.** A factory reset removes all mods, configuration, print files, and calibration data. WiFi credentials and the Creality cloud account are preserved. You will need to re-run the OpenKE installer afterwards.

---

## Method 1 — USB drive (no SSH required)

This is the emergency path: works even if the screen is black, SSH is dead, or Klipper won't start.

1. Format a USB drive as FAT32.
2. Create an **empty file** named exactly `emergency_factory_reset` in the root of the drive (no extension).
3. Safely eject the drive, then plug it into the printer while it is **powered off**.
4. Power the printer on.
5. At boot the service detects the file, renames it to `emergency_factory_reset.old` (so it won't loop), and runs the reset. The printer reboots into stock firmware.

> The flag file is renamed before the wipe starts, so even if something interrupts the reset mid-way the printer will not loop on reboot.

---

## Method 2 — SSH (when you still have shell access)

If you can SSH in but want a clean slate:

```sh
/etc/init.d/S58factoryreset reset
```

---

## What gets wiped

| Path | Outcome |
|---|---|
| `/overlay/upper/` | **Wiped** — all system-level modifications removed |
| `/usr/data/guppyscreen/` | **Wiped** — OpenKE binary, config, mods |
| `/usr/data/printer_data/` | **Wiped** — Klipper config, gcodes, timelapse |
| `/usr/data/printer_data/logs/` | **Kept** — logs survive for diagnosis |
| `/usr/data/wpa_supplicant.conf` | **Kept** — WiFi credentials survive |
| `/usr/data/creality/` | **Kept** — Creality cloud account & device identity |
| `/etc/init.d/S58factoryreset` | **Kept** — the reset service itself survives so USB resets work again after reinstalling |

---

## After the reset

The printer boots into stock Creality firmware. Re-run the OpenKE installer to get back up:

```sh
sh -c "$(wget --no-check-certificate -qO - https://raw.githubusercontent.com/coreflake1/guppyscreen/main/scripts/installer.sh)"
```

See [Installation](Installation) for the full guide.
