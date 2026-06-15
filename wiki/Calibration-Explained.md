# Calibration, explained

Most calibration confusion comes down to two questions: **"do I need to redo this?"** and **"do I have
to reset it first?"** This page answers both, plus gives you a cheat-sheet for "I changed X — what now?"

## The one rule that saves you

**Redoing a calibration *overwrites* the old value. You almost never need to "reset" anything first.**

Klipper stores one value per thing (your Z-offset, your shaper frequencies, your mesh…). When you run
the calibration again and save, it replaces the old value in place. So "reset then recalibrate" is just
"recalibrate."

**Reset only matters in one situation:** you changed hardware you **can't currently re-measure**, and
the now-stale value is actively *harmful*. Then you reset it to a safe baseline instead of trusting a
stale number. That's the whole reason "reset" ever comes up.

## Two kinds of stale calibration

How much a stale (out-of-date) value hurts depends on which bucket it's in:

**🟡 Quality-only — stale is suboptimal, never dangerous**
- **Input shaper.** A shaper tuned to an outdated frequency just under-corrects; worst case some
  ringing comes back. It can't crash anything or ruin a print. Lowest-stakes to leave alone.

**🔴 First-layer / safety — stale can wreck a print or crash the nozzle**
- **Z-offset** (probe offset / saved baby-step)
- **Bed mesh**
- **Axis Twist Compensation**
- **Skew correction**
- **PID** (heater stability)

If something in the 🔴 group is stale and you *can* re-measure it, just redo it. If you *can't*
re-measure it right now, reset it to a safe default rather than print on a stale value.

## What each calibration does & when to redo it

| Calibration | What it does | Redo it when you change… | Bucket |
|---|---|---|---|
| **Z-offset** | Sets nozzle-to-bed distance for layer 1 | Nozzle, probe, hotend, bed surface, bed height | 🔴 |
| **Bed mesh** | Maps bed flatness so Z compensates across the plate | Bed, springs, surface, anything affecting flatness | 🔴 |
| **Axis Twist Comp** | Corrects a tilted X gantry that fools the probe left↔right | X gantry / anything tilting the X axis | 🔴 |
| **Skew correction** | Squares up parts (fixes parallelogram error) | Frame squareness, axis perpendicularity | 🔴 |
| **PID** | Stable hotend/bed temperature | Hotend, heater, or bed heater swap | 🔴 |
| **Input shaper** | Cancels vibration → faster, ringing-free prints | The *moving* hardware on an axis (mass/stiffness) | 🟡 |

> **How to reset (only if you must):** Z-offset/mesh/PID — just recalibrate (overwrites). Skew —
> `SET_SKEW CLEAR=1`. Bed mesh (clear active) — `BED_MESH_CLEAR`. Input shaper (disable one axis) —
> set `shaper_freq_x: 0` (or `_y`). You rarely need any of these.

## The accelerometer gotcha (input shaper)

On a bed-slinger like the KE, the input-shaper test needs the accelerometer **on whatever moves**:

- **X axis** → the **toolhead** moves, so the accelerometer goes on the **toolhead**.
- **Y axis** → the **bed** moves, so the accelerometer goes on the **bed**.

You don't need a fancy permanent bracket for a one-time test — people routinely **tape or zip-tie** the
accelerometer to the bed, run `TEST_RESONANCES AXIS=Y`, and move it back. It just has to survive ~1
minute of shaking.

And because X and Y are measured independently, **recalibrating one axis doesn't touch the other.**
Changed only the bed? You only need to redo **Y**; X stays valid.

## Worked example: "I installed linear rails on the bed"

This is a real one. Bed rails = a **Y-axis** change (the bed is what moves in Y). Here's the call:

1. **Z-offset → redo first.** Rails almost certainly changed the bed height → first-layer crash/gouge
   risk. No accelerometer needed. Do this carefully, before anything else. 🔴
2. **Bed mesh → redo.** New rails change flatness/tilt. No accelerometer needed. 🔴
3. **Y input shaper → recalibrate** (tape the accel to the bed). Rails changed the moving mass *and*
   stiffness, so Y resonance shifted. Quality-only, so not urgent — but worth it. 🟡
4. **X input shaper → leave it.** The toolhead didn't change; X is still valid.
5. **Skew / ATC → optional.** Only if you notice parts aren't square or the first layer is uneven
   left-to-right. Both are accelerometer-free and quick.
6. **PID → leave it** (unless you also swapped the bed heater).

And to be explicit: **no resets needed.** Everything affected is something you can re-measure and
overwrite. If you genuinely can't measure the Y shaper right now, leaving the old value is *safe* (it's
🟡) — just watch for ghosting on Y-facing walls and redo it when you can.

## TL;DR

- Recalibrating **overwrites** — no "reset" step needed.
- Only **reset** when you changed something you can't re-measure *and* a stale value would be harmful.
- 🔴 first-layer/safety calibrations matter most after a hardware change; 🟡 input shaper is low-stakes.
- Changed one axis? You usually only redo that axis.
- Not sure where to even start? → [Perfect prints — start here](Perfect-Prints).
