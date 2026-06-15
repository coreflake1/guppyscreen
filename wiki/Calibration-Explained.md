# Calibrate your KE — start to finish

This is the full run, in order, from a fresh printer to clean prints. Do it **top to bottom** and stop
when your prints look the way you want — you don't always need every step. Each one says *what it fixes*,
*how to run it* (mostly from the screen's **Tune** tab), and *how to know it worked*.

> **Golden rule: change one thing, then print a test.** If you tweak five things and the next print is
> worse, you won't know which one did it.

> **You won't "reset" anything.** Re-running a calibration just overwrites the old value — there's no
> separate reset step. (More on that in [After a hardware change](#after-a-hardware-change) at the end.)

---

## 0. Mechanics first (no software)

Software can't fix loose hardware — it can only paper over it.

- Belts firm (a plucked belt should "thunk," not flap), no slack in the gantry, bed not rocking.
- Nozzle clean (no plastic blob), bed clean (IPA), V-wheels/rails snug but not binding.

**Done when:** nothing wobbles when you nudge it. Five minutes here saves hours later.

## 1. Stable temperatures (PID)

Wobbling hotend/bed temperature = inconsistent extrusion. If your temp graph swings more than ~±1 °C at
target, PID-tune. In the **Klipper console**:

```
PID_CALIBRATE HEATER=extruder TARGET=230
PID_CALIBRATE HEATER=heater_bed TARGET=60
SAVE_CONFIG
```

(If you installed the Creality macros, `PID_HOTEND` / `PID_BED` do the same.) **Done when:** the temp
holds flat at target on the screen's graph.

## 2. Bed mesh (even first layer across the whole plate)

The bed is never perfectly flat. A mesh maps its hills and valleys so Z follows them. From the console:

```
BED_MESH_CALIBRATE
SAVE_CONFIG
```

(Your `G29` macro does this too. If you installed **KAMP**, it meshes just the print area automatically at
the start of each print — see [Perfect first layer](KAMP-and-Axis-Twist-Compensation).) **Done when:** the
mesh on the screen (3D or table view) looks like a smooth surface with no wild spikes — typically within a
few tenths of a millimetre corner to corner.

## 3. Z-offset / first layer (the big one)

This is most of what makes a print "look good." Set the nozzle-to-bed gap with the screen's **live
Z-offset baby-stepping** (Tune tab, or tap the Z-offset readout while a first-layer test prints):

1. Start a first-layer test print (a single-layer square or patch).
2. Nudge Z down/up in **0.01–0.05 mm** steps while watching the line go down.
3. Aim for lines that are **squished together and fused** — not gappy/round (too high), not translucent
   or smeared (too low).

It saves automatically (with Save Z-Offset installed). **Done when:** the first layer is uniform and the
lines have no gaps between them.

## 4. Axis Twist Compensation (only if the first layer is uneven left↔right)

Classic symptom: first layer is perfect in the **middle** but squished on one side and lifting on the
other, and bed mesh didn't fix it. That's a slightly twisted X gantry tilting the probe. Run the on-screen
wizard: **Tune → Axis Twist** (a guided 5-point paper test). Full walkthrough:
[Perfect first layer](KAMP-and-Axis-Twist-Compensation). **Done when:** the first layer is even edge to
edge, not just in the centre.

## 5. Input shaper (kill ringing / ghosting)

Those faint echoes after sharp corners are vibration. Input shaping cancels it so you can print **fast and
clean**. The KE has an onboard accelerometer, so you measure and apply it from the screen's **Input
Shaper** tool — it runs the resonance test, shows the graph, and recommends a shaper for each axis.

> **Accelerometer placement (bed-slinger):** the sensor must be on **whatever moves** for that axis — the
> **toolhead** for the **X** test, the **bed** for the **Y** test. You don't need a permanent bracket;
> taping or zip-tying it on for the ~1-minute test is fine. X and Y are measured independently.

**Done when:** a fast ringing-test print shows no echoes trailing the corners.

## 6. Pressure advance, flow & temperature (slicer side)

The last 10%: corner bulges, blobs, gaps, over/under-extrusion. These are tuned in your **slicer** (flow,
temperature) and with **pressure advance**. Don't start here — it only pays off once the first layer and
motion are solid.

## 7. Skew correction (only if parts aren't square)

If functional parts come out as slight parallelograms, square them up: **Tune → Skew** — print a
calibration square, measure three lengths with calipers, type them in. Full guide:
[Skew Correction](Skew-Correction). **Done when:** a measured test square is square on both diagonals.

## 8. Quieter motors (optional)

**Tune → TMC Autotune** computes better stepper-driver settings from your motor type — quieter, cooler,
sometimes smoother. It doesn't change dimensions, so it's pure quality-of-life. See
[TMC Autotune](TMC-Autotune).

---

## After a hardware change

You don't have to redo everything when you change a part — just the calibrations that part affects.

**The rule:** redoing a calibration **overwrites** the old value, so there's no "reset first" step. The
only time *reset* matters is if you changed something you **can't re-measure right now** and the stale
value would be harmful — then set it back to a safe default instead of trusting it.

**How stale values hurt — two buckets:**

- 🔴 **First-layer / safety** (Z-offset, bed mesh, Axis Twist, skew, PID): a stale value can wreck a print
  or crash the nozzle. Redo these promptly after a relevant change.
- 🟡 **Input shaper:** a stale shaper just under-corrects — worst case some ringing returns. It can't
  damage anything, so it's the lowest-stakes one to leave for later.

**What to redo when you change…**

| You changed… | Redo |
|---|---|
| Nozzle / hotend | Z-offset (+ PID if you swapped the heater) |
| Bed surface, springs, or anything affecting bed height/flatness | Z-offset, bed mesh |
| X gantry / anything that could tilt the X axis | Axis Twist, then re-check the first layer |
| The *moving* hardware on an axis (belts, carriage, bed rails — changes mass/stiffness) | Input shaper for **that axis only** (the other axis stays valid) |
| Frame squareness | Skew |
| A heater | PID |

**Reset commands (rarely needed):** skew → `SET_SKEW CLEAR=1`; active mesh → `BED_MESH_CLEAR`; disable a
shaper axis → `shaper_freq_x: 0` (or `_y`). For everything else, just recalibrate — it overwrites.

Not sure where your problem comes from? → [Perfect prints — start here](Perfect-Prints) has a
symptom-to-fix table.
