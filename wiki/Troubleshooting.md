# Troubleshooting

| Symptom | Cause / fix |
|---|---|
| Klipper shuts down after a restart with `serialqueue … NoneType` / "Unhandled exception during run" | Known KE host-MCU reconnect race — **not** a config problem. Just restart Klipper again; a full **Restart Klipper** (Mainsail → *Restart* → host, or the installer's service restart) is more reliable than a bare `FIRMWARE_RESTART`, and a cold boot always clears it. It can need **more than one** restart. |
| Screen upside-down or 90° wrong | Set `display_rotate: 2` in `guppyconfig.json` (KE default). `0` = upside-down, `1` = wrong axis. See [Configuration](Configuration). |
| Touch targets shifted after changing `display_rotate` | The saved calibration coefficients are tied to the old rotation. Reset: open `/usr/data/guppyscreen/guppyconfig.json`, remove `touch_calibration_coeff`, set `touch_calibrated` to `false`, restart GuppyScreen. See [Configuration](Configuration#display-rotation-ke-specific). |
| Installer prints `tar: can't remove old file ./guppyscreen/debian: Is a directory` | Harmless — tar is trying to replace a directory with a non-directory entry (or vice versa) during extraction. Nothing failed; installation continues normally. |
| Installer prints `Skipping Save Z-Offset`, `Skipping M600 filament-change support`, or `Skipping Exclude Object` | The installer detected that your `printer.cfg` already defines those sections (e.g. from a previous install or CrealityHelper). It skips adding duplicates. No action needed — your existing config is used as-is. |
| White screen after wake from sleep | Fixed on `main`; if a stuck framebuffer state remains, a full reboot clears it. |
| Simulator crashes on startup with `spdlog_ex: Failed opening file` | The directory in `log_path` doesn't exist — create it. |
| `Found arch mips / Terminating` during install | You ran `installer-deb.sh` on the KE; use `installer.sh` instead. |
| Labels wrap / buttons truncated on the small screen | Build with `GUPPY_SMALL_SCREEN=1` (sets the default font to `montserrat_10`). |
| Build fails on GCC 14+ in `lv_touch_calibration` | Add `-Wno-incompatible-pointer-types` to `CFLAGS` (see [Building from Source](Building-from-Source)). |
| `guppyscreen` keeps restarting after you `kill` it | It is supervised by `supervise-daemon`; stop it with `/etc/init.d/S99guppyscreen stop`. |
| Websocket reconnects every ~2 s in the simulator | Expected when no printer/Moonraker is connected. |
| Wrong sensor names after an update, e.g. a row labelled **Temperature** overlapping its value, or `Heater Bed` instead of `Bed` | Your `guppyconfig.json` has an empty or old `monitored_sensors`, so Guppy falls back to auto-naming each sensor from its raw Klipper id (and the long name overlaps the reading). This happens when you update **through the screen** (which only swaps the binary) instead of re-running the installer. See [Sensor labels are wrong / overlap the value](#sensor-labels-are-wrong--overlap-the-value) below. |
| aarch64 (`guppyscreen-arm.tar.gz`) won't run on the KE | The KE is MIPS; use the `guppyscreen-smallscreen.tar.gz` (MIPS) asset. |
| Mainsail/screen laggy or webcam stutters, signal is fine | Turn on **Low Latency** in the WiFi panel (WiFi power-save / idle-sleep / roam-scans off + Bluetooth stopped, since WiFi and BT share one 2.4 GHz radio). Also fix the basics off-device: avoid 2.4 GHz channel congestion and strong nearby emitters (e.g. a printer's Wi-Fi-Direct). See [Using OpenKE](Using-GuppyKE#settings--system). |

## Sensor labels are wrong / overlap the value

If the home screen shows a sensor labelled **Temperature** (overlapping its reading), or `Heater Bed` instead of `Bed`, your `guppyconfig.json` has no usable `monitored_sensors`. Guppy then auto-names every sensor from its raw Klipper object id — those names are long and run into the value.

This is almost always because the printer was updated **from the screen** (Settings → update), which replaces only the `guppyscreen` binary and leaves the existing `guppyconfig.json` untouched. The correct sensor defaults ship *with the installer*, not with the binary.

**Easiest fix — re-run the installer.** It rewrites `guppyconfig.json` to current defaults. SSH into the printer and run the same one-liner from [Installation](Installation):

```sh
sh -c "$(wget --no-check-certificate -qO - https://raw.githubusercontent.com/coreflake1/guppyscreen/main/scripts/installer.sh)"
```

> ⚠️ The installer overwrites `guppyconfig.json`. If you have customised it (extra sensors, macros, host/port), copy it aside first and merge your changes back afterwards.

**Surgical fix — edit the config by hand.** SSH into the printer, open `/usr/data/guppyscreen/guppyconfig.json`, and set `monitored_sensors` for your active printer to:

```json
"monitored_sensors": [
  { "id": "extruder",                    "display_name": "Extruder", "controllable": true,  "color": "red" },
  { "id": "heater_bed",                  "display_name": "Bed",      "controllable": true,  "color": "purple" },
  { "id": "temperature_sensor mcu_temp", "display_name": "MCU",      "controllable": false, "color": "blue" }
]
```

Then restart Guppy: `/etc/init.d/S99guppyscreen restart`. Match each `id` to a real Klipper object — `display_name` is only the label, so keep it short to avoid overlap.

## Logs

- On device: `/usr/data/printer_data/logs/guppyscreen.log`
- Raise verbosity with `log_level` (e.g. `debug`) in the active printer's config block.

## Recovering after an install you want to undo

Run the uninstaller (see [Installation](Installation) → Uninstall). Anything not auto-restored has a
copy in `/usr/data/guppyify-backup/`; restore those files manually and reboot.
