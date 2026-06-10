# Using GuppyKE — Your Screen, Explained

This page is a plain-English tour of what's on your printer's screen and what each tool actually does. No
jargon, no assumptions. If you've just installed GuppyKE and you're looking at the screen wondering "okay,
what does all this do?" — start here.

GuppyKE replaces the stock Creality interface with a faster, fuller one. Everything runs **on the printer
itself** — you don't need a phone or computer for day-to-day printing (though Mainsail in a browser still
works great for remote control).

---

## The basics: printing something

1. **Load a file.** Slice in your slicer (OrcaSlicer, Cura, …) and copy the `.gcode` to the printer — over
   the network (Mainsail/Fluidd), or on a **USB stick** plugged into the printer. New files show up in the
   **Files** list automatically.
2. **Start it.** Open the file and tap print. The **print status** screen shows temperatures, progress, time
   left, current layer, and a live readout of speed/flow/Z-offset.
3. **Adjust while it prints.** Almost everything on the status screen is tappable — tap a temperature to change
   it, tap the Z-offset to baby-step the first layer, etc. (More on that below.)

> 🔒 **You can't break a running print by poking around.** Anything that would disrupt a live job is either
> blocked or asks you to confirm first. Explore freely.

---

## Everyday controls

- **Temperatures** — tap the nozzle or bed reading to set a target, or use the presets. There's a chamber/MCU
  temperature shown too.
- **Move** — home the printer, jog X/Y/Z, disable the motors.
- **Fans & LED** — part-cooling and other fans get sliders (or on/off for simple ones); the case light
  toggles on/off.
- **Filament** — load/unload helpers heat up and feed/retract for you.
- **Extrude** — manual extrude/retract once the nozzle is hot.

---

## The Tune tab: calibration & quality tools

This is where GuppyKE shines. These are the tools that take a printer from "works" to "dialed in." You don't
need most of them often — but when you want them, they're right on the screen.

### First-layer & dimensional accuracy

| Tool | What it does | When to use it |
|---|---|---|
| **Bed Mesh** | Shows the bed's height map as an interactive 3D surface (rotate/zoom) or a table. | To *see* how flat your bed is, or after re-meshing. |
| **Z Offset** | Live first-layer baby-stepping — Raise/Lower in steps from **0.001** up to 0.05 mm. Saves automatically. | First layer too squished or not sticking. Nudge it mid-print and watch. |
| **Axis Twist** | A guided 5-point wizard that fixes a first layer that's good in the middle but uneven **left-to-right**. | The classic "one side squished, other side lifting" problem bed mesh can't fix. → [full guide](KAMP-and-Axis-Twist-Compensation.md) |
| **Skew** | Corrects parts that come out as a slight **parallelogram** instead of square. Print a frame, measure, type in 3 numbers. | Functional parts that need to be truly square. → [full guide](Skew-Correction.md) |

### Print quality & motion

| Tool | What it does | When to use it |
|---|---|---|
| **Fine Tune** | Live speed, flow, Z-offset, and pressure-advance while printing. | On-the-fly tweaks mid-print. |
| **Input Shaper** | Runs a resonance test (needs the accelerometer) and shows frequency-response graphs, then applies the result. | Reduce ghosting/ringing and unlock faster, cleaner prints. |
| **Belts / Shake** | Belt-tension check and axis excitation. | Comparing left/right belt tension, diagnosing a noisy axis. |
| **Retraction** | Live firmware-retraction tuning (length/speed). | Stringing or blobbing, if your slicer uses firmware retraction. |
| **Limits** | Max velocity, acceleration, square-corner velocity. | Push speed up, or calm the printer down. |

### Drivers & power

| Tool | What it does | When to use it |
|---|---|---|
| **TMC Autotune** | Automatically optimizes your stepper drivers (quieter, cooler, smoother) from the motor type. Pick the motor + a goal and it tunes every boot. | Set once and forget — but the button is greyed out until a small add-on is installed. See the [TMC Autotune guide](TMC-Autotune.md). |
| **TMC Metrics** | A live read-out of the stepper drivers — current, temperature, and internal settings. A diagnostic dashboard. | Chasing skipped steps or a hot driver. Otherwise ignorable. |
| **Power Settings** | Power-device toggles and Power-Loss-Recovery (resume a print after a power cut). | Recovering an interrupted print. |

> 💡 **You don't have to understand all of these to print well.** The two that fix the most common complaints
> are **Axis Twist** (uneven first layer) and **Z Offset** (first-layer height). Start there.

---

## Settings & system

- **Network** — connect to WiFi, see your IP, and a **low-latency toggle** that disables the radio's
  power-save for a snappier remote connection (handy if Mainsail feels laggy).
- **System info** — temperatures, load, versions.
- **Macros / Console** — run any gcode macro, or type commands directly (a drill-down command browser with
  history).
- **Spoolman** — filament spool tracking, if you run a Spoolman server.

---

## The big three setup guides

Most people only need to do these once. They're written step-by-step for non-programmers:

1. **[Perfect first layer (Axis Twist + KAMP)](KAMP-and-Axis-Twist-Compensation.md)** — fixes left-to-right
   first-layer unevenness. The single highest-impact tweak for the KE.
2. **[Skew correction](Skew-Correction.md)** — makes functional parts come out truly square.
3. **[TMC Autotune](TMC-Autotune.md)** — quieter, cooler motors (and how to enable the greyed-out button).

---

## A note on firmware updates

A few of these features rely on small additions to the printer's system files. **A Creality firmware update
can wipe those.** It's not a disaster — each guide has a quick reinstall section — but it's why you should
bookmark the setup guides and re-run them after any firmware update.
