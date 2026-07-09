# Adaptive Meshing + Purge Line

OpenKE makes your printer smarter about bed preparation before each print:

- **Adaptive meshing** — instead of mapping the whole 220×220 mm bed every time, it maps only the
  area your actual print footprint covers. Faster, and the points are denser where it matters.
  Uses Klipper's own native adaptive meshing under the hood, ported onto the KE's Klipper.
- **Auto purge line** — draws a clean purge line right next to the print, so the nozzle is primed and
  clean for the first move.

These are independent — you can use either one on its own. Just include the macro(s) you want in your
start G-code and drop the ones you don't: e.g. skip `LINE_PURGE` entirely if your slicer already draws
its own purge/skirt, or skip `SMART_PARK` if you don't care where the nozzle parks while heating. Together
they're a nice pair, but nothing forces you to use all three.

> **Do [Axis Twist Compensation](Axis-Twist-Compensation) first** if you have left-right unevenness.
> Adaptive meshing relies on accurate probing; there's no point in a denser mesh if the probe readings
> are skewed.

---

## Before you start

- ⏱️ **Time:** ~15 minutes (most of it is one slicer config change).
- 🧰 **You need:** the printer, Mainsail/Fluidd in a browser, and your slicer.
- ↩️ **Safe to try:** fully reversible — see [Undoing it](#undoing-it).

> ⚠️ These changes live in the printer's system files. **A Creality firmware update will erase them.** Re-run the OpenKE installer afterwards and they come back.

---

## Step 1 — Make sure it's installed

The OpenKE installer drops this in during the print-quality mods step. To verify, check Mainsail's
Machine tab for a `GuppyScreen/modules/` folder (with `Adaptive_Meshing.cfg`, `Line_Purge.cfg`,
`Smart_Park.cfg` inside) plus `GuppyScreen/Settings.cfg`. If they're there, you're good — jump to
Step 2.

If not, re-run the [installer](Installation) and answer **Y** at the print-quality mods prompt. If
you previously had **KAMP** installed (an older OpenKE version vendored it directly), the installer
detects it, carries over any settings you'd customized, and removes the old files automatically —
nothing to do by hand.

---

## Step 2 — Tell your slicer to use it (OrcaSlicer)

This is triggered from your slicer's **Machine start G-code**. Nothing works until this is set.

In **OrcaSlicer**, go to your printer profile → **Machine G-code** → **Machine start G-code**.

First, enable **Label Objects** in your print settings (Orca: **Others** tab → tick **Label Objects**).
The adaptive macros use the object labels to know the print footprint.

> 🎁 **Bonus:** the same **Label Objects** setting also enables **Exclude Object** — if a print starts
> failing partway through, you can drop the failed piece and let the rest keep printing. One setting,
> two features.

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

> 🛟 **Nothing detected? No harm done.** If Label Objects isn't ticked (or your slicer didn't write
> object labels for some other reason), `ADAPTIVE_BED_MESH_CALIBRATE` doesn't fail or stop your print —
> it just prints *"No objects detected! ... Defaulting to regular meshing"* in the console and falls
> back to a normal full-bed mesh. Worst case you lose the "adaptive" speed-up for that one print; nothing
> breaks.

### Using a different slicer (Cura, PrusaSlicer, SuperSlicer)

The G-code above is Orca-flavoured, but these macros don't care which slicer sends them — only that
**object labels** are present and the **order** (home → mesh → wait for temps → purge) is right. The
setting names differ:

| Slicer | Equivalent of "Label Objects" | Where to add the start G-code |
|---|---|---|
| **Cura** | Exclude Objects is built in and always labels objects — nothing to enable | Printer Settings → **Start G-code** |
| **PrusaSlicer / SuperSlicer** | **Label objects** checkbox, Print Settings → **Output options** | Printer Settings → **Custom G-code** → **Start G-code** |

Swap the bracketed temperature tokens (`[bed_temperature_initial_layer_single]`, etc.) for whatever your
slicer's own placeholder syntax is — check its start G-code documentation or an existing default profile
for the exact names. The macro calls themselves (`ADAPTIVE_BED_MESH_CALIBRATE`, `SMART_PARK`,
`LINE_PURGE`) are identical no matter which slicer sends them.

---

## Optional — denser mesh

This works fine with the stock 5×5 grid, but a 9×9 gives smoother compensation. If you want it, open
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
> the KE or conflict with adaptive meshing. Remove them if a config you copied has them.

---

## Optional — further tuning

`Settings.cfg` (Mainsail Machine tab, in `GuppyScreen/`) has more variables than most people ever
touch, but they're there if you want to adjust the defaults:

| Variable | Controls |
|---|---|
| `variable_mesh_margin` | Extra margin added around the print footprint before meshing |
| `variable_purge_height` | Z height of the purge line |
| `variable_purge_margin` | Distance between the purge line and the print |
| `variable_purge_amount` | How much filament the purge line uses |
| `variable_tip_distance` | Retract distance at the end of the purge |
| `variable_flow_rate` | Purge line flow rate |
| `variable_smart_park_height` | Z height while parked and waiting to heat |

Leave these alone unless you have a specific reason to change one — the defaults work for the KE as
shipped.

---

## Done when

- Your slicer's start G-code includes the macros you want (mesh, park, purge, or all three).
- A print starts, and the console shows the mesh running over roughly the print's footprint — not the
  whole bed — before heating finishes.
- A purge line (if enabled) appears right next to the print, not across the whole bed.

---

## Undoing it

The easiest path is the [uninstaller](Resetting-and-Uninstalling). By hand:

```sh
rm -rf /usr/data/printer_data/config/GuppyScreen/modules \
       /usr/data/printer_data/config/GuppyScreen/Settings.cfg
```

Then `FIRMWARE_RESTART`. Remove `ADAPTIVE_BED_MESH_CALIBRATE`, `SMART_PARK`, and `LINE_PURGE` from
your slicer start G-code and replace with `BED_MESH_CALIBRATE` if you still want a full mesh.

---

## Where this code actually lives

Unlike most third-party Klipper mods, this isn't a vendored copy of someone else's package anymore.
`ADAPTIVE_BED_MESH_CALIBRATE` delegates to Klipper's own native adaptive meshing (ported onto the
KE's older Klipper via a small, targeted patch — see `patch_bed_mesh.py`); `SMART_PARK` and
`LINE_PURGE` are OpenKE-maintained macros (originally adapted from
[KAMP](https://github.com/kyleisah/Klipper-Adaptive-Meshing-Purging), credited in `NOTICE.md`).
Everything lives in [`k1/k1_mods/klipper_mods/adaptive_print_setup/`](../k1/k1_mods/klipper_mods/adaptive_print_setup/)
if you want to see or adapt it yourself — see [Building from Source](Building-from-Source).

---

## After a Creality firmware update

Re-run the [installer](Installation) with **Y** at the print-quality mods prompt. Your slicer start
G-code lives on your computer and survives — just double-check it after any OrcaSlicer update too.
