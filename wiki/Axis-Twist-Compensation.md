# Axis Twist Compensation

This guide fixes the single most common Ender-3 V3 KE first-layer complaint: **one side squished, the other side lifting — and bed mesh doesn't help**. If you've re-levelled, re-meshed, and tweaked Z-offset and it *still* looks wrong from left to right, you're in the right place.

---

## Will this help me?

**Yes, if:** your first layer is inconsistent left-to-right — one side too low, the other too high — even though the bed is clean and not visibly warped, and bed mesh hasn't solved it.

**Probably not the whole story, if:** the bed physically rocks (gently push each corner — any play?), or one specific spot is dramatically high/low. Fix loose hardware first, then come back.

### Why this happens

Your printer probes the bed before each print to build a height map (a *bed mesh*) and follows it with Z corrections. The probe sits a couple of centimetres **behind** the nozzle. Here's the catch: the X-axis bar twists by a tiny fraction of a degree as it moves left to right. That twist tilts the probe just enough that the height it measures **isn't** what the nozzle will actually print at. The bed mesh faithfully records this wrong number — so no amount of re-meshing fixes it.

**Axis Twist Compensation** measures that error at five points across the bed and corrects it, so the probe finally tells the truth.

---

## Before you start

- ⏱️ **Time:** about 15–20 minutes.
- 🧰 **You need:** the printer on your network and a sheet of paper. No tools, no disassembly.
- ↩️ **Safe to try:** fully reversible — see [Undoing it](#undoing-it).

> ⚠️ These changes live in the printer's system files. **A Creality firmware update will erase them.** Re-run the OpenKE installer afterwards and they come back — bookmark this page.

---

## Step 1 — Make sure the mod is installed

The OpenKE installer sets this up for you during the print-quality mods step. To check, open the
**Tune** tab on the screen — if you see an **Axis Twist** entry, you're good. Jump to Step 2.

If you don't see it, re-run the [installer](Installation) and answer **Y** at the print-quality mods
prompt. It's safe to re-run on an existing install.

---

## Step 2 — Calibrate (the 5-point paper test)

You only need to do this once per printer lifetime, unless you change the X gantry hardware.

### Option A — from the screen (easiest)

Open **Tune → Axis Twist** on the printer screen. The wizard walks you through the 5 points on the
screen itself — no browser needed.

### Option B — from the Klipper console (Mainsail/Fluidd)

Make sure the nozzle tip is **clean** (no plastic blob — wipe it on a paper towel at temperature, then
let it cool). Then in the console:

```
BED_MESH_CLEAR
AXIS_TWIST_COMPENSATION_CALIBRATE SAMPLE_COUNT=5
```

The printer probes, then moves the nozzle to the first of **5 spots** across the bed. A small
**"Manual Probe"** box appears. At each spot:

1. The nozzle starts about 5 mm up. Lower it using the step buttons (or `TESTZ Z=-1`, then smaller
   increments).
2. Slide a sheet of paper under the nozzle and keep lowering in small steps (`-0.1`, `-0.05`, `-0.01`)
   until you feel **light drag** — the same feel as setting Z-offset.
3. Tap **ACCEPT**. The nozzle lifts and moves to the next spot.

> 🎯 **The one trick that matters:** use the *same* paper drag feel at all 5 points. The tool only
> cares about the *difference* between spots, so consistency is everything.

After the 5th point, save permanently:

```
SAVE_CONFIG
```

Klipper restarts. Run a first-layer test that spans the full width — the left-right unevenness should be gone.

> **Small note:** if the overall first-layer height feels slightly different after calibration, just nudge
> your Z-offset as usual. Twist is fixed; this is just fine-tuning.

---

## Undoing it

The easiest path is the [uninstaller](Resetting-and-Uninstalling) — it removes the module and config
and tells you how to restore `probe.py`. By hand:

```sh
cp -a /usr/data/guppyify-backup/probe.py.bak /usr/share/klipper/klippy/extras/probe.py
rm /usr/share/klipper/klippy/extras/axis_twist_compensation.py
rm /usr/data/printer_data/config/GuppyScreen/axis_twist_compensation.cfg
```

Then delete the saved `[axis_twist_compensation]` block (if any) from `printer.cfg` and run
`FIRMWARE_RESTART`.

---

## If the installer skipped the probe patch

The installer patches `probe.py` automatically, but deliberately **skips** if your firmware's file looks
different than expected — better to skip than half-apply. If you see `STOP` in the install log, do the
one edit by hand:

1. Open `/usr/share/klipper/klippy/extras/probe.py`.
2. Find the function `def _probe` and, just before its `return epos[:3]` line, insert:
   ```python
        axis_twist_compensation = self.printer.lookup_object(
            'axis_twist_compensation', None)
        if axis_twist_compensation is not None:
            epos[2] += axis_twist_compensation.get_z_compensation_value(pos)
   ```
3. Save, then `FIRMWARE_RESTART`. Carry on to the calibration above.

---

## After a Creality firmware update

A Creality firmware update wipes these Klipper-side files. Getting back takes 2 minutes:

1. **Re-run the [installer](Installation)** and answer **Y** at the print-quality mods prompt.
2. **Re-run the calibration** (5-point paper test above) and `SAVE_CONFIG` — the saved correction is
   cleared by the firmware update.

> Next: **[Adaptive Meshing (KAMP)](Adaptive-Meshing-KAMP)** — mesh only the area your print actually
> uses, plus an automatic purge line.
