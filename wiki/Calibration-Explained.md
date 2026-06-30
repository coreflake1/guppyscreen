# Calibrate your KE — start to finish

The full run, in the order that actually matters. Do it **top to bottom** and stop when your prints look
the way you want — you don't need every step.

> **Golden rule: change one thing, then print a test.** If you tweak five settings and the next print is
> worse, you won't know which one did it.

---

## The order that matters

Calibrate from the **machine outward** — mechanical first, then the first layer, then motion quality,
then slicer. Doing it out of order means redoing work.

| Step | Fixes |
|---|---|
| 0. Touch calibration | Tap targets are off on a fresh install |
| 1. Mechanics | Loose hardware that no software can fix |
| 2. Temperature (PID) | Inconsistent extrusion from wobbly heater temps |
| 3. Axis Twist | First layer uneven left-to-right |
| 4. Bed mesh + Z-offset | First layer height everywhere |
| 5. Input shaper | Ghosting/ringing on fast prints |
| 6. Pressure advance, flow, temp | Corner bulges, stringing, over/under-extrusion |
| 7. Skew correction | Parts that aren't square |
| 8. TMC Autotune | Loud or hot motors (optional) |

**"Which one fixes my problem?"**

| What you see | Most likely fix |
|---|---|
| Tap targets off / need to press slightly away from what you mean | Step 0 — touch calibration |
| First layer uneven left↔right, bed mesh didn't help | Step 3 — Axis Twist |
| First layer uneven all over / patchy adhesion | Step 4 — Bed mesh |
| First layer too high or too low everywhere | Step 4 — Z-offset |
| Ghosting/echoes after sharp corners | Step 5 — Input shaper |
| Bulging corners, blobs, gaps | Step 6 — Pressure advance + flow |
| Parts not square / parallelogram | Step 7 — Skew correction |
| Loud or hot motors | Step 8 — TMC Autotune |
| Layer shift right after pause/resume | See [Troubleshooting](Troubleshooting) |

---

## Step 0 — Touch calibration (do this first on a fresh install)

A fresh install has no touch calibration data, so taps are uncompensated raw coordinates. Run the
9-tap wizard once and the touchscreen becomes accurate.

**Screen:** Settings → System Info → **Reset Touch Calibration**

The screen shows three crosshairs in turn — tap the centre of each **three times** (lift your finger
between taps; the screen averages the three for accuracy). Takes about 30 seconds. The calibration is
saved automatically and survives reboots.

> Redo this any time tap targets feel noticeably off, or if you change `display_rotate` — the
> calibration coefficients are rotation-specific.

---

## Step 1 — Mechanics first (no software)

Software can't fix loose hardware — it can only paper over it.

- **Belts:** firm. A plucked belt should "thunk", not flap. Slack belts cause layer shift and ringing.
- **Gantry:** square. The X bar should be parallel to the bed, not tilted.
- **Bed:** not rocking. Gently push each corner — no play.
- **V-wheels/rails:** snug but not binding. Should spin with light finger pressure.
- **Nozzle:** clean, no plastic blob.

**Done when:** nothing wobbles when you nudge it. Five minutes here saves hours later.

---

## Step 2 — Stable temperatures (PID)

Wobbling hotend or bed temperature = inconsistent extrusion. If your temp graph swings more than
~±1 °C at target, PID-tune. In the **Klipper console** (Mainsail or the Console tab on screen):

```
PID_CALIBRATE HEATER=extruder TARGET=230
PID_CALIBRATE HEATER=heater_bed TARGET=60
SAVE_CONFIG
```

If you installed the Creality macros, `PID_HOTEND` / `PID_BED` do the same.

**Done when:** the temperature holds flat at target on the graph (< ±1 °C).

---

## Step 3 — Axis Twist Compensation

Skip this if your first layer is consistently even from left to right. Do it if you see the classic
KE symptom: first layer is fine in the middle but squished on one side and lifting on the other — and
bed mesh hasn't fixed it.

**Full guide with instructions:** [Axis Twist Compensation](Axis-Twist-Compensation)

In short: **Tune → Axis Twist** on the screen runs a 5-point guided paper test across the X axis. The
result corrects the probe readings so the bed mesh is accurate. Takes ~15 minutes, done once.

---

## Step 4 — Bed mesh + Z-offset (the big one)

This is 80% of whether prints "look good." Do both.

### 4a. Bed mesh

Maps the bed's hills and valleys so Z follows them everywhere, not just the centre.

From the console:
```
BED_MESH_CALIBRATE
SAVE_CONFIG
```

Or tap **Tune → Bed Mesh → Re-mesh** on the screen. Takes 2–3 minutes.

If you installed KAMP (the installer default), use `ADAPTIVE_BED_MESH_CALIBRATE` in your slicer start
G-code instead — it re-meshes just the print footprint automatically at the start of each print. See
[Adaptive Meshing (KAMP)](Adaptive-Meshing-KAMP).

**Done when:** the mesh surface (3D or table view) is smooth with no wild spikes.

### 4b. Z-offset — the first layer

Set the nozzle-to-bed gap by watching the first layer go down:

1. Start a **first-layer test** (a single-layer square, available in many slicer model libraries).
2. Tap the **Z-offset readout** on the print-status screen (or **Tune → Z Offset**) and nudge in
   0.01–0.05 mm steps while watching:
   - **Lines not fusing / gaps:** too high — lower (negative)
   - **Lines translucent or flat:** too low — raise (positive)
   - **Squished together and fused, no gaps:** correct
3. The value saves automatically.

> **Why not use the strain-sensor auto Z-offset?** The KE *can* auto-set Z-offset with its nozzle
> load sensor, but it drifts run-to-run by up to ~0.08 mm (tested: range 83 µm, stddev 32 µm), where a
> consistent first layer needs stability to ±0.01–0.02 mm. That's a hardware limitation of Creality's
> sensor choice. The BLTouch you also have is there for exactly this reason — accurate probing. Use the
> **manual baby-step** instead. It's the reliable path.
>
> One extra caveat: the auto routine saves immediately and can overwrite your saved bed mesh with a
> temporary adaptive one — always check your saved mesh if you ever do run it.

**Done when:** the first layer is uniform with no gaps, and it stays that way print after print.

---

## Step 5 — Input shaper (kill ringing/ghosting)

Those faint echoes trailing sharp corners are vibration. Input shaping cancels them.

**On the screen:** Tune → Input Shaper

1. Select **X**, tap **Run test** — the printer shakes at a frequency sweep (~1 minute).
2. The graph shows resonant peaks; the panel recommends a shaper and frequency.
3. Tap **Apply**.
4. Repeat for **Y** (move the accelerometer to the **bed** for the Y measurement — the sensor must
   be on whatever moves for that axis).

**Done when:** a fast corner-test print shows no echoes. If it does, try a different shaper type.

---

## Step 6 — Pressure advance, flow & temperature (slicer side)

The last 10%. These fix corner bulges, blobs, gaps, stringing, and over/under-extrusion — but only
after the first layer and motion quality are solid. Tuning these first wastes time.

- **Flow:** start with 0.98 (a modest under-extrusion offset that reduces over-fill) and adjust from
  your first real print.
- **Pressure advance:** print a PA tower or line, find the cleanest corners, dial it in. The on-screen
  **Tune → Fine Tune** lets you adjust PA live while printing.
- **Temperature:** print a temperature tower from your slicer's test model library and pick the layer
  that looks cleanest.

---

## Step 7 — Skew correction (only if parts aren't square)

If functional parts come out as slight parallelograms, print a calibration square, measure three lengths
with calipers, and enter them on the screen: **Tune → Skew**. Full guide: [Skew Correction](Skew-Correction).

**Done when:** a measured test square matches the expected dimensions on both diagonals.

---

## Step 8 — TMC Autotune (optional)

Quieter, cooler stepper motors. **Tune → TMC Autotune** — select your motor type, choose Performance or
Silent, tap Apply. It tunes the stepper drivers and reapplies the settings every boot. Doesn't affect
print quality or dimensions, just reduces noise and heat.

See [TMC Autotune](TMC-Autotune) for the full guide and how to enable the greyed-out button.

---

## After a hardware change

You don't have to redo everything — just the calibrations that the changed part affects.

**The rule:** re-running a calibration overwrites the old value, no "reset first" step. The only time
*reset* matters is if you changed something you can't re-measure right now and the stale value would
be harmful — then set it back to a safe default.

| You changed… | Redo |
|---|---|
| Nozzle / hotend | Z-offset (+ PID if you swapped the heater too) |
| Bed surface, springs, or anything affecting bed height | Z-offset, bed mesh |
| X gantry / anything that could tilt the X axis | Axis Twist, then re-check first layer |
| Moving mass on an axis (belts, carriage, bed rails) | Input shaper for **that axis only** |
| Frame squareness | Skew |
| A heater element | PID |

**Reset commands (rarely needed):** skew → `SET_SKEW CLEAR=1`; active mesh → `BED_MESH_CLEAR`;
disable a shaper axis → set `shaper_freq_x: 0` (or `_y`) in config. For everything else, just
recalibrate — it overwrites.
