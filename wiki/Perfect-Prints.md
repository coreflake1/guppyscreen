# Perfect prints — start here

The Ender-3 V3 KE is a good printer out of the box. With an afternoon of calibration it's a *great*
one. This page is the short, opinionated path from "it prints" to "it prints well" — in the order that
actually matters. You don't need to do all of it at once; do it top-to-bottom and stop when your prints
look the way you want.

> **Golden rule:** change **one thing at a time** and print a test after each. If you tweak five things
> and the next print is worse, you won't know which one did it.

## The order that matters

Calibrate from the **machine outward** — mechanical first, then the first layer, then motion quality,
then the slicer. Doing it out of order means redoing work.

### 1. Mechanical basics (do this first, no excuses)
Belts tight, frame square, nozzle clean, bed not warped to hell. Software can't fix a loose belt or a
wobbly gantry — it can only paper over it. Five minutes here saves hours later.

### 2. Z-offset + bed mesh — the first layer
This is 80% of whether a print "looks good." Get the nozzle-to-bed distance right, then let the printer
map the bed so the first layer is even *everywhere*, not just in the middle.

- **Live Z-offset baby-stepping** is built into the screen (Tune tab, or tap the Z-offset while
  printing) — nudge in steps down to 0.001 mm while watching the first layer go down.
- **Bed mesh** maps the bed's hills and valleys so Z compensates across the whole plate.
- **[KAMP (adaptive meshing + purge)](KAMP-and-Axis-Twist-Compensation)** only meshes the area your
  print actually uses, and lays a smart purge line right before it — faster, and a clean nozzle for the
  first move.

→ Full walkthrough: **[Perfect first layer (Axis Twist + KAMP)](KAMP-and-Axis-Twist-Compensation)**

### 3. Axis Twist Compensation — when mesh isn't enough
Classic KE symptom: first layer is perfect in the middle but **squished on one side and lifting on the
other**, left-to-right, and no amount of bed mesh fixes it. That's the X gantry being very slightly
twisted, which tilts the probe. The on-screen **Axis Twist wizard** measures and corrects it.

→ Same page: **[Perfect first layer (Axis Twist + KAMP)](KAMP-and-Axis-Twist-Compensation)**

### 4. Input shaper — kill ringing/ghosting
Those faint echoes after sharp corners are vibration. Input shaping cancels it so you can print **fast
and clean**. The KE ships already shaped; you only need to redo it if you change the moving hardware
(see below). Run it from the screen's input-shaper tool.

### 5. Pressure advance + flow + temps — the slicer side
Corner bulges, gaps, over/under-extrusion — these are dialed in your slicer and with pressure advance.
This is the last 10%; don't start here.

### 6. Skew correction — for parts that must be *square*
If you print functional parts and they come out as slight parallelograms, **[Skew
Correction](Skew-Correction)** squares them up: print a test square, measure three lengths with calipers,
type them into **Tune → Skew**.

### 7. Quieter, cooler motors (optional, nice quality-of-life)
**[TMC Autotune](TMC-Autotune)** computes better stepper-driver settings from your motors' specs —
quieter, cooler, sometimes smoother. Doesn't change dimensions, just niceness.

## "Which one fixes my problem?"

| What you see | Most likely fix |
|---|---|
| First layer uneven middle-vs-edges | [Bed mesh / KAMP](KAMP-and-Axis-Twist-Compensation) |
| First layer squished one side, lifting the other (left↔right) | [Axis Twist Compensation](KAMP-and-Axis-Twist-Compensation) |
| Ghosting/echoes after corners | Input shaper (screen tool) |
| Bulging corners, blobs, gaps | Pressure advance + flow (slicer) |
| Parts not square / parallelogram | [Skew Correction](Skew-Correction) |
| Loud or hot steppers | [TMC Autotune](TMC-Autotune) |
| Layer shift right after a pause/resume | [Pause/resume fix](Pause-Park-Layer-Shift-Fix) |
| "I changed hardware — what do I redo?" | [Calibration, explained](Calibration-Explained) |

## Changed some hardware?

Don't blindly re-run everything. **[Calibration, explained](Calibration-Explained)** tells you exactly
which calibrations a given change invalidates, which are safe to leave, and whether you need to "reset"
anything (spoiler: almost never).
