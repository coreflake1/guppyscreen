# Screen Reference — Every Panel, Every Setting

This is the plain-English reference for every screen and setting in OpenKE. No assumptions, no jargon.
If you're looking at something and wondering "what does this do?", find it here.

OpenKE replaces the stock Creality screen with a faster, fuller one. Everything runs **on the printer
itself** — you don't need a phone or PC for day-to-day printing (though Mainsail in a browser works
fine too).

> 🔒 **You can't break a running print by exploring the screen.** Anything that would interrupt a job is
> blocked or asks you to confirm first. Tap around freely.

---

## Getting around — the five tabs

A row of icon buttons runs across the bottom of every screen. Tap one to switch tabs. Tapping the
**already-active** tab resets it (Macros jumps back to your favourites list, Console clears the input).

| Tab | What's here |
|---|---|
| 🏠 **Home** | Temperatures, graph, and the buttons you use every day |
| **Macros** | Your Klipper gcode macros |
| **Console** | Type gcode directly or browse commands |
| **Tune** | Every calibration tool |
| **Settings** | WiFi, system, Spoolman, log level |

---

## 🏠 Home tab

Your everyday screen. Everything you need to babysit a print or start a new one.

### Temperature panel

Shows **nozzle, bed, chamber, and MCU** temperatures as a live read-out.

- **Tap any temperature** to open a numpad and set a target (preheat). The label shows both the
  current and target value — e.g. `210 / 200` means currently 210 °C, cooling toward 200 °C.
- **Temperature presets** appear when you tap: quick-select PLA, PETG, ABS, TPU (pre-set values), or
  type any number.
- **Tap the target** (the smaller number) to clear it (set to 0, i.e. off / cool down).
- **Chamber and MCU** temperatures are display-only — they have no heater target.

### Temperature graph

A rolling 60-second (approx.) line chart of all monitored temperatures, colour-coded. Useful for seeing
if your heater is stable at target or still swinging.

### Quick action buttons

Below the temperatures:

| Button | What it does |
|---|---|
| **Home** | Homes all axes (G28 — same as starting a print) |
| **Home Z** | Homes Z only |
| **Move** | Opens the movement panel (jog the toolhead manually) |
| **Fans** | Opens the fan-speed panel |
| **LED** | Toggles the case light on/off |
| **Extruder / Filament** | Opens the filament load/unload panel |
| **Files** | Opens the file browser to pick a print |

### Files browser

Lists `.gcode` files on internal storage and USB sticks. Tap a file to preview it (thumbnail if your
slicer generates one, estimated time, file size). Tap **Print** to start it.

- **USB sticks** are scanned automatically when plugged in.
- **Sort:** the list remembers your last sort order (name / date / size).
- Deleting, creating folders, and renaming are not supported — manage files from Mainsail or your
  slicer's upload tool.

---

## Print status screen

Appears automatically when a print is running. You can also reach it by tapping the compact status bar
at the top of any screen during a print.

| Display | What it shows |
|---|---|
| File name + thumbnail | The file being printed |
| Progress bar | Percentage complete (by filament, not time) |
| Time remaining / elapsed | Estimates based on current speed |
| Layer X / Y | Current layer and total layers |
| **Z offset** (live) | Tap to baby-step the first layer — see below |
| Speed % | How fast relative to the slicer's set speeds |
| Flow % | Extrusion multiplier |
| Pressure Advance | PA value in use |
| Filament used | How much has been extruded so far |

### Baby-stepping Z-offset while printing

Tap the **Z-offset readout** on the print-status screen to get +/− buttons and a step-size selector.
Step sizes: 0.001, 0.005, 0.01, 0.05 mm. Nudge while watching the first layer:

- **Lines not fusing / gaps between them:** too high — lower (negative direction)
- **Lines translucent / squashed flat:** too low — raise (positive direction)
- **Lines squished together and fused with no gaps:** correct

The value is saved automatically when you stop adjusting.

### Pause, Resume, Cancel, Emergency Stop

- **Pause** — parks the toolhead, holds temperatures. Resume brings it back.
- **Cancel** — cancels the print. A confirmation dialog appears (red Confirm button). For the **last
  object** being excluded, it cancels the whole print.
- **Emergency Stop** — cuts all movement and heaters immediately. Use only if something is going wrong
  physically (not a normal cancel). The printer needs a full power-cycle or `FIRMWARE_RESTART` after this.

---

## Movement panel

Tap the **Move** button from the Home tab.

### Jog controls

- **Step size:** 0.1 mm / 1 mm / 10 mm / 50 mm — tap to select before pressing a direction button.
- **X−/X+, Y−/Y+, Z−/Z+** — move the toolhead by the selected step. On a bed-slinger like the KE,
  Y moves the bed (forward/backward).
- The current XYZ position is shown at the top.

> ⚠️ The machine doesn't know where it is until you home it. Moving before homing can crash the toolhead
> into the frame. The **Home** button is right there.

### Home buttons

- **Home All (🏠)** — G28 — homes X, Y, and Z in sequence.
- **Home Z** — homes Z only (useful after adjusting Z-offset).

### Motors off

Disables the stepper motors so you can move the toolhead by hand. They re-engage on the next move
command or home.

### Invert Y

A toggle that flips which direction Y+ and Y− move the bed. Some users find the default unintuitive on
a bed-slinger (the bed moves toward you when you expect it to move away). Toggle this to match your
mental model.

---

## Fan panel

| Control | What it does |
|---|---|
| **Part cooling** | Slider 0–100%. This is the fan that blows on the print. 0% for first layer of ABS/ASA; 100% for PLA/PETG bridges. |
| **Case fan** (if fitted) | Extra ventilation fan inside the enclosure, if configured. |
| **Auxiliary fan** (if configured) | Any additional fans defined in your `fans:` config. |

Fan speeds set here are *immediately* applied and override whatever the gcode has been sending.

---

## Extruder / Filament panel

### Load filament

Heats the nozzle to the last-used target (or a default if you haven't set one), then feeds filament
forward until you tap **Stop**. A "Use this filament?" Spoolman confirmation appears first if you have
an active spool — tap Yes to proceed. After the load is done, the nozzle **auto-cools** back to ambient.

> Start Load when your filament is already inserted into the extruder throat. Watch for plastic coming
> out the nozzle tip — when it runs clean and consistent, stop.

### Unload filament

Heats the nozzle, then retracts filament fully. Auto-cools afterward. Designed to pull the filament back
past the bowden/throat; you then remove it manually from the top.

### Manual extrude / retract

For precise manual extrusion when the nozzle is already hot:

- **Distance:** how far to feed (mm)
- **Speed:** feedrate (mm/s)
- **Extrude / Retract** buttons — each press moves that amount at that speed

The nozzle must be at temperature for extrusion to work (Klipper enforces this). Manual extrude also
auto-cools after each operation.

---

## Tune tab

The calibration and print-quality toolbox. You won't need most of these day-to-day — but when you do,
they're on the screen, no PC needed.

### Bed Mesh

Shows the bed's height map.

- **3D view** — a colour surface you can rotate (drag), zoom (pinch or buttons), and tilt. Peaks
  (high spots) and valleys (low spots) are visible at a glance.
- **Table view** — the numeric Z values at each probe point.
- **Re-mesh button** — runs `BED_MESH_CALIBRATE` to map the whole bed fresh. Takes 2–3 minutes.
- **Enable/disable** — toggle whether the mesh compensation is applied to the current print. Default is
  on; you'd turn it off only for diagnostic purposes.

A good mesh has subtle variation (a few tenths of a millimetre difference corner-to-corner). Wild spikes
are usually noise from a dirty nozzle or a debris on the bed.

### Z Offset

Live first-layer baby-stepping. Same as tapping Z-offset on the print-status screen, but accessible
from the Tune tab even outside a print.

- **+/− buttons** with step sizes: 0.001, 0.005, 0.01, 0.05 mm
- **Current value** displayed at top
- **Save** — saves the current value to the config (with the Helper Script Save Z-Offset macro — installed by the installer)

> This is the recommended way to set Z-offset on the KE. Manual baby-stepping on a real first layer is
> more reliable than the strain-sensor auto-calibration — see
> [Calibration walkthrough](Calibration-Explained#3-z-offset--first-layer-the-big-one).

### Axis Twist

Launches the 5-point paper calibration wizard for Axis Twist Compensation. See [Axis Twist Compensation](Axis-Twist-Compensation) for the full guide and what this fixes.

### Skew

Corrects parts that come out as a slight parallelogram instead of a perfect square. You print a test
frame, measure three lengths with calipers (AC, BD, and AD diagonals), type them into the three fields,
and tap **Apply**. Save with **SAVE_CONFIG** in the console. See [Skew Correction](Skew-Correction).

### Fine Tune

Sliders for real-time adjustments while printing — or any time the nozzle is moving:

| Control | What it does | Normal range |
|---|---|---|
| **Speed %** | Global speed multiplier. 100% = follow slicer speeds exactly. | 50–150% |
| **Flow %** | Extrusion multiplier. 100% = exact filament amount. Under-extruding: raise. Over-extruding: lower. | 90–110% |
| **Pressure Advance** | How aggressively the extruder compensates for nozzle pressure lag at speed. Fixes corner bulges and blobs. | 0–0.08 typically |
| **Firmware Retraction** | Retraction length and speed if your slicer uses `G10`/`G11` firmware retraction commands. | depends on filament |

### Input Shaper

Reduces ghosting/ringing — those faint echoes that trail sharp corners at speed. The KE has an onboard
accelerometer, so you measure and apply it from this panel.

1. **Select axis** (X or Y) and tap **Run test** — the printer shakes at a sweep of frequencies.
2. The panel shows a **frequency-response graph** for that axis. Peaks = resonant frequencies.
3. **Recommended shaper** and frequency are shown automatically — tap **Apply** to use them.
4. Repeat for the other axis.

> 💡 **Accelerometer placement for the Y test:** the sensor needs to be on *whatever moves* — for Y
> that's the **bed**, not the toolhead. Tape or zip-tie it to the bed for the Y measurement; it comes
> right off after.

After applying, a fast ringing-test print (a small square with sharp corners at speed) should show no
echoes trailing the corners. If it does, try a different shaper type from the dropdown.

### Belts / Shake

Excites each axis at a specific frequency so you can compare the mechanical resonance. Primary use is
comparing **left vs. right belt tension** on the X axis — matched tension = cleaner prints. If one belt
is audibly different from the other when plucked, tighten the looser one.

### Retraction

If your slicer uses **firmware retraction** (`G10`/`G11` commands instead of explicit `G1 E-x` moves),
these sliders control the retraction distance and speed that the firmware actually uses.

Most slicers (OrcaSlicer default) use regular gcode retraction — in that case, these settings have no
effect.

### Limits

Speed and acceleration caps for the whole machine:

| Setting | What it is |
|---|---|
| **Max velocity** | Hard cap on axis speed (mm/s) |
| **Max acceleration** | Hard cap on acceleration (mm/s²) |
| **Max print acceleration** | Acceleration cap used while actually extruding |
| **Square corner velocity** | How fast the toolhead can turn a sharp corner without decelerating fully |

Raising these doesn't automatically make prints faster — your slicer's own speed settings have to be
raised too. Raising them beyond the machine's structural limit causes ringing/vibration artifacts.

### TMC Autotune

One-time setup for quieter, cooler stepper motors. Select your motor type and a goal (Performance or
Silent) and tap **Apply** — it computes and saves optimal stepper-driver settings that activate on
every boot.

> The button is greyed out until the TMC Autotune Klipper module is installed — the OpenKE installer
> does this during the print-quality mods step. See [TMC Autotune](TMC-Autotune) for the full guide.

### TMC Metrics

A live dashboard of your stepper drivers — current draw, driver temperature, and internal parameters.
This is a diagnostic tool; you won't need it for normal printing. Use it if you suspect a motor is
getting hot or skipping steps.

### Power Settings

| Setting | What it does |
|---|---|
| **Power devices** | On/off buttons for any smart plugs or relays you've configured in Moonraker |
| **Power Loss Recovery** | If a power cut interrupted a print, tap here to resume it from where it stopped (print file must still be on the printer) |

---

## Macros tab

All your Klipper gcode macros, accessible from the screen.

- **Favourites** — macros you've pinned for quick access. Tap the star icon on any macro to pin it.
  Tapping the Macros tab when already on it jumps back to the Favourites view.
- **All macros** — scrollable list, grouped by category (the `[gcode_macro]` section headers).
- **Parameters** — tap a macro to expand it and see any parameters it accepts. Fill them in before
  running.

---

## Console tab

Direct gcode access. Two modes:

### Command browser

A drill-down interface: tap a **category** to see the commands in it, tap a **command** to see its
description and parameters. Tap **Run** to execute it. This is the recommended way for exploring what
commands exist without needing a cheat sheet.

Tapping the Console tab when already on it jumps back to the top of the browser.

### Direct input

Type any gcode command directly, same as typing in the Mainsail terminal. History is kept (last 100
entries, with spam filtering so rapidly repeating commands don't fill it up). Tap a history entry to
re-run it.

---

## Settings tab

System-level settings for the printer and OpenKE itself.

### Network / WiFi

| Setting | What it does |
|---|---|
| **WiFi networks list** | Scans and shows available networks. Tap one to connect (prompts for password). |
| **IP address** | Your printer's current local IP — handy for typing into Mainsail on a new device. |
| **Low Latency mode** | Disables the WiFi radio's power-save, stops idle sleep, disables background network scans, and turns off Bluetooth. **Why Bluetooth?** The KE's WiFi and Bluetooth share a single 2.4 GHz radio and antenna. Leaving BT on (it's unused here) makes WiFi yield to it periodically, causing latency spikes and stuttering. Low Latency mode eliminates that. **Persists across reboots.** Turn it off to go back to stock (re-enables Bluetooth). |

> If Mainsail feels laggy, the camera stutters, or you notice the screen taking a beat to respond to
> commands — turn on **Low Latency** first. It fixes most WiFi-related sluggishness.

### System Info

Shows printer vitals — RAM, CPU load, disk usage, firmware version, Klipper version.

Buttons:

| Button | What it does |
|---|---|
| **Reset Touch Calibration** | Runs a fresh 9-tap touch calibration wizard (3 taps per crosshair, averaged for accuracy). Run this once after first install, and any time tap targets feel off or you've changed `display_rotate`. Takes about 30 seconds. |
| **Reset Options → Factory Reset** | Wipes everything back to stock. See [Resetting & Uninstalling](Resetting-and-Uninstalling). |
| **Update Guppy** | Downloads and installs the latest OpenKE release. **Note:** this only swaps the screen binary. If you want to also update Klipper mods, KAMP config, etc., re-run the full installer instead. |

### Spoolman

Filament inventory integration (requires a [Spoolman](https://github.com/Donkie/Spoolman) server on
your network — completely optional).

| Feature | What it does |
|---|---|
| **Spool list** | Shows all your spools — name, material, colour, remaining weight and length. |
| **Set active spool** | Tap a spool + **Set Active** to track usage against it. |
| **Archive** | Mark an empty spool as archived to keep the list tidy. Archived spools can be shown/hidden with a toggle. |
| **Auto tracking** | Once a spool is active, filament used by each print is subtracted automatically. No weighing. |
| **Wrong-filament check** | Before starting a print (or a manual Load), OpenKE shows a **"Use this filament?"** popup showing the active spool. Confirm or cancel. Catches the "oops wrong material" mistake before it ruins a print. |

No Spoolman server = Spoolman panel doesn't appear. Nothing else changes.

### Log Level

Controls how verbose the `guppyscreen.log` file is.

| Level | What gets logged | Use when |
|---|---|---|
| **warn** | Only warnings and errors | Normal day-to-day use. Log stays small. |
| **info** | Warnings + general events (connections, state changes) | **Default for end users.** Good balance — enough to diagnose most issues. |
| **debug** | Everything including per-frame events | Active debugging. Fills the log fast (~14 hours per 10 MB). |
| **trace** | Maximum verbosity | Developer use only. |

If you're reporting a bug or sending logs to a maintainer, switch to **debug** to capture more detail,
reproduce the issue, then switch back to **info**.

The log lives at `/usr/data/printer_data/logs/guppyscreen.log` (up to 30 MB across 3 rotating files).
