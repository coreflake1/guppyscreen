# Adaptive Meshing + Purge Line (KAMP)

KAMP makes your printer smarter about bed preparation before each print:

- **Adaptive meshing** — instead of mapping the whole 220×220 mm bed every time, it maps only the
  area your actual print footprint covers. Faster, and the points are denser where it matters.
- **Auto purge line** — draws a clean purge line right next to the print, so the nozzle is primed and
  clean for the first move.

These are independent — you can use either one on its own. Together they're a nice pair.

> **Do [Axis Twist Compensation](Axis-Twist-Compensation) first** if you have left-right unevenness.
> KAMP relies on accurate probing; there's no point in a denser mesh if the probe readings are skewed.

---

## Before you start

- ⏱️ **Time:** ~15 minutes (most of it is one slicer config change).
- 🧰 **You need:** the printer, Mainsail/Fluidd in a browser, and your slicer.
- ↩️ **Safe to try:** fully reversible — see [Undoing it](#undoing-it).

> ⚠️ These changes live in the printer's system files. **A Creality firmware update will erase them.** Re-run the OpenKE installer afterwards and they come back.

---

## Step 1 — Make sure KAMP is installed

The OpenKE installer drops in a pre-edited KAMP config during the print-quality mods step. To verify,
check Mainsail's Machine tab for a `GuppyScreen/KAMP/` folder in your config directory. If it's there,
you're good — jump to Step 2.

If it's not, re-run the [installer](Installation) and answer **Y** at the print-quality mods prompt.

---

## Step 2 — Tell your slicer to use it (OrcaSlicer)

KAMP is triggered from your slicer's **Machine start G-code**. Nothing works until this is set.

In **OrcaSlicer**, go to your printer profile → **Machine G-code** → **Machine start G-code**.

First, enable **Label Objects** in your print settings (Orca: **Others** tab → tick **Label Objects**).
KAMP uses the object labels to know the print footprint.

Then set your Machine start G-code to:

```gcode
M140 S[bed_temperature_initial_layer_single]   ; start heating bed (don't wait)
M104 S[nozzle_temperature_initial_layer]       ; start heating nozzle (don't wait)
G28                                             ; home all axes
ADAPTIVE_BED_MESH_CALIBRATE                     ; mesh just the print area
M190 S[bed_temperature_initial_layer_single]   ; wait for bed to reach temp
SMART_PARK                                      ; park next to the print while heating
M109 S[nozzle_temperature_initial_layer]       ; wait for nozzle temp
LINE_PURGE                                      ; purge a line beside the print
```

> The exact temperature token names can vary slightly across Orca versions — what matters is the
> **order**: home → mesh → wait for temps → purge. Don't wait for temps before meshing; a cold bed
> expands slightly as it heats, and you want the mesh taken at printing temperature.

---

## Optional — denser mesh

KAMP works fine with the stock 5×5 grid, but a 9×9 gives smoother compensation. If you want it, open
`printer.cfg` in Mainsail and update your `[bed_mesh]` section to:

```ini
[bed_mesh]
speed: 350
horizontal_move_z: 8
mesh_min: 5,10
mesh_max: 215,215
probe_count: 9,9
algorithm: bicubic
fade_start: 1
fade_end: 10
```

> **Don't add** `zero_reference_position` or `relative_reference_index` — they either aren't supported on
> the KE or conflict with KAMP. Remove them if a config you copied has them.

---

## Undoing it

The easiest path is the [uninstaller](Resetting-and-Uninstalling). By hand:

```sh
rm -rf /usr/data/printer_data/config/GuppyScreen/KAMP \
       /usr/data/printer_data/config/GuppyScreen/KAMP_Settings.cfg
```

Then `FIRMWARE_RESTART`. Remove `ADAPTIVE_BED_MESH_CALIBRATE`, `SMART_PARK`, and `LINE_PURGE` from
your slicer start G-code and replace with `BED_MESH_CALIBRATE` if you still want a full mesh.

---

## Manual installation (advanced)

The installer does all of this for you. This section is for reference — or if you need to reinstall by hand.

<details>
<summary>Manual steps</summary>

1. Download the config:
   ```sh
   cd /tmp
   git clone https://github.com/kyleisah/Klipper-Adaptive-Meshing-Purging.git kamp-src
   cd /usr/data/printer_data/config
   cp -r /tmp/kamp-src/Configuration ./KAMP
   cp /tmp/kamp-src/Configuration/KAMP_Settings.cfg ./KAMP_Settings.cfg
   ```
2. Edit `KAMP/Adaptive_Meshing.cfg` for KE compatibility:
   - rename `[gcode_macro BED_MESH_CALIBRATE]` → `[gcode_macro ADAPTIVE_BED_MESH_CALIBRATE]`
   - delete the `rename_existing:` line
   - in the macro body, change `_BED_MESH_CALIBRATE` → `BED_MESH_CALIBRATE`
3. In `KAMP_Settings.cfg`, uncomment the includes for `Adaptive_Meshing.cfg`, `Line_Purge.cfg`, and
   `Smart_Park.cfg`.
4. Add `[include KAMP_Settings.cfg]` near the top of `printer.cfg`, then `FIRMWARE_RESTART`.

</details>

---

## After a Creality firmware update

Re-run the [installer](Installation) with **Y** at the print-quality mods prompt. Your slicer start
G-code lives on your computer and survives — just double-check it after any OrcaSlicer update too.
