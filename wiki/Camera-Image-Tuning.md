# Tuning & Saving Camera Image Settings

> **Note on the H.264 stream:** hardware H.264 streaming via go2rtc was removed in v1.2.0 due to memory
> pressure on the KE's 197 MB RAM. If you had it installed and recently upgraded, re-run the installer
> once to remove go2rtc and free ~44 MB. The stock MJPEG camera and these image-tuning settings are
> completely unaffected.

If your camera's live image looks **flat, washed-out, or dull** compared to the promo shots, you can
improve it a lot by adjusting the camera's **image controls** (contrast, saturation, brightness…). The
catch: those settings are **forgotten every time the camera re-initialises** (a reboot, a replug, or the
camera daemon restarting). This page shows how to tune them *and* make them **stick across reboots**, so
you set them once and never think about it again.

> ✅ **If you used the OpenKE installer's "Creality Nebula camera" option, you already have everything on
> this page** — it installs the `CAM_*` macros themselves (not just the persist-on-boot logic). Skip to
> [Using it](#using-it-if-you-installed-via-openke) below. The rest of this page is the **manual route**
> (no OpenKE installer, or you're specifically adding this to an existing Creality Helper Script setup),
> and explains what those macros actually do under the hood.

You don't need to be a programmer — if you can paste a command into a terminal, you can do this.

---

## Using it (if you installed via OpenKE)

Nothing to set up — from the **Klipper console** (Mainsail/Fluidd), just run:

```
CAM_CONTRAST CONTRAST=100
CAM_SATURATION SATURATION=95
CAM_BRIGHTNESS BRIGHTNESS=115
```

Each command **applies the value live and saves it** in the same step — no separate save action needed.

- All four controls (`CAM_BRIGHTNESS`, `CAM_CONTRAST`, `CAM_SATURATION`, `CAM_HUE`) range **50–160**.
- `CAM_SETTINGS` prints the camera's current saved values.
- `APPLY_CAM_SETTINGS` re-applies everything from the saved values on demand (this is also what runs
  automatically ~15s and ~40s after every boot, so your tuning survives a restart).

**Good starting point for an open-air printer with overhead LED lighting:** bump **contrast (~100)** and
**saturation (~95)** — that fixes most of the "flat/washed" look. Leave brightness near default unless white
prints blow out (then lower it). Leave white balance on auto unless you see a colour tint.

That's it — reboot whenever you like and your settings come back on their own.

> **Honest expectation:** these controls make the image *look* its best, but they can't fix the
> **softness/blockiness** of the live stream — that comes from MJPEG compression on the printer's weak CPU
> and isn't tunable. The "great quality" images you see online are usually full-resolution stills, not the
> live feed.

If `CAM_CONTRAST` etc. aren't found, the installer skipped adding them because your config already defines
macros with those names (e.g. from an existing Creality Helper Script setup) — see the manual section below,
which covers exactly that case.

---

## The manual route (no OpenKE installer, or adding this to an existing Helper Script setup)

### Why camera settings don't stick on their own

Controls like brightness/contrast/saturation are **live hardware settings** held in the camera's memory
while it's powered. Setting one (via a macro or `v4l2-ctl`) applies instantly — but it's a *runtime* value,
not saved anywhere. The moment the camera re-initialises, it comes back at its firmware defaults and your
tuning is gone. They're **not** `printer.cfg`/`SAVE_CONFIG` values, so the usual Klipper save doesn't cover
them.

The fix is a small pattern (this is exactly what OpenKE's own macros do):

1. **`[save_variables]`** stores your chosen values in a file.
2. Each **`CAM_*` macro** applies the value live **and** saves it.
3. A **`[delayed_gcode]`** re-applies the saved values shortly after every boot.

Result: you change a setting once (from the Klipper console or a Mainsail macro button) and it's both
applied and remembered — no config editing.

### What this guide was tested on

| | |
|---|---|
| Printer | Creality Ender-3 V3 KE |
| Camera | Creality **Nebula** (USB UVC, controls on `/dev/video4`) |
| Builds on | Creality Helper Script → **Nebula Camera Settings Control** (provides the `CAM_*` macros, if you're not using OpenKE's own) |
| Interface | Mainsail (Fluidd works too) |

### Before you start

- ⏱️ **Time:** ~5 minutes.
- 🧰 **You need:** the printer on your network, a computer, and the Helper Script's **Nebula Camera Settings
  Control** installed (gives you `CAM_BRIGHTNESS`, `CAM_CONTRAST`, etc.).
- ↩️ **Reversible:** you'll back the file up first; removing the added block reverts everything.

### Step 1 — Make the macros save-on-set + add the boot rule

Open `Helper-Script/camera-settings.cfg` (in Mainsail: **Machine** tab, or over SSH at
`/usr/data/printer_data/config/Helper-Script/camera-settings.cfg`). The Helper Script's `CAM_*` macros
already *apply* a value; we add a `SAVE_VARIABLE` line to each so it also **saves**, then add the boot
re-apply rule.

Each macro gets one extra line. For example `CAM_CONTRAST`:

```ini
[gcode_macro CAM_CONTRAST]
description: min=50 / max=160
gcode:
    {% set contrast = params.CONTRAST|default(printer.save_variables.variables.cam_contrast|default(100)) %}
    RUN_SHELL_COMMAND CMD=v4l2-ctl PARAMS="-d /dev/video4 --set-ctrl contrast="{contrast}
    SAVE_VARIABLE VARIABLE=cam_contrast VALUE={contrast}        # <-- added
```

Do the same for `CAM_BRIGHTNESS` (`cam_brightness`), `CAM_SATURATION` (`cam_saturation`), and `CAM_HUE`
(`cam_hue`) — these four (brightness, contrast, saturation, hue) are exactly the controls OpenKE's own
macros manage. Then add a re-apply macro and the boot timer at the end of the file:

```ini
[gcode_macro APPLY_CAM_SETTINGS]
description: Re-apply all saved camera image settings
gcode:
    {% set v = printer.save_variables.variables %}
    RUN_SHELL_COMMAND CMD=v4l2-ctl PARAMS="-d /dev/video4 --set-ctrl brightness="{v.cam_brightness|default(115)}
    RUN_SHELL_COMMAND CMD=v4l2-ctl PARAMS="-d /dev/video4 --set-ctrl contrast="{v.cam_contrast|default(100)}
    RUN_SHELL_COMMAND CMD=v4l2-ctl PARAMS="-d /dev/video4 --set-ctrl saturation="{v.cam_saturation|default(95)}
    RUN_SHELL_COMMAND CMD=v4l2-ctl PARAMS="-d /dev/video4 --set-ctrl hue="{v.cam_hue|default(50)}

# Re-apply ~15s after Klipper is ready, then once more at +40s in case the
# camera wasn't initialised yet on a cold boot.
[delayed_gcode APPLY_CAM_SETTINGS_ON_BOOT]
initial_duration: 15
gcode:
    APPLY_CAM_SETTINGS
    UPDATE_DELAYED_GCODE ID=APPLY_CAM_SETTINGS_RETRY DURATION=40

[delayed_gcode APPLY_CAM_SETTINGS_RETRY]
gcode:
    APPLY_CAM_SETTINGS
```

> ### ⚠️ Only one `[save_variables]` allowed
> `SAVE_VARIABLE` needs a `[save_variables]` section, but **Klipper permits exactly one** in the whole
> config. If you have the Helper Script's **Save Z-Offset**, or OpenKE's own Save Z-Offset / camera-tuning
> feature, already installed, one already exists (filename `variables.cfg`, possibly under a
> `Helper-Script/` subfolder depending on which one) — **reuse it, don't add a second**, or Klipper will
> misbehave. Only if you have *no* `[save_variables]` anywhere should you add one:
> ```ini
> [save_variables]
> filename: /usr/data/printer_data/config/variables.cfg
> ```

### Step 2 — Restart Klipper

In the **Klipper console** (Mainsail/Fluidd) or over SSH:

```
FIRMWARE_RESTART
```

> On the KE, `FIRMWARE_RESTART` occasionally fails to reconnect to the mainboard the first time (a
> `serialqueue ... NoneType` shutdown). If that happens, just run `FIRMWARE_RESTART` once more — it's a
> known KE quirk, unrelated to this change.

### Step 3 — Tune to taste (and it saves automatically)

From the console, set values and watch the live feed change instantly. Each command **applies and saves**:

```
CAM_CONTRAST CONTRAST=100
CAM_SATURATION SATURATION=95
CAM_BRIGHTNESS BRIGHTNESS=115
```

- All four controls (brightness, contrast, saturation, hue) range **50–160**.
- `CAM_SETTINGS` prints the camera's current values.
- `APPLY_CAM_SETTINGS` re-applies everything from the saved file on demand.

**Good starting point for an open-air printer with overhead LED lighting:** bump **contrast (~100)** and
**saturation (~95)** — that fixes most of the "flat/washed" look. Leave brightness near default unless white
prints blow out (then lower it). Leave white balance on auto unless you see a colour tint.

That's it — reboot whenever you like and your settings come back on their own.

---

## How to confirm it persisted

Set a value (e.g. `CAM_CONTRAST CONTRAST=110`), reboot the printer, open the camera feed, and check the
look held. To verify on the technical side, your value will be written into whichever `variables.cfg` your
`[save_variables]` section points at — check the `filename:` under `[save_variables]` in your config if
you're not sure which one that is, then:

```sh
grep cam_ <that filename>
```

---

## Undoing it

**Installed via OpenKE:** re-run the installer's optional-features step and skip the Creality Nebula camera
option, then remove `GuppyScreen/nebula_camera.cfg` and `FIRMWARE_RESTART`.

**Installed manually:** remove the `SAVE_VARIABLE` lines you added and the `APPLY_CAM_SETTINGS*` blocks from
`camera-settings.cfg`, then `FIRMWARE_RESTART`. (Or restore your backup of the file.) The camera will go
back to forgetting settings on reboot.

---

## Troubleshooting

- **`Unknown save variable` / macro errors after editing** — usually a missing or duplicate
  `[save_variables]`. Make sure there's exactly **one** in your whole config (see the warning above).
- **Settings don't come back after a reboot** — the camera may not have been ready when the boot rule
  fired. The `+40s` retry covers most cases; if your camera is slow to start, raise `initial_duration`.
- **Camera shows nothing at all** — that's a different problem (the camera pipeline can hang: "connected
  but no image"). A printer **reboot** clears it. Tuning only matters once the feed is live.
- **Image still soft/blocky** — that's MJPEG stream compression, not these controls; it can't be tuned away
  here.
