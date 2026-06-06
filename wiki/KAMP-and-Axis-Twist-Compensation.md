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
- 💻 **Skill:** comfortable opening a terminal and pasting commands. That's it.
- ↩️ **Safe to try:** every change here is reversible (see [Undoing it](#undoing-it)), and none of it touches
  your prints until you run the calibration.

> ### ⚠️ One important warning — read this
> These changes live in the printer's system files. **A Creality firmware update will erase them.** That's
> not a disaster — this whole page is written so you can redo it in a few minutes — but it's why you should
> bookmark it. After any firmware update, just run through the [Quick reinstall](#quick-reinstall-after-a-firmware-update) at the bottom.

### What this guide was tested on

| | |
|---|---|
| Printer | Creality Ender-3 V3 KE |
| Firmware | **V1.1.0.15** |
| Probe | CR-Touch / BLTouch (the standard KE probe) |
| Interface | Mainsail (Fluidd works too) |

---

## Connecting to your printer

Everything below is typed into your printer over **SSH** (a remote terminal). From your computer:

```sh
ssh root@<your-printer-ip>
```

Replace `<your-printer-ip>` with your printer's address (e.g. `192.168.0.231` — you can find it on the
printer's WiFi screen). When it asks for a password, use your printer's root password. Windows users: download
[PuTTY](https://www.putty.org/) and connect the same way.

**Make a safety backup first** (so you can always undo):

```sh
cd /usr/data/printer_data/config
cp -a printer.cfg printer.cfg.bak-$(date +%Y%m%d)
cp -a /usr/share/klipper/klippy/extras/probe.py /usr/share/klipper/klippy/extras/probe.py.bak-$(date +%Y%m%d)
```

You'll also run a few commands in the **Klipper console** — that's the input box in Mainsail/Fluidd (the web
page you open in a browser to control the printer). We'll say "in the console" when that's the case.

---

# Part B — Axis Twist Compensation (the left/right first-layer fix)

> Doing just this part? Great — it's the one that fixes the uneven first layer. KAMP (Part A) is optional and
> can be added later.

### Step 1 — Add the compensation tool to Klipper

Paste this into your SSH terminal. It downloads one small file that teaches Klipper how to do the correction:

```sh
wget --no-check-certificate \
  "https://raw.githubusercontent.com/Klipper3d/klipper/v0.12.0/klippy/extras/axis_twist_compensation.py" \
  -O /usr/share/klipper/klippy/extras/axis_twist_compensation.py
```

### Step 2 — Connect the tool to the probe

By itself, the file from Step 1 does nothing — Klipper needs a small edit to actually *use* it during probing.
Just paste this whole block into SSH; it makes the edit for you, safely. (It refuses to run twice and won't
break anything if your printer is too different.)

```sh
python3 - <<'PY'
import py_compile, sys
p = '/usr/share/klipper/klippy/extras/probe.py'
s = open(p).read()
ANCHOR = '        msg = "probe at %.3f,%.3f is z=%.6f" % (epos[0], epos[1], epos[2] - self.z_offset)'
if 'axis_twist_compensation' in s:
    sys.exit('Already done — nothing to change.')
if s.count(ANCHOR) != 1:
    sys.exit('STOP: your firmware differs from what this expects. See "If Step 2 says STOP" below.')
graft = (
    "        # --- axis_twist_compensation (added by hand) ---\n"
    "        axis_twist_compensation = self.printer.lookup_object(\n"
    "            'axis_twist_compensation', None)\n"
    "        if axis_twist_compensation is not None:\n"
    "            epos[2] += axis_twist_compensation.get_z_compensation_value(pos)\n"
)
open(p, 'w').write(s.replace(ANCHOR, graft + ANCHOR, 1))
py_compile.compile(p, doraise=True)
print('Success: the probe is now connected to axis twist compensation.')
PY
```

You want to see **"Success:"**. If you see "STOP", jump to [If Step 2 says STOP](#if-step-2-says-stop) — it's
rare and fixable.

> **Why not the popular Reddit patch?** The widely-shared `.patch` file only works on one exact firmware
> version (V1.1.0.14) and silently fails on others — that's why so many people report "I followed the guide
> and nothing changed." The block above does the same edit but adapts to your file, so it keeps working across
> firmware versions.

### Step 3 — Tell Klipper to use it

Open `printer.cfg` (easiest in Mainsail: **Machine** tab → `printer.cfg`). Add these lines **anywhere above**
the line that says `#*# <---------------------- SAVE_CONFIG ---------------------->`:

```ini
[axis_twist_compensation]
calibrate_start_x: 20
calibrate_end_x: 200
calibrate_y: 110
```

Save the file. These numbers are already correct for the KE — you don't need to change them.

### Step 4 — Restart and calibrate

In the **Klipper console**, restart:

```
FIRMWARE_RESTART
```

When it comes back, start the guided calibration. **No heating needed** — just make sure the nozzle tip is
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

> **Using GuppyKE?** There's now a built-in wizard for this — **Tune → Axis Twist** — that walks you through
> the same 5-point calibration on the printer's screen, no console needed. (It still needs Steps 1–3 done
> first, since those add the tool to Klipper.)

---

# Part A — KAMP (optional: smarter meshing + auto purge line)

KAMP makes the printer mesh only the footprint of your print (instead of the whole bed every time) and draw a
clean purge line right beside it. It's optional but nice. It needs the **Exclude Object** feature, which the
KE already has on by default.

### Step 1 — Download KAMP

```sh
cd /tmp
git clone https://github.com/kyleisah/Klipper-Adaptive-Meshing-Purging.git kamp-src
cd /usr/data/printer_data/config
cp -r /tmp/kamp-src/Configuration ./KAMP
cp /tmp/kamp-src/Configuration/KAMP_Settings.cfg ./KAMP_Settings.cfg
```

### Step 2 — One required edit for the KE

KAMP normally *replaces* the printer's built-in meshing command, which doesn't play nicely with the KE's older
Klipper. We'll make it a **separate** command instead. Open `KAMP/Adaptive_Meshing.cfg` and make three small
changes:

1. Change the first line of the macro from
   `[gcode_macro BED_MESH_CALIBRATE]` to `[gcode_macro ADAPTIVE_BED_MESH_CALIBRATE]`
2. **Delete** the line that starts with `rename_existing:`
3. Near the bottom, find the line that begins with `_BED_MESH_CALIBRATE` and remove the leading underscore so
   it reads `BED_MESH_CALIBRATE mesh_min=...` (just delete the `_`).

### Step 3 — KAMP settings

Open `KAMP_Settings.cfg` and make it look like this (these values are tuned for the KE):

```ini
[include ./KAMP/Adaptive_Meshing.cfg]
[include ./KAMP/Line_Purge.cfg]
[include ./KAMP/Smart_Park.cfg]

[gcode_macro _KAMP_Settings]
variable_verbose_enable: True
variable_mesh_margin: 0
variable_fuzz_amount: 0
variable_purge_height: 0.8
variable_tip_distance: 0
variable_purge_margin: 10
variable_purge_amount: 30
variable_flow_rate: 12
variable_smart_park_height: 10
```

### Step 4 — Turn it on in `printer.cfg`

Add this line near the top of `printer.cfg`, with the other `[include ...]` lines:

```ini
[include KAMP_Settings.cfg]
```

And make your `[bed_mesh]` section look like this. **Two settings the KE is fussy about** are flagged:

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
> KE or conflict with KAMP. If you copied a config that has them, remove them.

Restart (`FIRMWARE_RESTART`). KAMP is installed.

### Step 5 — Tell your slicer to use it (OrcaSlicer)

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

## Undoing it

Everything reverts cleanly.

**Axis Twist Compensation:**
```sh
cp -a /usr/share/klipper/klippy/extras/probe.py.bak-<date> /usr/share/klipper/klippy/extras/probe.py
rm /usr/share/klipper/klippy/extras/axis_twist_compensation.py
```
…then delete the `[axis_twist_compensation]` block from `printer.cfg` and `FIRMWARE_RESTART`.

**KAMP:**
```sh
rm -rf /usr/data/printer_data/config/KAMP /usr/data/printer_data/config/KAMP_Settings.cfg
```
…then remove `[include KAMP_Settings.cfg]` from `printer.cfg` and `FIRMWARE_RESTART`.

---

## If Step 2 says STOP

This only happens if your firmware's `probe.py` is structured differently than expected (e.g. a much newer or
older firmware). You can still do the edit by hand:

1. Open `/usr/share/klipper/klippy/extras/probe.py`.
2. Find the function `def _probe` and, just before its `return epos[:3]` line, paste:
   ```python
        axis_twist_compensation = self.printer.lookup_object(
            'axis_twist_compensation', None)
        if axis_twist_compensation is not None:
            epos[2] += axis_twist_compensation.get_z_compensation_value(pos)
   ```
3. Save, then `FIRMWARE_RESTART`. If Klipper comes back happy, continue from Step 3.

---

## Quick reinstall (after a firmware update)

A firmware update wipes these. To put them back in a few minutes:

1. SSH in; back up `printer.cfg` and `probe.py` (the two `cp` commands at the top).
2. **Axis Twist:** redo Steps 1–3, restart, recalibrate (Step 4), `SAVE_CONFIG`.
3. **KAMP (if you use it):** redo Steps 1–4. Your slicer start G-code lives on your computer, so it survives
   the update — but double-check it after any OrcaSlicer update.

> Bookmark this page. Re-run it after **every** Creality firmware update.
