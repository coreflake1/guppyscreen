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
| Layer shift right after pause, then a "bang" sound | The Y park position was set too high and the bed hit the frame on resume. This is **auto-fixed** in the current installer — re-run the installer to get the fix. If you see it on an updated install, check that `y_park` in your `PAUSE` macro is `220` or lower. |
| Parts not square / parallelogram | [Skew Correction](Skew-Correction). |
| Ghosting / echoes after sharp corners at speed | [Input shaper](Calibration-Explained#step-5--input-shaper-kill-ringingghosting) — Tune → Input Shaper. |
| Corner bulges, blobs, or gaps | Pressure advance and/or flow — [Calibration walkthrough](Calibration-Explained#step-6--pressure-advance-flow--temperature-slicer-side). |

---

## Klipper errors

| Symptom | Fix |
|---|---|
| Klipper shuts down after a restart with `serialqueue … NoneType` / "Unhandled exception during run" | Known KE host-MCU reconnect race — **not** a config problem. Run `FIRMWARE_RESTART` once more, or a full **Restart Klipper** from Mainsail. A cold reboot always clears it. It can take more than one restart. |
| Klipper shuts down with `Unable to connect` after installing mods | The same race condition — restart Klipper once more. If it persists, check for duplicate `[include]` lines in `printer.cfg`. |
| `Skipping Save Z-Offset`, `Skipping M600`, or `Skipping Exclude Object` during install | The installer found those sections already in your `printer.cfg` (from a previous install or the Creality helper script) and skipped adding duplicates. Your existing config is used as-is — no action needed. |
| `Terminating... Your OS Platform has not been tested with Guppy Screen` during install | You ran `installer-deb.sh` on the KE — that script is for a different (aarch64/Debian) target, not this printer. Use `installer.sh` instead. |
| Installer prints `tar: can't remove old file ./guppyscreen/debian: Is a directory` | This was a symptom of a release-packaging bug that shipped a broken settings template in every release; it's fixed as of v1.3.1. If you see this on a current release, please report it — it shouldn't happen anymore. |

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
