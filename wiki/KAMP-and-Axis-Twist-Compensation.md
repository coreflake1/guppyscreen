# Getting a Perfect First Layer (KAMP + Axis Twist Compensation)

This guide fixes the single most common Ender-3 V3 KE complaint: **a first layer that looks great in the
middle of the bed but is squished on one side and lifting/too-high on the other** — usually worse going
left-to-right. If you've leveled, re-leveled, meshed, and tweaked Z-offset and it *still* does this, you're
in the right place. We'll fix it properly, and it stays fixed.

You don't need to be a programmer. If you can copy-paste commands into a terminal, you can do this.

---

## Will this actually help me?

**Yes, if:** your first layer is inconsistent *across the bed* — one side smooshed, the other side barely
sticking — even though the bed is clean and not visibly warped, and bed mesh hasn't solved it.

**Probably not the whole story, if:** the bed physically rocks/has play (check by gently pushing the corners),
or one spot is dramatically high/low. Fix loose hardware first, then come back.

### Why this happens (in plain English)

Your printer probes the bed before a print to build a "map" (a *bed mesh*) and corrects for bumps. The probe
sits a couple of centimetres **behind** the nozzle. Here's the catch: the X-axis (the bar the nozzle rides
on) twists by a tiny fraction of a degree as it moves left to right. That twist tilts the probe just enough
that the height it measures **isn't** the height the nozzle will actually print at. The bed mesh faithfully
records this *wrong* number — so no amount of re-meshing fixes it.

**Axis Twist Compensation** measures that error at a few points and corrects it, so the probe finally tells
the truth. **KAMP** is a complementary upgrade that meshes only the area your print actually covers (faster,
more accurate) and lays a tidy purge line right next to the print. Together they give a clean first layer edge
to edge.

You can do **either one on its own** — they're independent. Axis Twist is the one that fixes the
left/right problem; KAMP is a quality-of-life bonus.

---

## Before you start

- ⏱️ **Time:** about 30–45 minutes, most of it the guided calibration.
- 🧰 **You need:** the printer on your network, a computer, and a sheet of paper. No tools, no disassembly.
- 💻 **Skill:** if you can paste a few commands into the Klipper console, you can do this.
- ↩️ **Safe to try:** every change here is reversible (see [Undoing it](#undoing-it)), and none of it touches
  your prints until you run the calibration.

> ### ⚠️ One important warning — read this
> These changes live in the printer's system files. **A Creality firmware update will erase them.** That's
> not a disaster — just re-run the OpenKE installer afterwards and the mods come back — but it's why you
> should bookmark this. After any firmware update, see [After a firmware update](#after-a-firmware-update) at the bottom.

### What this guide was tested on

| | |
|---|---|
| Printer | Creality Ender-3 V3 KE |
| Firmware | **V1.1.0.15** |
| Probe | CR-Touch / BLTouch (the standard KE probe) |
| Interface | Mainsail (Fluidd works too) |

---

## The installer already set this up

Good news: **you don't install KAMP or Axis Twist by hand anymore.** The OpenKE installer does it for you.

When you ran the installer it asked **"Install OpenKE print-quality mods? (Y/n)"** — if you said yes (the
default), it already:

- dropped in the **KAMP** config (pre-edited for the KE) and auto-included it,
- copied the **Axis Twist Compensation** module into Klipper and patched `probe.py` for you (safely, with a
  backup), and added the `[axis_twist_compensation]` section with the correct KE bed bounds.

**Didn't see that prompt, or said no?** Just re-run the [installer](Installation) and answer **Y** at the
mods prompt. It's idempotent — safe to run again. Then come back here.

The only thing left for *you* to do is the one-time calibration below (Axis Twist) and the slicer setup
(KAMP). Everything that needs a terminal is already done.

> You'll run a few commands in the **Klipper console** — that's the input box in Mainsail/Fluidd (the web
> page you open in a browser to control the printer). We'll say "in the console" when that's the case.

---

# Part B — Axis Twist Compensation (the left/right first-layer fix)

> The module is already installed (see above). This part is the **calibration** — the bit only you can do,
> since it measures *your* gantry.

### Calibrate it (the 5-point paper test)

In the **Klipper console**, start the guided calibration. **No heating needed** — just make sure the nozzle tip is
clean (no plastic blob):

```
BED_MESH_CLEAR
AXIS_TWIST_COMPENSATION_CALIBRATE SAMPLE_COUNT=5
```

The printer will probe, then move the nozzle to the first of **5 spots** across the bed and show a small
**"Manual Probe"** box. At each spot you do the classic paper test:

1. The nozzle starts about 5 mm up. Bring it down in steps using the buttons in the box (or `TESTZ Z=-1`,
   then smaller).
2. Slide a sheet of paper under the nozzle and keep lowering in **small** steps (`-0.1`, then `-0.05`,
   `-0.01`) until you feel **light drag** on the paper — the same feel as setting Z-offset. Too far? Back off
   with `+0.01`.
3. Tap **ACCEPT**. The nozzle lifts and moves to the next spot. Repeat.

> 🎯 **The one trick that matters:** use the *exact same* paper feel at all 5 spots. The tool only cares about
> the *difference* between spots, so consistency is everything.

After the 5th spot, it prints the results. Lock them in permanently:

```
SAVE_CONFIG
```

Klipper restarts and you're done. Run a print (or a first-layer test) that covers the whole bed — the
left/right unevenness should be gone. 🎉

*(If the overall height feels slightly off now, just nudge your Z-offset a touch as usual — the twist is
fixed, this is only fine-tuning.)*

> **On the OpenKE screen:** there's a built-in wizard — **Tune → Axis Twist** — that walks you through this
> same 5-point calibration on the printer itself, no console needed.

---

# Part A — KAMP (optional: smarter meshing + auto purge line)

KAMP makes the printer mesh only the footprint of your print (instead of the whole bed every time) and draws a
clean purge line right beside it. It's optional but nice. It needs the **Exclude Object** feature, which the
KE already has on by default.

### The installer already set up KAMP

The KAMP config (pre-edited for the KE — separate `ADAPTIVE_BED_MESH_CALIBRATE` command, so it doesn't fight
the KE's built-in meshing) is dropped in and auto-included by the installer. You don't clone anything or edit
config files. There's just **one optional tweak** and **one required slicer step**:

**Optional — richer mesh.** KAMP works with the stock 5×5 mesh, but a denser mesh is nicer. If you want it,
make your `[bed_mesh]` section in `printer.cfg` look like this (the two KE-fussy settings are flagged):

```ini
[bed_mesh]
speed: 350
horizontal_move_z: 8
mesh_min: 5,10
mesh_max: 215,215
probe_count: 9,9
algorithm: bicubic       # ← required when probe_count is above 6, or Klipper errors out
fade_start: 1
fade_end: 10
```

> Don't add `zero_reference_position` or `relative_reference_index` here — they either aren't supported on the
> KE or conflict with KAMP. If a config you copied has them, remove them.

### Required — tell your slicer to use it (OrcaSlicer)

KAMP runs from your slicer's **Machine start G-code**. In Orca, turn on **Label Objects**, then set your
machine start G-code to run things in this order:

```gcode
M140 S[bed_temperature_initial_layer_single]   ; start heating bed
M104 S[nozzle_temperature_initial_layer]       ; start heating nozzle (don't wait)
G28                                             ; home
ADAPTIVE_BED_MESH_CALIBRATE                     ; mesh just the print area
M190 S[bed_temperature_initial_layer_single]   ; wait for bed temp
SMART_PARK                                      ; park next to the print to finish heating
M109 S[nozzle_temperature_initial_layer]       ; wait for nozzle temp
LINE_PURGE                                      ; purge a line beside the print
```

The exact temperature tokens vary by Orca version; what matters is the **order**: home → mesh → heat → park →
purge.

---

## Set it up manually (advanced)

The installer does all of this for you. If you'd rather do it by hand — or want to know exactly what the
installer changed — here are the steps.

### Axis Twist (manual)
1. Drop the module into Klipper:
   ```sh
   wget --no-check-certificate \
     "https://raw.githubusercontent.com/Klipper3d/klipper/v0.12.0/klippy/extras/axis_twist_compensation.py" \
     -O /usr/share/klipper/klippy/extras/axis_twist_compensation.py
   ```
2. Connect it to the probe — make the one edit to `probe.py` shown in
   [If the installer skipped the probe patch](#if-the-installer-skipped-the-probe-patch-stop) above.
3. Add the config section to `printer.cfg` (anywhere above the `#*# SAVE_CONFIG` marker):
   ```ini
   [axis_twist_compensation]
   calibrate_start_x: 20
   calibrate_end_x: 200
   calibrate_y: 110
   ```
4. `FIRMWARE_RESTART`, then run the [calibration](#calibrate-it-the-5-point-paper-test) above.

### KAMP (manual)
1. Download and copy the config:
   ```sh
   cd /tmp
   git clone https://github.com/kyleisah/Klipper-Adaptive-Meshing-Purging.git kamp-src
   cd /usr/data/printer_data/config
   cp -r /tmp/kamp-src/Configuration ./KAMP
   cp /tmp/kamp-src/Configuration/KAMP_Settings.cfg ./KAMP_Settings.cfg
   ```
2. Edit `KAMP/Adaptive_Meshing.cfg` so it doesn't override the KE's built-in meshing (this is the part
   the vendored copy already has done):
   - rename `[gcode_macro BED_MESH_CALIBRATE]` → `[gcode_macro ADAPTIVE_BED_MESH_CALIBRATE]`
   - delete the `rename_existing:` line
   - in the call near the bottom, drop the leading underscore: `_BED_MESH_CALIBRATE …` → `BED_MESH_CALIBRATE …`
3. In `KAMP_Settings.cfg`, uncomment the includes for `Adaptive_Meshing.cfg`, `Line_Purge.cfg`, and
   `Smart_Park.cfg`.
4. Add `[include KAMP_Settings.cfg]` near the top of `printer.cfg`, then `FIRMWARE_RESTART`.

---

## Undoing it

Everything reverts cleanly. The easiest path is the installer's uninstall (see [Installation](Installation)),
which removes the KAMP/Axis-Twist config and tells you how to restore `probe.py`. By hand:

**Axis Twist Compensation:**
```sh
cp -a /usr/data/guppyify-backup/probe.py.bak /usr/share/klipper/klippy/extras/probe.py
rm /usr/share/klipper/klippy/extras/axis_twist_compensation.py
rm /usr/data/printer_data/config/GuppyScreen/axis_twist_compensation.cfg
```
…then delete the saved `[axis_twist_compensation]` block (if any) from `printer.cfg` and `FIRMWARE_RESTART`.

**KAMP:**
```sh
rm -rf /usr/data/printer_data/config/GuppyScreen/KAMP \
       /usr/data/printer_data/config/GuppyScreen/KAMP_Settings.cfg
```
…then `FIRMWARE_RESTART`.

---

## If the installer skipped the probe patch ("STOP")

The installer patches `probe.py` automatically, but it deliberately **skips** (printing `STOP`) if your
firmware's `probe.py` is structured differently than expected — better to skip than half-apply. If that
happened, do the one edit by hand:

1. Open `/usr/share/klipper/klippy/extras/probe.py`.
2. Find the function `def _probe` and, just before its `return epos[:3]` line, paste:
   ```python
        axis_twist_compensation = self.printer.lookup_object(
            'axis_twist_compensation', None)
        if axis_twist_compensation is not None:
            epos[2] += axis_twist_compensation.get_z_compensation_value(pos)
   ```
3. Save, then `FIRMWARE_RESTART`. If Klipper comes back happy, carry on to the calibration above.

---

## After a firmware update

A Creality firmware update wipes the Klipper-side changes (this is true of *any* Klipper mod). Putting them
back takes a couple of minutes:

1. **Re-run the OpenKE [installer](Installation)** and answer **Y** at the print-quality-mods prompt. That
   re-installs KAMP and re-patches `probe.py` for you.
2. **Re-calibrate Axis Twist** (the 5-point test above) and `SAVE_CONFIG` — the saved correction is cleared by
   the update.
3. Your slicer start G-code lives on your computer, so KAMP's slicer side survives — just double-check it after
   any OrcaSlicer update.

> Bookmark this page and revisit it after **every** Creality firmware update.
