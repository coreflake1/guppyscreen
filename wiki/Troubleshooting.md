# Troubleshooting

Quick-reference for the most common problems. If your symptom isn't here, check [Known limitations](#known-limitations) below, or open an issue on GitHub.

---

## Screen & display

| Symptom | Fix |
|---|---|
| Screen upside-down or 90° wrong | Set `display_rotate: 2` in `guppyconfig.json` (KE default). `0` = upside-down, `1` = wrong axis. See [Configuration](Configuration). |
| Touch targets shifted after changing `display_rotate` | Calibration coefficients are rotation-specific. Redo: **Settings → System → Reset Touch Calibration**. See [Screen reference → System panel](Using-GuppyKE#system-panel). |
| Touch feels inverted / wildly off after calibration | The calibration captured bad taps. Reset to raw mode via SSH then re-run the wizard: `python3 -c "import json; d=json.load(open('/usr/data/guppyscreen/guppyconfig.json')); d['touch_calibrated']=False; d.pop('touch_calibration_coeff',None); json.dump(d,open('/usr/data/guppyscreen/guppyconfig.json','w'),indent=2)"` then restart. |
| White screen after waking from sleep | Fixed on current builds. If still stuck, a full reboot clears it. |
| Wrong sensor names / label overlaps value | Your `guppyconfig.json` has an empty or missing `monitored_sensors` list — usually from an on-screen update that only swapped the binary (see below). Recent installer versions merge your settings instead of overwriting them, so re-running the installer is now safe. See [Sensor labels wrong](#sensor-labels-are-wrong--overlap-the-value) below. |

---

## Printing & first layer

| Symptom | Fix |
|---|---|
| First layer uneven left↔right, bed mesh didn't help | [Axis Twist Compensation](Axis-Twist-Compensation) — the X gantry is slightly twisted, tilting the probe. |
| First layer uneven all over | Re-run bed mesh: **Tune → Bed Mesh → Calibrate**. Make sure the bed is clean (IPA) and nothing is loose. |
| First layer too high or too low everywhere | Baby-step Z-offset: **Tune → Z Offset** or tap the Z readout while printing. |
| Layer shift right after pause or cancel, then a "bang" sound | The Y park position was set too high and the bed hit the frame — either on resume (`PAUSE`'s `y_park`) or when cancelling (`CANCEL_PRINT`'s own separate, hardcoded copy of the same value — found and fixed later, since it wasn't touched by the original `PAUSE` fix). Both are **auto-fixed** in the current installer — re-run it to get the fix. If you still see it on an updated install, check that both `y_park` in `PAUSE` and the `Y222` in `CANCEL_PRINT`'s park move (`gcode_macro.cfg`) are `220` or lower. |
| Parts not square / parallelogram | [Skew Correction](Skew-Correction). |
| Ghosting / echoes after sharp corners at speed | [Input shaper](Calibration-Explained#step-5--input-shaper-kill-ringingghosting) — Tune → Input Shaper. |
| Adaptive mesh (`ADAPTIVE=1`) seems ignored — full-bed mesh used instead | If your slicer's custom start-gcode has a leftover `BED_MESH_PROFILE LOAD=default` line (common from older KAMP-based profiles), it can silently reload the full-bed mesh right after the fresh adaptive one calibrates. Remove that line — [Adaptive Print Setup](Adaptive-Print-Setup) no longer needs it. (Older installs also had a bug where the adaptive mesh saved under a random profile name instead of staying unsaved — fixed, and a leftover stale `adaptive-*` profile from that period is cleaned up automatically on the next install/update.) |
| Corner bulges, blobs, or gaps | Pressure advance and/or flow — [Calibration walkthrough](Calibration-Explained#step-6--pressure-advance-flow--temperature-slicer-side). |

---

## Klipper errors

| Symptom | Fix |
|---|---|
| Klipper shuts down after a restart with `serialqueue … NoneType` / "Unhandled exception during run" | Known KE host-MCU reconnect race — **not** a config problem. Run `FIRMWARE_RESTART` once more, or a full **Restart Klipper** from Mainsail. A cold reboot always clears it. It can take more than one restart. |
| Klipper shuts down with `Unable to connect` after installing mods | The same race condition — restart Klipper once more. If it persists, check for duplicate `[include]` lines in `printer.cfg`. |
| `Skipping Save Z-Offset`, `Skipping M600`, or `Skipping Exclude Object` during install | The installer found those sections already in your `printer.cfg` (from a previous install or the Creality helper script) and skipped adding duplicates. Your existing config is used as-is — no action needed. |
| `Terminating... Your OS Platform has not been tested with Guppy Screen` during install | You ran `installer-deb.sh` on the KE — that script is for a different (aarch64/Debian) target, not this printer. Use `installer.sh` instead. |
| Installer prints `tar: can't remove old file ... Is a directory` / `NotADirectoryError`, or the same install failure repeats no matter how many times you re-run it | Two separate causes, both fixed: a v1.3.1-era release-packaging bug shipped a broken settings template (fixed then); separately, *any* prior corrupt or interrupted extraction (a partial download, a killed install, etc.) could leave a stale file/directory behind that every future run then collides with forever, since nothing cleaned it up first. The installer now clears its own known packaged directories before every `tar`/`zip` extraction step, so a bad prior attempt can no longer permanently block future ones. If you're stuck on this on an old install, `rm -rf /usr/data/guppyscreen` (or the equivalent Moonraker/nginx/Mainsail directory named in the error) then re-run the installer once. |
| Installer refuses to start, complains storage is critically low | New safety check — it now aborts *before* writing anything if free space is too low to safely complete, rather than failing partway through. Free up space (see [Disk space](#disk-space) above) and re-run it. |
| Mainsail/web UI broken or unreachable after updating | Older installs could get nginx into a bad state during an upgrade (a version-comparison bug could skip needed config fixes, and in one case an nginx upgrade could clobber Mainsail's own config). The installer now self-heals nginx on every run (checks `nginx -t`, not just a marker string) regardless of version — just re-run it. |

---

## WiFi & connectivity

| Symptom | Fix |
|---|---|
| Mainsail feels laggy, camera stutters, screen takes a beat to respond | Enable **Low Latency** in Settings → WIFI. This disables WiFi power-save and Bluetooth (which share the same antenna on the KE). See [Screen reference → WiFi panel](Using-GuppyKE#wifi-panel). |
| Mainsail still laggy with Low Latency on | Check for nearby 2.4 GHz congestion (e.g. a neighbour's network or a HP WiFi-Direct device on channel 6). Move your router channel to 1 or 11. |
| `guppyscreen` keeps restarting after you `kill` it | It's supervised by `supervise-daemon`. Stop it properly: `/etc/init.d/S99guppyscreen stop`. |

---

## Memory / freeze

| Symptom | Fix |
|---|---|
| Screen goes dark, SSH stops responding, only a power-cycle recovers | Memory pressure causing eMMC swap-thrash (the KE has 197 MB RAM). Most common cause is the H.264 camera stream add-on (go2rtc + ffmpeg = ~44 MB). If you have it, re-run the installer — v1.2.0+ removes it automatically. If you're on a current install and still seeing this, check `free -m` over SSH while it's healthy and look for anything large consuming memory. |

---

## Simulator & developer issues

| Symptom | Fix |
|---|---|
| Simulator crashes on startup with `spdlog_ex: Failed opening file` | The directory in `log_path` doesn't exist — create it. |
| Websocket reconnects every ~2 s in the simulator | Expected when no printer/Moonraker is connected. |
| Labels wrap / buttons truncated in the sim | Build with `GUPPY_SMALL_SCREEN=1` (sets the default font to `montserrat_10`). |
| Build fails on GCC 14+ in `lv_touch_calibration` | Add `-Wno-incompatible-pointer-types` to `CFLAGS` — see [Building from Source](Building-from-Source). |
| aarch64 asset (`guppyscreen-arm.tar.gz`) won't run on the KE | The KE is MIPS — use `guppyscreen-smallscreen.tar.gz`. |

---

## Sensor labels are wrong / overlap the value

If the home screen shows a sensor labelled **Temperature** (overlapping its reading), or `Heater Bed`
instead of `Bed`, your `guppyconfig.json` has an empty or missing `monitored_sensors` list — the app
falls back to showing the raw internal sensor name instead of a friendly one. This most often happens
after the printer was updated **from the screen** (Settings → Update Guppy), which only swaps the
binary and never touches `guppyconfig.json` at all.

**As of v1.3.1, this is much safer to fix than it used to be.** Re-running the installer no longer
overwrites your settings file — it merges the packaged defaults *underneath* whatever you already have,
so any setting you've already customized (including a custom sensor list) is kept as-is. It will also
print a note in the install log if one of a handful of specific settings differs from the recommended
default, purely informational — your value is never changed without you doing it.

**Easiest fix — re-run the installer:**
```sh
sh -c "$(wget --no-check-certificate -qO - https://raw.githubusercontent.com/coreflake1/guppyscreen/main/scripts/installer.sh)"
```

**Surgical fix — edit by hand instead.** Open `/usr/data/guppyscreen/guppyconfig.json` and set
`monitored_sensors` to:

```json
"monitored_sensors": [
  { "id": "extruder",                    "display_name": "Extruder", "controllable": true,  "color": "red" },
  { "id": "heater_bed",                  "display_name": "Bed",      "controllable": true,  "color": "purple" },
  { "id": "temperature_sensor mcu_temp", "display_name": "MCU Temp", "controllable": false, "color": "blue" }
]
```

Then restart: `/etc/init.d/S99guppyscreen restart`.

> If you were on a release **before v1.3.1** and hit this repeatedly no matter how many times you
> reinstalled, that was a real bug (a release-packaging error that silently kept shipping a years-old
> settings template) — not something you were doing wrong. It's fixed now; a single reinstall on v1.3.1
> or later should resolve it for good.

---

## Disk space

- **Old `SAVE_CONFIG` backups and log files filling up storage** — every `SAVE_CONFIG` (from
  calibration wizards, bed mesh, etc.) leaves a timestamped `printer-*.cfg` backup, and these
  accumulate forever on stock Klipper (133+ files / going back over a year is normal on an
  unmaintained printer). Current installs add a small recurring cleanup service (runs at boot)
  that rotates these down to just the single newest one past a 7-day grace period, does the same
  for OpenKE's own install-time config backups, clears old rotated log files, and (only if free
  space drops below 1 GB) removes gcode files older than 7 days. No action needed, nothing to
  configure, and it never touches anything less than a week old or any one-time named
  backup/restore file. If you're on an older install and seeing storage fill up, re-run the
  installer to pick this up.

## Logs

- On device: `/usr/data/printer_data/logs/guppyscreen.log` (rotating, up to 30 MB)
- Raise verbosity: **Settings → Log Level** → `debug` to capture more detail, then back to `info`

---

## Known limitations

- **"View Job" / "Queue Job" buttons** in the "print already in progress" dialog are unimplemented stubs (Moonraker job-queue — low priority on a single-printer KE).
- **No automated unit-test suite.** Verification is via the CI build, the simulator, and on-device testing — see [Development and Simulator](Development-and-Simulator).
- **CI builds only the KE smallscreen target** (`guppyscreen-smallscreen`, MIPS). aarch64 and standard-screen MIPS can be built from source but aren't CI artifacts.
- **`wget --no-check-certificate`** in the installer/updater — BusyBox wget on the KE has no CA bundle. Be mindful when installing over untrusted networks.
- **Installer pins a release tag** — `PINNED_RELEASE` in `installer.sh` must be bumped at each release. The installer script itself is always fetched fresh from `main`, so the install command on the [Installation](Installation) page stays current.
