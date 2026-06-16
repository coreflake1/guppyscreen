# Auto Z-offset (the load sensor) — and why we don't rely on it

Your KE can set its Z-offset **automatically**: the nozzle taps the bed, a sensor feels the contact, and
Klipper writes the offset for you — no paper, no eyeballing. It's built into the stock firmware and it
*works*, so you're welcome to use it. But we tested it carefully and found it **isn't repeatable enough**
to trust for a good first layer. This page explains what it is, shows our numbers, and tells you what we
do instead.

> **The short version:** auto Z-offset on the KE is convenient but **noisy** — it can land ~0.08 mm
> apart run to run, where a good first layer needs to be within ~0.01–0.02 mm. That's a **hardware**
> limit of the sensor Creality chose, not a screen or OpenKE problem. Use the **manual on-screen
> baby-step** instead (it's the reliable path, and it's why your KE also has a BLTouch).

---

## What "auto Z-offset" actually is

The KE has a **strain-gauge / ceramic pressure sensor** in the toolhead. When the nozzle pushes down on
the bed, that sensor detects the force and reports "contact." Creality's firmware turns that into a few
gcode commands — `Z_OFFSET_CALIBRATION`, `Z_OFFSET_AUTO` — that home, clean the nozzle, probe the bed
with the **nozzle tip itself**, compute the Z-offset, and **save it automatically**.

The idea is genuinely nice: the nozzle *is* the probe, so there's no probe-to-nozzle offset to worry
about, and you never touch a feeler gauge.

## Why we don't trust it — our test

We ran the no-frills calibration routine five times back to back on a real KE and recorded the offset it
computed each time (one run was lost to a midnight log rotation):

| Run | Computed Z-offset (mm) |
|---|---|
| 1 | 2.676 |
| 2 | 2.746 |
| 3 | 2.714 |
| 4 | 2.759 |

- **Spread (range): ~0.083 mm.** Standard deviation ~0.032 mm.
- A first layer that looks the same every time needs the offset to be stable to about **±0.01–0.02 mm**.
- So the auto routine is roughly **2–4× noisier than the tolerance that matters.**

Interestingly, *within* a single run the two probe points agreed nicely (~0.006–0.015 mm). It's the
**run-to-run** result that drifts ~0.08 mm — the classic signature of **strain-gauge preload and thermal
drift**. In plain terms: the sensor's "zero" wanders a little each time it heats, cleans, and presses,
so the number it lands on wanders with it.

> **Bottom line:** if you auto-set your Z-offset today and again next week, you can easily get a first
> layer that's visibly different — gappy one time, smeared the next — through no fault of your own.

## Why this is the sensor, not the screen

This matters, so it's worth being clear: **the unreliability comes from Creality's choice of sensor**,
not from OpenKE or the touchscreen.

- The probing logic lives entirely in **Creality's stock Klipper module** (a compiled `.so`). OpenKE
  doesn't run, wrap, or modify it — the screen has no way to make a strain gauge more repeatable.
- Strain/load-cell nozzle probing is **inherently drift-prone** unless it's built with careful ADC
  wiring and compensation. The KE's implementation is the convenient, low-cost version.
- The strongest evidence is on the printer itself: the KE also ships a **BLTouch** as its real Z probe.
  If the nozzle-load sensor were accurate enough on its own, that second probe wouldn't need to be there.
- One more wrinkle: the auto routine probes at around **140 °C**, not your printing temperature
  (~210–230 °C). The nozzle hasn't fully expanded yet, so even a *perfectly* repeatable reading would be
  a slightly different absolute offset than your true hot offset.

## A gotcha if you do run it

The auto routine **saves immediately** (Creality's own save step), with no Klipper restart. Two things
to know:

1. It **overwrites your saved Z-offset** without asking — there's no "test only" mode exposed.
2. If you've just done a **KAMP** (adaptive-mesh) print, that save can also persist the small temporary
   adaptive mesh **over your saved full bed mesh**, which throws off the first layer across the *whole*
   plate. If you ever run auto Z-offset, check your saved bed mesh afterwards (or restore a config
   backup).

## What to do instead — the reliable path

Set Z-offset with the screen's **live baby-stepping**, which is repeatable because *you* judge the
actual first layer:

1. Start a **first-layer test** (a single-layer square or patch).
2. On the **Tune** tab — or tap the Z-offset readout while it prints — nudge Z in **0.01–0.05 mm** steps.
3. Aim for lines that are **squished together and fused**: not gappy/round (too high), not translucent or
   smeared (too low).
4. With **Save Z-Offset** installed (an installer option), it persists automatically.

**Done when:** the first layer is uniform with no gaps between the lines — and it stays that way next
time, because the value didn't come from a drifting sensor.

Full first-layer walkthrough: [Calibrate step by step → Z-offset](Calibration-Explained#3-z-offset--first-layer-the-big-one).
For uneven left↔right first layers, that's a different fix —
[Axis Twist Compensation](KAMP-and-Axis-Twist-Compensation).

## "So should I ever use auto Z-offset?"

It's fine as a **rough starting point** — e.g. after a nozzle change, to get in the ballpark fast — as
long as you follow it with a baby-step pass on a real first-layer print and save *that*. Just don't treat
the number it prints as your final, trustworthy offset.
