# Fixing Layer Shifts After Pause / Resume

If you **pause a print and then resume it** — for a filament change, to clear a blob, whatever — and the
print comes back **shifted sideways** (everything above the pause line is offset in the **Y** direction,
toward or away from the front of the bed), this page is for you. It's a known issue on the stock Ender-3
V3 KE configuration, it has a simple one-line fix, and it's **not** your printer being broken.

You don't need to be a programmer. If you can paste a couple of commands into a terminal, you can do this
in about five minutes.

> 💡 **Shortcut:** the OpenKE [installer](Installation) can apply this for you — choose the
> **Pause/Resume layer-shift fix** option. It backs up `gcode_macro.cfg` first and only changes the stock
> `y_park = 222`. The manual steps below are the same edit, by hand.

---

## What you'll see

- A print pauses fine, resumes fine — but from the resume point on, the layers are **shifted in Y**.
- Often a faint *clunk* when the bed parks at pause time (the bed reaching the very back of its travel).
- It only happens **after a pause/resume**, never on an uninterrupted print. That's the giveaway.

---

## Why it happens

When the KE pauses, the `PAUSE` macro **parks the bed** so the nozzle isn't sitting on your part. On the
stock config it parks at **Y = 222 mm**:

```jinja
{% set y_park = 222 %}
...
G1 X{x_park} Y{y_park} F6000      ← fast move (100 mm/s) to the very back
```

Here's the problem, and it's a chain:

1. **222 is almost the end of the rail.** The Y axis soft limit (`position_max` in `printer.cfg`) is
   **223**. So 222 is just **1 mm** short of the maximum — and the print area is only 220 mm, so 222 is
   already *past* where you'd ever print.
2. **There is no endstop at the back.** The KE's only Y endstop is at the **front** (the home position).
   The back of the axis is open — nothing physically tells the firmware "you've hit the wall."
3. **`position_max: 223` is optimistic.** That soft limit is set a hair beyond the true mechanical travel.
   So commanding `Y222` at full speed drives the **bed into the end of the rail**.
4. **The motor skips steps (or the belt slips).** Because nothing stopped the move, Klipper *thinks* it
   reached Y222, but the bed physically couldn't. Now Klipper's idea of "where Y is" and reality have
   **drifted apart**.
5. **Resume returns to the wrong place.** On resume the printer moves back to where it *thinks* the print
   was — which is now off by however many steps it lost. Everything from there on is **shifted**.

Lowering the park target by a couple of millimetres keeps the bed inside reliable travel, so it never
crashes the rail and never loses position.

> This is a **firmware/config quirk, not a hardware fault.** A printer that crashes the rail on every
> pause will eventually wear things, but the shift itself is purely the lost-steps math above.

### What this guide was tested on

| | |
|---|---|
| Printer | Creality Ender-3 V3 KE |
| Firmware | **V1.1.0.17** (also present on 1.1.0.15) |
| Symptom | Y layer shift after pause → resume |
| Stock values | `y_park = 222`, `stepper_y: position_max = 223` |
| Interface | Mainsail (Fluidd works too) |

---

## Before you start

- ⏱️ **Time:** about 5 minutes.
- 🧰 **You need:** the printer on your network and a computer. No tools, no disassembly.
- ⏸️ **Do it between prints**, not during one. The edit itself is harmless mid-print, but the restart at the
  end (`FIRMWARE_RESTART`) **would abort a running print.**
- ↩️ **Reversible:** you'll make a backup first, and the change is a single number.

---

## Connecting to your printer

Everything below is typed into your printer over **SSH** (a remote terminal). From your computer:

```sh
ssh root@<your-printer-ip>
```

Replace `<your-printer-ip>` with your printer's address (e.g. `192.168.0.231` — you can find it on the
printer's WiFi screen). Use your printer's root password (it's shown on the printer). Windows users:
download [PuTTY](https://www.putty.org/) and connect the same way.

---

## Step 1 — Back up the config

One line, so you can always roll back:

```sh
cp -a /usr/data/printer_data/config/gcode_macro.cfg \
      /usr/data/printer_data/config/gcode_macro.cfg.bak-ypark
```

## Step 2 — Change the park position

You can do this two ways. Pick **one**.

### Option A — the quick command (recommended)

This finds the `y_park = 222` line and changes it to `220`, automatically:

```sh
sed -i 's/{% set y_park = 222 %}/{% set y_park = 220 %}/' \
    /usr/data/printer_data/config/gcode_macro.cfg
```

Then confirm it took (you should see `220`):

```sh
grep -n 'y_park = 22' /usr/data/printer_data/config/gcode_macro.cfg
```

### Option B — edit by hand with `vi`

If you'd rather see it yourself:

```sh
vi /usr/data/printer_data/config/gcode_macro.cfg
```

- Press `/` , type `y_park = 222` , press **Enter** to jump to the line (inside the `[gcode_macro PAUSE]`
  section).
- Press `i` to start editing, change `222` to `220`.
- Press `Esc`, then type `:wq` and **Enter** to save and quit.

Either way you've changed this one line:

```diff
-    {% set y_park = 222 %}
+    {% set y_park = 220 %}
```

`x_park` (the X side) stays at `0` — the front/X side homes against a real endstop there and isn't part of
this problem.

> **Prefer a value that adjusts itself?** Instead of a hard-coded `220`, you can use the self-adjusting
> form (it's already in the file as a commented-out example just above the line you edited):
> ```jinja
> {% set y_park = printer.toolhead.axis_maximum.y|float - 5.0 %}
> ```
> On the KE that evaluates to 223 − 5 = **218**, a touch more margin, and it tracks `position_max`
> automatically if you ever change it. `220` and `218` both work; `220` matches the most widely reported
> fix.

## Step 3 — Restart Klipper

In the **Klipper console** (the input box in Mainsail/Fluidd), or over SSH, run:

```
FIRMWARE_RESTART
```

That's it. The next time you pause, the bed parks at 220 — comfortably inside its travel — and resume
lands back on the print exactly where it left off.

---

## How to confirm it worked

Start any print, let it lay a few layers, then **Pause** from the screen or Mainsail. Watch the bed go to
the back — it should stop smoothly with **no clunk** against the frame. **Resume**, and the next layers
should line up perfectly with what was already printed. No sideways step.

---

## Undoing it

Restore the backup from Step 1 and restart:

```sh
cp -a /usr/data/printer_data/config/gcode_macro.cfg.bak-ypark \
      /usr/data/printer_data/config/gcode_macro.cfg
```
…then `FIRMWARE_RESTART`.

---

## After a firmware update

A Creality firmware update can overwrite `gcode_macro.cfg` and put `y_park` back to **222**. If layer
shifts on pause come back after an update, just **redo Steps 1–3**. Bookmark this page.

---

## Troubleshooting

- **`grep` in Step 2 still shows `222`** — the `sed` didn't match (maybe the spacing in your file differs).
  Use **Option B** and edit the line by hand.
- **Still shifting after the fix** — confirm Klipper actually restarted (Step 3) and that
  `grep 'y_park' gcode_macro.cfg` shows `220`/`218`. If it does and you *still* shift, the lost steps may be
  happening elsewhere (e.g. printing too fast, loose Y belt, or a Z-hop/skirt issue) — that's a different
  problem from this one.
- **Where did the *clunk* come from?** That was the bed reaching the back rail end during the park. After
  the fix it shouldn't happen on pause anymore.
- **Does this touch print quality or normal printing?** No. It only changes where the bed sits **while
  paused**. Uninterrupted prints are completely unaffected.
