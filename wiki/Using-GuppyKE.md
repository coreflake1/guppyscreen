# Screen Reference — Every Panel, Every Setting

Plain-English reference for every screen in OpenKE. If you're looking at something and wondering
"what does this do?" — find it here.

> 🔒 **You can't break a running print by exploring the screen.** Anything that would interrupt a job is
> blocked or asks you to confirm first. Tap around freely.

---

## Getting around — the five tabs

A column of icon buttons runs down the **left edge** of the screen. Tap one to switch. Tapping the
already-active tab resets it (Macros jumps back to favourites, Console clears the input).

| Tab | What's here |
|---|---|
| 🏠 **Home** | Temperatures, graph, and the buttons you use every day |
| **Macros** | Your Klipper gcode macros |
| **Console** | Type gcode directly or browse commands |
| **Tune** | Grid of calibration tool launchers |
| **Settings** | Grid of system panel launchers |

> **Both Tune and Settings work the same way:** each is a grid of large icon buttons. Tapping one opens
> a full-screen sub-panel. Nothing is adjusted or configured inline on these tabs themselves.

---

## 🏠 Home tab

Your everyday screen. Inline content — no launcher grid.

### Temperature panel

Shows **nozzle, bed, chamber, and MCU** temperatures as live read-outs with target values.

- **Tap any temperature** to set a target (preheat). A numpad opens. The reading shows current/target
  — e.g. `45 / 210` means currently 45 °C, heating toward 210 °C.
- **Presets** appear below the numpad: PLA, PETG, ABS, TPU quick-select buttons.
- **Tap the target** to clear it (set to 0 — cool down / off).
- Chamber and MCU are display-only (no heater to control).

### Temperature graph

A rolling line chart of all monitored temperatures, colour-coded. Useful for watching whether a heater
is stable at target or still swinging.

### Action buttons

Five icon buttons below the temperatures. Each opens a **floating panel** over the current screen:

| Button | Opens |
|---|---|
| **Homing** | Movement panel (jog + home axes) |
| **Extrude** | Filament panel (load/unload + manual extrude) |
| **Fans** | Fan speed controls |
| **LED** | Case light controls |
| **Print** | File browser |

---

## Homing panel (movement)

Opens from the Home tab **Homing** button.

### Jog controls

- **Step size selector:** 0.1 mm / 1 mm / 10 mm / 50 mm — tap to select before pressing a direction.
- **X−/X+, Y−/Y+, Z−/Z+** — move the toolhead by the selected step. On the KE (bed-slinger), Y moves
  the bed.
- Current XYZ position shown at the top.

> ⚠️ The printer doesn't know where it is until it homes. Moving before homing can crash the toolhead.
> Use the Home buttons below first.

### Home buttons

| Button | What it does |
|---|---|
| **Home All** | G28 — homes X, Y, and Z |
| **Home Z** | Homes Z only |
| **Home X** | Homes X only |
| **Home Y** | Homes Y only |

### Motors off

Disables stepper motors so you can move the toolhead and bed by hand. They re-engage on the next move
command or home.

### Invert Y toggle

Flips which direction Y+ and Y− move the bed. Some users find the default unintuitive on a bed-slinger.
Toggle to match your mental model. (Also settable permanently in Settings → System → Invert Y Direction.)

---

## Filament panel (extruder)

Opens from the Home tab **Extrude** button.

### Load

Heats the nozzle to the last-used target, feeds filament forward until you tap **Stop**. If Spoolman is
active, a **"Use this filament?"** confirmation appears first. After loading, the nozzle auto-cools.

> Start Load when your filament is already inserted into the extruder throat. Stop when plastic coming
> out the nozzle runs clean and consistent.

### Unload

Heats the nozzle, retracts filament fully out of the bowden/throat, then auto-cools. Remove the
filament from the top of the extruder manually after.

### Manual extrude / retract

For precise manual movement once the nozzle is already hot:

- **Distance** — how far to move (mm)
- **Speed** — feedrate (mm/s)
- **Extrude / Retract** buttons — each press moves that amount once

The nozzle must be at temperature (Klipper enforces this). Also auto-cools after each operation.

---

## Fan panel

Opens from the Home tab **Fans** button.

Each configured fan appears as a row:

- **Part cooling fan** — slider 0–100%. The fan that blows on the print. 0% for first layer of
  ABS/ASA; 100% for PLA/PETG bridges.
- **Other fans** (auxiliary, case fan, etc.) — shown if configured in your Klipper config. Appear as
  sliders (variable-speed) or toggles (on/off for simple fans).

Fan speeds set here apply immediately and override whatever gcode has been sending.

---

## LED panel

Opens from the Home tab **LED** button.

Each configured LED/light appears as a row with a slider (brightness) or toggle (on/off). The KE case
light is a simple toggle.

---

## File browser (Print)

Opens from the Home tab **Print** button.

- Lists `.gcode` files from internal storage and any plugged-in USB stick.
- **Tap a file** to preview it: thumbnail (if the slicer generated one), print time estimate, file size.
- **Tap Print** to start.
- Files sort by date by default; the sort order is remembered between opens.
- USB files load full metadata (print time, filament weight) via a metascan on first access.

---

## Print status screen

Appears automatically when a print is running. Also reachable by tapping the compact status bar at the
top of any screen during a print.

| Element | What it shows |
|---|---|
| File name + thumbnail | The file being printed |
| Progress bar | Percentage complete |
| Time remaining / elapsed | Running estimates |
| Layer X / Y | Current layer and total |
| Z offset (live) | Tap to baby-step — see below |
| Speed % | Speed relative to slicer settings |
| Flow % | Extrusion multiplier |
| Pressure Advance | PA value in use |
| Filament used | How much has extruded so far |

### Baby-stepping Z-offset while printing

Tap the **Z-offset readout** to get step buttons (±0.001, ±0.005, ±0.01, ±0.05 mm). Nudge while
watching the first layer:

- **Lines not fusing / gaps:** too high — lower (−)
- **Lines translucent or completely flat:** too low — raise (+)
- **Squished together and fused, no gaps:** correct

Saves automatically.

### Control buttons

| Button | What it does |
|---|---|
| **Pause** | Parks the toolhead, holds temperatures. **Resume** brings it back and continues. |
| **Cancel** | Cancels the print. Shows a red Confirm dialog. For the very last remaining object (Exclude Object), shows a "Cancel print?" confirmation. |
| **Emergency Stop** | Cuts all movement and heaters immediately. Use only if something is going wrong physically. Requires a power-cycle or `FIRMWARE_RESTART` to recover. |

---

## Macros tab

All your Klipper gcode macros, accessible from the screen. Inline content — no launcher grid.

- **Favourites view** (default) — macros you've pinned. Tap the ★ icon on any macro to pin/unpin. Tapping
  the Macros tab when already on it jumps back here.
- **All macros** — full scrollable list, grouped by category (the `[gcode_macro]` section names in your
  config). Tap a category header to expand it.
- **Parameters** — tap a macro to expand its parameter inputs. Fill them in before running, or leave
  defaults as-is.

---

## Console tab

Direct gcode access. Inline content — no launcher grid.

### Command browser

Drill-down interface: tap a **category** → see commands in it → tap a **command** → see its description
and parameters → tap **Run**. Good for exploring what's available without a cheat-sheet. Tapping the
Console tab when already on it jumps back to the top.

### Direct input

Type any gcode command directly, same as the Mainsail terminal.

- **History** — last 100 entries. Temperature-spam lines (`T:210 B:60 …`) are filtered so they don't
  flood the history. Tap a history entry to re-run it.

---

## Tune tab

A **4-column × 3-row grid of icon buttons**. Tapping a button opens its sub-panel full-screen. Nothing
is adjusted inline on this tab.

| Row | Col 1 | Col 2 | Col 3 | Col 4 |
|---|---|---|---|---|
| 1 | Fine Tune | Z Offset | Retraction | Limits |
| 2 | Bed Mesh | Input Shaper | Axis Twist | Skew |
| 3 | Belts/Shake | Power Settings | TMC Autotune | TMC Metrics |

### Fine Tune

Live sliders — usable during a print or any time:

| Slider | What it does | Typical range |
|---|---|---|
| **Speed %** | Global speed multiplier. 100% = follow slicer speeds exactly. | 50–150% |
| **Flow %** | Extrusion multiplier. Under-extruding → raise. Over-extruding → lower. | 90–110% |
| **Pressure Advance** | Compensates for nozzle pressure lag at speed. Fixes corner bulges and blobs. | 0–0.08 |
| **Firmware Retraction** | Retraction length and speed (only if your slicer uses `G10`/`G11`). | Filament-dependent |

### Z Offset

First-layer baby-stepping. Same as tapping Z on the print-status screen, but also usable outside a print.

- Step size buttons: **0.001 / 0.005 / 0.01 / 0.05 mm**
- Current value shown at top
- **Save** — persists the value to the config (with Save Z-Offset installed; added by the installer)

### Retraction

Only relevant if your slicer uses **firmware retraction** (`G10`/`G11`). Controls the retraction
distance and speed the firmware executes. Most OrcaSlicer setups use regular gcode retraction — if so,
these sliders have no effect.

### Limits

Hard caps on machine speed and acceleration:

| Setting | What it is |
|---|---|
| **Max velocity** | Hard cap on axis speed (mm/s) |
| **Max acceleration** | Hard cap on acceleration (mm/s²) |
| **Max print acceleration** | Acceleration cap while extruding |
| **Square corner velocity** | How fast the toolhead turns a sharp corner without fully decelerating |

Raising these doesn't automatically make prints faster — the slicer's own speeds must also be raised.
Going beyond what the machine can handle causes ringing artifacts.

### Bed Mesh

The bed's height map.

- **3D view** — colour surface you can rotate (drag) and zoom. Peaks = high spots, valleys = low spots.
- **Table view** — the raw Z values at each probe point.
- **Re-mesh** — runs `BED_MESH_CALIBRATE`, maps the whole bed fresh (~2–3 min).
- **Enable/disable** — toggles whether mesh compensation is applied. On by default.

### Input Shaper

Reduces ghosting/ringing (echoes trailing sharp corners at speed).

1. Select **X** or **Y** axis, tap **Run test** — the printer sweeps frequencies (~1 min).
2. A **frequency-response graph** is shown; peaks = resonant frequencies.
3. The panel recommends a shaper type and frequency — tap **Apply**.
4. Repeat for the other axis.

> 💡 For the **Y axis** test: move the accelerometer to the **bed** (it must be on whatever moves for
> that axis). Tape or zip-tie it on for the 1-minute test.

### Axis Twist

Launches the 5-point calibration wizard for Axis Twist Compensation. Fixes first layers that are
uneven left-to-right despite re-meshing. Full guide: [Axis Twist Compensation](Axis-Twist-Compensation).

### Skew

Corrects parts that come out as slight parallelograms instead of squares.

- Enter three caliper measurements from a printed test square: **AC diagonal**, **BD diagonal**, **AD side**.
- Tap **Apply**, then run `SAVE_CONFIG` in the Console to persist.

Full guide: [Skew Correction](Skew-Correction).

### Belts/Shake

Excites each axis at a chosen frequency to check mechanical resonance. Primary use: comparing left vs.
right belt tension on the X axis. Matched tension = cleaner prints. If one belt sounds different when
plucked, tighten the looser one.

### Power Settings

| Section | What it does |
|---|---|
| **Power devices** | On/off buttons for any smart plugs or relays configured in Moonraker |
| **Power Loss Recovery** | Resume a print that was interrupted by a power cut (print file must still be on the printer) |

### TMC Autotune

Quieter, cooler, sometimes smoother steppers. Select motor type and a goal, tap Apply:

- **Performance** — prioritises current/torque (slightly louder)
- **Silent** — prioritises quiet operation

Settings are saved and reapplied every boot.

> The button is **greyed out** until the TMC Autotune module is installed (the installer does this
> during the print-quality mods step). Full guide: [TMC Autotune](TMC-Autotune).

### TMC Metrics

Live diagnostics for each stepper driver — current draw, driver temperature, internal flags. Not needed
for normal printing; use it if you suspect a driver is overheating or skipping steps.

---

## Settings tab

A **4-column × 2-row grid of icon buttons** plus a quick-action restart row at the top. Nothing is
configured inline on this tab.

### Quick-action row (top row)

These execute immediately — no sub-panel opens:

| Button | What it does |
|---|---|
| **Restart Klipper** | Restarts the Klipper host process. Use after editing `printer.cfg`. |
| **Restart Firmware** | Resets the mainboard MCU (`FIRMWARE_RESTART`). Use after MCU-level config changes. |
| **Restart Guppy** | Restarts the OpenKE screen process only (not Klipper). Use if the UI feels stuck. |
| **Update Guppy** | Downloads and installs the latest OpenKE screen binary. **Note:** only swaps the binary — for full updates (Klipper mods, KAMP config, etc.) re-run the full installer. |

### Navigation row (main row)

| Button | Opens |
|---|---|
| **WiFi** | WiFi connection panel |
| **Printers** | Printer connection manager |
| **Spoolman** | Filament tracking panel |
| **System** | Screen settings + info panel |

---

## WiFi panel

Opens from Settings → **WiFi**.

| Control | What it does |
|---|---|
| **Network list** | Scans and shows available networks. Tap one to connect (prompts for password). Rescans automatically on open; tap the scan button to refresh. |
| **IP address** | Your printer's current local IP — shown once layer-3 is confirmed. Handy for accessing Mainsail from a new device. |
| **Password entry** | Has an **eye-toggle** button to reveal/hide what you're typing. |
| **Low Latency** | Disables WiFi power-save, idle sleep, background roam scans, and **Bluetooth**. The KE's WiFi and Bluetooth share one 2.4 GHz radio and antenna — leaving BT on (it's unused) makes WiFi yield to it periodically and stutter. Low Latency eliminates that. Persists across reboots. Turn off to re-enable Bluetooth. |

> If Mainsail feels laggy, the camera stutters, or tap response feels slow — enable **Low Latency** first.

---

## Printers panel

Opens from Settings → **Printers**. Manages Moonraker connections.

Each configured printer is shown as a card with its name, IP, and port, plus:

- **Switch** — reconnects OpenKE to that printer
- **Remove** — removes it from the config (asks for confirmation)

**Add a printer:** fill in Name, IP/hostname, and port (default 7125), then tap the add button. A
keyboard appears for text entry.

---

## Spoolman panel

Opens from Settings → **Spoolman**. Requires a [Spoolman](https://github.com/Donkie/Spoolman) server
on your network — the button is greyed out until one is configured.

| Feature | What it does |
|---|---|
| **Spool list** | All spools — name, material, colour, remaining weight and length |
| **Set active** | Tap a spool → Set Active. The active spool is deducted as you print. |
| **Archive** | Mark an empty spool archived to keep the list tidy. Toggle to show/hide archived spools. |
| **Auto tracking** | Once a spool is active, filament used by prints is subtracted automatically. No weighing. |
| **Wrong-filament check** | Before a print starts (and before a manual Load), a **"Use this filament?"** popup shows the active spool. Prevents "oops, wrong material" before it ruins a print. |

---

## System panel

Opens from Settings → **System**. This is the main settings panel for the screen itself — everything
that changes how OpenKE behaves.

**Network info label** (top of right column): shows each network interface and its IP address, plus the
currently running OpenKE version. Read-only.

### Left column

| Setting | Options | What it does |
|---|---|---|
| **Display Sleep** | Never / 30s / 1m / 5m / 10m / 30m / 1h | Dims the screen after this period of inactivity. Touch to wake. |
| **Brightness** | Low (10%) / Dim (25%) / Medium (50%) / Bright (75%) / Max (100%) | Backlight brightness. 10% is the minimum readable level. |
| **Log Level** | trace / debug / info / warn | Verbosity of `guppyscreen.log`. **info** is the default (weeks-long logs). Use **debug** before reporting a bug, then switch back. **warn** = errors only (smallest logs). **trace** = developer use only. |
| **Prompt Emergency Stop** | Toggle | **On:** tapping Emergency Stop shows a confirmation dialog first. **Off:** acts immediately, no dialog. Default on. |
| **Invert Z Direction** | Toggle | Flips Z jog buttons in the movement panel (up/down). |
| **Invert Y Direction** | Toggle | Flips Y jog buttons (front/back). Useful if the bed moves opposite to your expectation on a bed-slinger. |

### Right column

| Setting | Options | What it does |
|---|---|---|
| **Theme Color** | Material, Blue, others | Changes the screen colour scheme. Takes effect immediately. |
| **Def. Temp** | Preset temperatures | Default extruder target used by Load/Unload when no explicit target has been set (e.g. on a cold boot). |
| **Touch Beep** | Toggle | Plays a short click sound through the buzzer on every tap. A test beep plays immediately when you enable it. |

### Reset Options

Tap the **Reset Options** button (top-right corner of the System panel) to open a dialog with three choices:

| Option | What it does |
|---|---|
| **Reset GuppyScreen settings** | Deletes the screen config and sensor layout (`guppyconfig.json`). OpenKE restarts with factory defaults. Your Klipper config and print files are **not** touched. |
| **Factory Reset Printer** | Wipes OpenKE, all Klipper config, gcodes, and calibration. Reboots to stock Creality firmware. **Irreversible.** WiFi and Creality cloud account are preserved. See [Resetting & Uninstalling](Resetting-and-Uninstalling). |
| **Reset Touch Calibration** | Clears saved calibration. OpenKE restarts and shows the 9-tap calibration wizard immediately. |

Tap **Close** to dismiss without doing anything.

**Back** button (bottom-right of the System panel) returns to the Settings tab.
