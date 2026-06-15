# Troubleshooting

| Symptom | Cause / fix |
|---|---|
| Klipper shuts down after a restart with `serialqueue … NoneType` / "Unhandled exception during run" | Known KE host-MCU reconnect race — **not** a config problem. Just restart Klipper again; a full **Restart Klipper** (Mainsail → *Restart* → host, or the installer's service restart) is more reliable than a bare `FIRMWARE_RESTART`, and a cold boot always clears it. It can need **more than one** restart. |
| Screen upside-down or 90° wrong | Set `display_rotate: 2` in `guppyconfig.json` (KE default). `0` = upside-down, `1` = wrong axis. See [Configuration](Configuration). |
| White screen after wake from sleep | Fixed on `main`; if a stuck framebuffer state remains, a full reboot clears it. |
| Simulator crashes on startup with `spdlog_ex: Failed opening file` | The directory in `log_path` doesn't exist — create it. |
| `Found arch mips / Terminating` during install | You ran `installer-deb.sh` on the KE; use `installer.sh` instead. |
| Labels wrap / buttons truncated on the small screen | Build with `GUPPY_SMALL_SCREEN=1` (sets the default font to `montserrat_10`). |
| Build fails on GCC 14+ in `lv_touch_calibration` | Add `-Wno-incompatible-pointer-types` to `CFLAGS` (see [Building from Source](Building-from-Source)). |
| `guppyscreen` keeps restarting after you `kill` it | It is supervised by `supervise-daemon`; stop it with `/etc/init.d/S99guppyscreen stop`. |
| Websocket reconnects every ~2 s in the simulator | Expected when no printer/Moonraker is connected. |
| Wrong sensors shown after upgrade | The MCU-temp default applies to fresh installs only; edit the on-device `guppyconfig.json` to change `monitored_sensors`. |
| aarch64 (`guppyscreen-arm.tar.gz`) won't run on the KE | The KE is MIPS; use the `guppyscreen-smallscreen.tar.gz` (MIPS) asset. |
| Mainsail/screen laggy or webcam stutters, signal is fine | Turn on **Low Latency** in the WiFi panel (WiFi power-save / idle-sleep / roam-scans off + Bluetooth stopped, since WiFi and BT share one 2.4 GHz radio). Also fix the basics off-device: avoid 2.4 GHz channel congestion and strong nearby emitters (e.g. a printer's Wi-Fi-Direct). See [Using OpenKE](Using-GuppyKE#settings--system). |

## Logs

- On device: `/usr/data/printer_data/logs/guppyscreen.log`
- Raise verbosity with `log_level` (e.g. `debug`) in the active printer's config block.

## Recovering after an install you want to undo

Run the uninstaller (see [Installation](Installation) → Uninstall). Anything not auto-restored has a
copy in `/usr/data/guppyify-backup/`; restore those files manually and reboot.
