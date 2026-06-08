# Fixing Parts That Come Out Skewed (Skew Correction)

Do your prints come out as a slight **parallelogram** instead of a true square? A box whose lid won't sit
square, holes that look a touch oval, a 100 mm part that measures 100 mm on each side but isn't quite
*square*? That's **skew** — and GuppyKE can correct it entirely from the printer's screen.

You don't need a computer or a slicer for this. Print one test frame from the screen, measure it with
calipers, type in three numbers, and you're done.

---

## Will this actually help me?

**Yes, if:** your printer's axes aren't perfectly at 90° to each other (extremely common, usually by a
tiny amount). The tell-tale sign: a printed square has **two diagonals of different lengths**, or parts
that should be square come out leaning.

**Probably not worth it, if:** you only print models, figurines, or decorative things. Skew is a fraction
of a degree — you'll never see it on a dragon, only on parts that must *fit* something. If you don't print
functional/engineering parts, you can happily skip this.

### The 30-second test for whether you even have skew

Print any square (or the test frame below), pop it off the bed, and measure the **two diagonals** corner-to-corner
with calipers. If they're the same length → you have no meaningful skew, nothing to fix. If they differ by
more than ~0.2–0.3 mm → skew correction will tidy that up.

### Why it happens (in plain English)

A 3D printer assumes its X and Y axes meet at a perfect 90° corner. In reality the frame can be off by a
hair. Every layer then prints as a very slightly leaning parallelogram instead of a rectangle. No amount of
bed leveling or meshing fixes it, because it's a *geometry* problem, not a height problem. **Skew correction**
measures the error from a test print and shears every move by the opposite amount, so squares come out square.

---

## Before you start

- ⏱️ **Time:** ~15 minutes (a short test print + three caliper measurements).
- 🧰 **You need:** a pair of calipers (digital is easiest), and filament loaded. No computer, no tools.
- ↩️ **Safe to try:** it changes nothing until you enter measurements and tap Apply, and it's a one-tap undo.

> **Requirement:** your printer needs the `[skew_correction]` section in `printer.cfg`. On GuppyKE builds
> this is already added. If the **Skew** button says *"not enabled,"* see [Enabling it](#enabling-it) at the bottom.

---

## Step 1 — Print the test frame (from the screen)

GuppyKE ships a ready-to-print calibration frame — **no slicing needed**.

1. On the printer, go to the **Files / Print** screen.
2. Find **`GuppyKE_Skew_Calibration.gcode`** and start it.
3. It heats up (PLA: 200 °C nozzle / 60 °C bed — see the note below for other materials), homes, meshes the
   bed, and prints a **150 mm square frame** a millimetre tall. Takes about 10 minutes.

> 🧵 **Filament note:** the frame is set for PLA. For PETG/ABS, print your own square at the right temps
> instead — skew correction only cares about the *shape*, not what it's made of. Any flat, ~150 mm square
> with crisp corners works.

When it's done and cool, gently pop the frame off the bed. **Keep track of which corner was at the front-left
of the printer** — that's corner **A**. (If it helps, mark it with a pen as you lift it off.)

---

## Step 2 — Measure the three lengths

Lay the frame flat. The corners are named like this, **as the part sat on the bed** (printer front toward you):

```
   D (back-left) ───────────── C (back-right)
   │                                       │
   │                                       │
   │                                       │
   A (front-left) ──────────── B (front-right)
```

With your calipers, measure these three, outer corner to outer corner, as accurately as you can (to 0.01 mm
if your calipers allow):

| Measure | From → to | What it is |
|---|---|---|
| **A–C** | front-left → back-right | one diagonal |
| **B–D** | front-right → back-left | the other diagonal |
| **A–D** | front-left → back-left | the left edge |

> 🎯 **This is the whole trick:** measure carefully and consistently. If there's no skew, A–C and B–D come
> out equal. The *difference* between them is exactly what the printer corrects.

---

## Step 3 — Enter them on the screen

1. On the printer: **Tune → Skew**.
2. Tap each field and type the millimetre value (the keypad has a decimal point): **A-C**, **B-D**, **A-D**.
3. Tap **Apply & Save**.

That sends `SET_SKEW` and saves it permanently (the printer restarts briefly to lock it in). Done — every
print from now on comes out square. 🎉

*(Want to confirm? Re-print the frame and re-measure the diagonals — they should now match.)*

---

## Undoing / clearing it

If you ever want to remove the correction, run this in the Klipper console (Mainsail/Fluidd) and `SAVE_CONFIG`:

```
SET_SKEW CLEAR=1
```

That zeroes the correction. Re-running Steps 1–3 overwrites it with fresh measurements any time.

---

## Enabling it

If **Tune → Skew** shows *"Skew correction isn't enabled"*, the `[skew_correction]` section is missing from
your config. Add it (it has no settings — the bare section is all that's needed):

1. Open `printer.cfg` (Mainsail **Machine** tab, or over SSH).
2. Add this line **anywhere above** the `#*# <---- SAVE_CONFIG ---->` marker:
   ```ini
   [skew_correction]
   ```
3. Save and `FIRMWARE_RESTART`.

> A Creality firmware update can reset `printer.cfg`. If the Skew button stops working after an update,
> just re-add that one line.

---

## How much can it fix?

Skew correction handles the small, consistent frame-squareness error that's baked into the machine. It does
**not** fix a part that physically rocks, a loose belt, or a bent axis — fix mechanical problems first. Think
of it as the final polish for dimensional accuracy once the hardware is sound.
