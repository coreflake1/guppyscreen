# First-Layer Mods: KAMP & Axis Twist Compensation (Ender-3 V3 KE)

Two Klipper modifications that, together, give a near-perfect first layer across the **entire** bed on the
Ender-3 V3 KE:

- **KAMP** (Klipper Adaptive Meshing & Purging) — meshes only the area the print actually occupies, parks
  near the part for final heat-up, and lays an adaptive purge line beside it.
- **Axis Twist Compensation** — corrects the left-to-right Z error caused by tiny X-gantry twist combined
  with the probe's non-zero `y_offset`. This is the classic *"first layer too far on the left, too close on
  the right"* symptom that **bed mesh alone cannot fix** (the mesh trusts the biased probe reading). The two
  are complementary: KAMP feeds the probe, axis twist compensation makes the probe honest.

> ⚠️ **These are root-filesystem changes and a Creality OTA firmware flash will erase all of them.** That is
> exactly why this page exists — so you can reinstall in minutes instead of re-deriving everything. After any
> firmware update, walk this page again.

---

## Scope — verified configuration

| Property | Value |
|---|---|
| Printer | Creality Ender-3 V3 KE |
| Firmware | **V1.1.0.15** (`/etc/ota_info` → `ota_version=1.1.0.15`, board `F005`) |
| Probe | **BLTouch/CR-Touch**, `x_offset: 0`, `y_offset: 27` (probe behind nozzle) |
| Klipper | Creality's modified fork (Python 3.8, old `probe.py` structure) |
| Front-end | Mainsail (Fluidd works the same) |
| Last verified | 2026-06-06 |

> **Firmware matters.** The popular Reddit/lmutt `probe.py` *patch file* is a line-numbered context diff built
> for **V1.1.0.14** and **fails on V1.1.0.15** (`Hunk #1 FAILED at 129`) because Creality rewrote `_probe`.
> This guide uses a **hand-graft keyed on a code anchor, not line numbers**, so it survives across firmware
> revisions. If a future firmware renames the anchor line, adjust the `ANCHOR` string in the snippet below.

## Prerequisites

- **Root SSH access** to the printer: `ssh root@<printer-ip>` (default Creality root login).
- **Mainsail or Fluidd** for the Klipper console (calibration is run from there).
- `[exclude_object]` enabled and the slicer **labeling objects** — required by KAMP's adaptive mesh.
  On this printer it is already present:
  - `gcode_macro.cfg` contains `[exclude_object]`
  - `moonraker.conf` has `enable_object_processing: True`
- Config dir: `/usr/data/printer_data/config` (referred to below as `$CFG`).
- Klipper extras dir: `/usr/share/klipper/klippy/extras` (referred to below as `$EXTRAS`).

**Always back up first:**

```sh
cd /usr/data/printer_data/config
cp -a printer.cfg printer.cfg.bak-$(date +%Y%m%d)
cp -a /usr/share/klipper/klippy/extras/probe.py /usr/share/klipper/klippy/extras/probe.py.bak-$(date +%Y%m%d)
```

---

# Part A — KAMP (Adaptive Meshing & Purging)

### A1. Install the KAMP files

Clone upstream and copy the configuration set into the config directory (do **not** symlink — Mainsail
needs real files):

```sh
cd /tmp
git clone https://github.com/kyleisah/Klipper-Adaptive-Meshing-Purging.git kamp-src
cd /usr/data/printer_data/config
cp -r /tmp/kamp-src/Configuration ./KAMP
cp /tmp/kamp-src/Configuration/KAMP_Settings.cfg ./KAMP_Settings.cfg
```

You should end up with:

```
$CFG/KAMP_Settings.cfg
$CFG/KAMP/Adaptive_Meshing.cfg
$CFG/KAMP/Line_Purge.cfg
$CFG/KAMP/Smart_Park.cfg
$CFG/KAMP/Voron_Purge.cfg
```

### A2. KE-safe edit to `KAMP/Adaptive_Meshing.cfg`

Upstream ships the adaptive macro **as an override of** `BED_MESH_CALIBRATE` (using `rename_existing`). On
this old Creality fork that is fragile and collides with the built-in. Make the macro a **separate command**
that calls the native built-in instead. Apply these three edits:

1. Rename the macro header:
   ```ini
   [gcode_macro BED_MESH_CALIBRATE]      →      [gcode_macro ADAPTIVE_BED_MESH_CALIBRATE]
   ```
2. **Delete** the `rename_existing:` line (e.g. `rename_existing: _BED_MESH_CALIBRATE`).
3. At the bottom of the macro, change the internal call from the renamed alias to the **native** built-in:
   ```ini
   _BED_MESH_CALIBRATE mesh_min=... mesh_max=... ALGORITHM=... PROBE_COUNT=...
   →
   BED_MESH_CALIBRATE   mesh_min=... mesh_max=... ALGORITHM=... PROBE_COUNT=...
   ```

This leaves the stock `G29` macro and the native `BED_MESH_CALIBRATE` untouched, and exposes a new
`ADAPTIVE_BED_MESH_CALIBRATE` command. (On the KE, `BED_MESH_CALIBRATE` is the Klipper built-in — there is
**no** `[gcode_macro BED_MESH_CALIBRATE]` override anywhere, so it accepts `mesh_min`/`mesh_max`/`probe_count`
params directly.)

### A3. KAMP settings (`KAMP_Settings.cfg`)

Enable the three modules we use (Voron_Purge stays off) and set the purge/park values verified on this
machine:

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
# (probe_dock settings left at defaults / disabled)
```

### A4. `printer.cfg` — include + bed mesh density

Add the include alongside the other includes near the top:

```ini
[include KAMP_Settings.cfg]
```

And set the bed mesh to a richer grid. **Two KE-fork gotchas:**

```ini
[bed_mesh]
speed: 350
horizontal_move_z: 8
mesh_min: 5,10           # need to handle head distance with bl_touch
mesh_max: 215,215        # max probe range
probe_count: 9,9
algorithm: bicubic       # REQUIRED for probe_count > 6 (lagrange caps at 6/axis on this fork)
fade_start: 1
fade_end: 10
```

- ❗ `probe_count: 9,9` **requires** `algorithm: bicubic` — the default `lagrange` errors with *"cannot
  exceed a probe_count of 6 when using lagrange."*
- ❗ Do **not** add `zero_reference_position` (not a valid option on this fork) and do **not** use
  `relative_reference_index` — the fixed grid index is incompatible with KAMP's shrinking adaptive mesh.

### A5. Restart and verify

```
FIRMWARE_RESTART
```

In the console, confirm these commands now exist: `ADAPTIVE_BED_MESH_CALIBRATE`, `LINE_PURGE`, `SMART_PARK`,
and `_KAMP_Settings`. The stock `G29` and native `BED_MESH_CALIBRATE` should still work.

### A6. Slicer start G-code (OrcaSlicer)

KAMP runs from the **slicer's machine start G-code** (the KE has no `PRINT_START` macro — the start sequence
lives slicer-side). Enable **Label Objects** in Orca, then use a machine start sequence along these lines:

```gcode
M140 S[bed_temperature_initial_layer_single]   ; start bed heat
M104 S[nozzle_temperature_initial_layer]       ; start nozzle heat (no wait)
G28                                             ; home all
ADAPTIVE_BED_MESH_CALIBRATE                     ; adaptive mesh of the print area
M190 S[bed_temperature_initial_layer_single]   ; wait for bed
SMART_PARK                                      ; park near the part
M109 S[nozzle_temperature_initial_layer]       ; wait for nozzle
LINE_PURGE                                      ; adaptive purge line beside the part
```

> The exact macro/temperature tokens depend on your Orca version; the **order** is what matters:
> home → adaptive mesh → (bed heat) → smart park → (nozzle heat) → line purge.

---

# Part B — Axis Twist Compensation

### B1. Install the module

Drop in the stock Klipper **v0.12.0** module (it pairs cleanly with this old fork — it calls
`probe.run_probe(gcmd)`, a method this `probe.py` has):

```sh
wget --no-check-certificate \
  "https://raw.githubusercontent.com/Klipper3d/klipper/v0.12.0/klippy/extras/axis_twist_compensation.py" \
  -O /usr/share/klipper/klippy/extras/axis_twist_compensation.py
```

Sanity-check it compiles: `python3 -m py_compile /usr/share/klipper/klippy/extras/axis_twist_compensation.py`
(expected md5 `28da5f89fa4ede80e833ce0756793fbd`).

### B2. Graft the hook into `probe.py` (the part the gist patch botches)

The module does nothing until `probe.py` applies its correction. The required change is just four lines
inside `PrinterProbe._probe`, immediately after the probing move and before the result is returned:

```python
        # --- axis_twist_compensation (hand-grafted; stock v0.12.0 probe.py hook) ---
        axis_twist_compensation = self.printer.lookup_object(
            'axis_twist_compensation', None)
        if axis_twist_compensation is not None:
            epos[2] += axis_twist_compensation.get_z_compensation_value(pos)
```

Apply it safely with this anchor-based Python snippet (works regardless of line numbers; refuses to run twice
or if the anchor moved):

```sh
python3 - <<'PY'
import py_compile, sys
p = '/usr/share/klipper/klippy/extras/probe.py'
s = open(p).read()
ANCHOR = '        msg = "probe at %.3f,%.3f is z=%.6f" % (epos[0], epos[1], epos[2] - self.z_offset)'
if 'axis_twist_compensation' in s:
    sys.exit('already grafted — nothing to do')
if s.count(ANCHOR) != 1:
    sys.exit('ABORT: anchor not found uniquely (firmware differs — graft by hand near the end of _probe)')
graft = (
    "        # --- axis_twist_compensation (hand-grafted; stock v0.12.0 probe.py hook) ---\n"
    "        axis_twist_compensation = self.printer.lookup_object(\n"
    "            'axis_twist_compensation', None)\n"
    "        if axis_twist_compensation is not None:\n"
    "            epos[2] += axis_twist_compensation.get_z_compensation_value(pos)\n"
)
open(p, 'w').write(s.replace(ANCHOR, graft + ANCHOR, 1))
py_compile.compile(p, doraise=True)
print('OK: probe.py grafted and compiles clean')
PY
```

If the anchor isn't found (a future firmware changed that line), open `probe.py`, find `def _probe`, and paste
the four-line block right before the method's `return epos[:3]`.

### B3. Add the config block

Add to `printer.cfg`, **above** the `#*# <---------------------- SAVE_CONFIG ---------------------->` marker:

```ini
[axis_twist_compensation]
calibrate_start_x: 20
calibrate_end_x: 200
calibrate_y: 110
```

Geometry note for the KE (`x_offset: 0`, `y_offset: 27`): these defaults keep the probe on the bed and inside
the `5,10 → 215,215` mesh region. The stock offsets are already correct on this machine — **do not** retune
them (the "fix your offsets first" advice in the Reddit thread was for the Ender-3 V3 **SE**).

### B4. Restart, calibrate, save

```
FIRMWARE_RESTART
```

Confirm Klipper comes back `ready` and `AXIS_TWIST_COMPENSATION_CALIBRATE` is registered. Then calibrate
(nozzle clean; **no heating needed**):

```
BED_MESH_CLEAR
AXIS_TWIST_COMPENSATION_CALIBRATE SAMPLE_COUNT=5
```

A **Manual Probe** dialog appears at each of 5 X positions, starting ~5 mm above the bed:

1. Lower in coarse steps (`-1`) toward ~1 mm, watching the nozzle.
2. Approach with `-0.1`, slide a sheet of paper under the nozzle.
3. Fine-tune with `-0.05` / `-0.01` until the paper has **light drag** (back off with `+0.01`/`+0.05`).
4. **ACCEPT.** The head lifts and moves to the next point — repeat with the *same* paper feel each time
   (consistency between points is what makes the result accurate).

The console prints the per-point offsets and mean z_offset. Persist permanently:

```
SAVE_CONFIG
```

Klipper restarts and writes the results into the auto-generated section of `printer.cfg`. (`SAVE_CONFIG` is
registered on this fork alongside Creality's `CXSAVE_CONFIG` and works correctly.)

Finally, run a fresh mesh and a wide first-layer test — the left/right bias should be gone.

---

## Reverting

Either mod reverts cleanly:

**Axis twist compensation**
```sh
cp -a /usr/share/klipper/klippy/extras/probe.py.bak-<date> /usr/share/klipper/klippy/extras/probe.py
rm /usr/share/klipper/klippy/extras/axis_twist_compensation.py
# remove the [axis_twist_compensation] block from printer.cfg
FIRMWARE_RESTART
```

**KAMP**
```sh
# remove [include KAMP_Settings.cfg] from printer.cfg (and revert [bed_mesh] if desired)
rm -rf /usr/data/printer_data/config/KAMP /usr/data/printer_data/config/KAMP_Settings.cfg
FIRMWARE_RESTART
```

---

## Quick reinstall checklist (after a firmware flash)

1. `ssh root@<printer-ip>`; back up `printer.cfg` and `probe.py`.
2. **KAMP:** clone → copy `KAMP/` + `KAMP_Settings.cfg`; apply the 3 edits to `Adaptive_Meshing.cfg`
   (A2); set `KAMP_Settings.cfg` values (A3); add `[include KAMP_Settings.cfg]` and the `[bed_mesh]`
   `probe_count: 9,9` + `algorithm: bicubic` (A4).
3. **Axis twist:** `wget` the v0.12.0 module (B1); run the graft snippet (B2); add the
   `[axis_twist_compensation]` block (B3).
4. `FIRMWARE_RESTART` → verify both load.
5. `BED_MESH_CLEAR` → `AXIS_TWIST_COMPENSATION_CALIBRATE SAMPLE_COUNT=5` → paper test ×5 → `SAVE_CONFIG`.
6. Re-check the slicer start G-code still has the KAMP sequence + Label Objects (A6) — slicer profiles are
   not on the printer, so they survive a flash, but verify after any Orca update.

> Reminder: re-run this whole checklist after **every** Creality OTA firmware update.
