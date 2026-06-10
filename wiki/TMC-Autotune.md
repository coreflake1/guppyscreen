# Quieter, Cooler Steppers (TMC Autotune)

GuppyKE has a built-in **TMC Autotune** screen (**Tune → TMC Autotune**) that re-tunes your stepper
drivers from the *motor type* — quieter motion, cooler drivers, and a touch smoother surface, applied
automatically on every boot. You pick the motor and a goal from two dropdowns and tap Save. No tuning
math, no datasheets.

But there's a catch a lot of people hit after updating to 0.5: **the TMC Autotune button is greyed out.**
That's expected on a fresh install — and this page explains why and walks you through enabling it. You
don't need to be a programmer; if you can paste commands into a terminal, you can do this.

---

## Why is the button greyed out?

The 0.5 update ships the on-screen **panel**, but autotuning itself is done by a small **Klipper add-on**
that doesn't come with Creality's firmware. GuppyKE checks for that add-on at startup — specifically the
file `klippy/extras/motor_database.cfg` — and **only un-greys the button when it's present.** No add-on,
no button.

A few things this is *not*:

- It is **not** related to **TMC Metrics** (the live driver dashboard) — that's a separate button and a
  separate feature. You do **not** need metrics or diagnostics enabled.
- It does **not** need a print running or any print history. Autotune works at idle.

So: install the add-on (below), restart Klipper, and the button lights up.

---

## Will this actually help me? (Honest answer for the KE)

**Yes, but moderately.** The Ender-3 V3 KE uses **TMC2208** drivers on X, Y, and Z. The 2208 supports the
chopper-timing and stealthChop/PWM tuning that autotune computes from the motor specs — so you *will* get
**quieter, cooler, slightly smoother** motors. That part is real.

What the 2208 *doesn't* have is **CoolStep** and **StallGuard** (those are 2209-and-up features). So:

- The **"Sensorless Threshold"** field on the autotune screen does nothing on the KE — it's only used by
  drivers with StallGuard. Leave it alone.
- Don't expect a transformative change. Think "noticeably calmer and cooler," not "a different printer."

If your motors are already quiet and you have no thermal concerns, this is a nice-to-have, not a must-do.

---

## Before you start

- ⏱️ **Time:** about 10 minutes. Most of it is one paste and a restart.
- 🧰 **You need:** the printer on your network and a computer. No tools, no disassembly, no print.
- ↩️ **Safe to try:** everything here is reversible (see [Undoing it](#undoing-it)) and changes nothing
  about how your printer moves until you pick a motor and tap Save.

> ### ⚠️ One important warning — read this
> This add-on lives in the printer's system files. **A Creality firmware update will erase it.** Not a
> disaster — redoing it takes a couple of minutes — but it's why you should bookmark this page. After any
> firmware update, run the [Quick reinstall](#quick-reinstall-after-a-firmware-update) at the bottom.

### What this guide was tested on

| | |
|---|---|
| Printer | Creality Ender-3 V3 KE |
| Firmware | **V1.1.0.15** |
| Drivers | **TMC2208** on X / Y / Z (extruder is not TMC) |
| Interface | Mainsail (Fluidd works too) |

---

## Connecting to your printer

Everything below is typed into your printer over **SSH** (a remote terminal). From your computer:

```sh
ssh root@<your-printer-ip>
```

Replace `<your-printer-ip>` with your printer's address (e.g. `192.168.0.231` — you can find it on the
printer's WiFi screen). Use your printer's root password. Windows users: download
[PuTTY](https://www.putty.org/) and connect the same way.

We'll also run one command in the **Klipper console** — that's the input box in Mainsail/Fluidd (the web
page you open in a browser to control the printer). We'll say "in the console" when that's the case.

---

## Step 1 — Install the autotune add-on

This downloads **three small files** into Klipper's add-on folder. They teach Klipper how to compute and
apply driver settings (the third one is a database of common motors so you don't need any datasheets).
Paste this whole block into SSH:

```sh
BASE="https://raw.githubusercontent.com/evgarthub/klipper_tmc_autotune_k1/main"
DEST="/usr/share/klipper/klippy/extras"
for f in autotune_tmc.py motor_constants.py motor_database.cfg; do
  wget --no-check-certificate "$BASE/$f" -O "$DEST/$f"
done
```

> **Why this fork?** `evgarthub/klipper_tmc_autotune_k1` is a version of the popular
> [Klipper TMC Autotune](https://github.com/andrewmcgr/klipper_tmc_autotune) project specifically adapted
> for the Creality K1/KE (correct paths, no desktop-Linux service checks). The stock upstream version
> assumes a Raspberry Pi layout and won't load cleanly on the KE.

All three files are required. If you install only `autotune_tmc.py` you'll get a config error about a
missing `motor_constants` module on the next restart — that's the second file. `motor_database.cfg` is
read automatically from the same folder; you do **not** add an `[include]` for it.

## Step 2 — Let the on-screen Save button work

GuppyKE's autotune screen saves your choice by sending a `_GUPPY_SAVE_CONFIG` command to Klipper. On a
stock GuppyKE install that command isn't registered until you add one line. Open your GuppyScreen command
config:

```
/usr/data/printer_data/config/GuppyScreen/guppy_cmd.cfg
```

(easiest in Mainsail: **Machine** tab → open `GuppyScreen/guppy_cmd.cfg`). Add this section on its own
line, anywhere in the file:

```ini
[guppy_config_helper]
```

Save the file. Without this, the autotune screen renders fine and lets you pick a motor, but tapping
**Save/Restart** silently fails with an "Unknown command" error in the log.

## Step 3 — Restart, and the button lights up

In the **Klipper console**:

```
FIRMWARE_RESTART
```

When GuppyKE reconnects, go to **Tune → TMC Autotune**. The button is now enabled, and the screen lists
each stepper (X, Y, Z) with two dropdowns.

---

## Step 4 — Pick your motors and tune

For each axis, choose the **Motor** from the dropdown, then a **Tuning Goal**, then tap **Save/Restart**.

**Recommended on the KE** (these are the stock motors):

| Stepper | Motor | Tuning Goal |
|---------|-------|-------------|
| `stepper_x` | `creality-42-34` | **performance** |
| `stepper_y` | `creality-42-34` | **performance** |
| `stepper_z` | `creality-42-40` | **silent** |

- **`performance`** keeps the motor in spreadCycle (crisp, accurate motion) — good for the moving X/Y.
- **`silent`** uses stealthChop (quietest) — ideal for the Z lead-screw, which doesn't need the precision.
- **`auto`** picks sensible defaults and, on the KE, lands on essentially the same result.
- Leave **Sensorless Threshold** untouched — it's inert on the KE's 2208 drivers (see above).

Tap **Save/Restart**. Klipper writes your choice into its auto-save block, restarts, and applies the tuned
driver registers. From now on it re-applies them automatically on every boot — set once and forget.

> The motor names come from the bundled motor database. If you've swapped to non-stock motors, pick the
> closest match in the dropdown (or look your motor up in the database file from Step 1).

---

## Undoing it

To turn a single axis back to stock, open its entry on the screen, set **Motor** back to *"Not
Configured,"* and tap **Save/Restart** — GuppyKE removes that `[autotune_tmc]` section for you.

To remove the add-on entirely, over SSH:

```sh
rm /usr/share/klipper/klippy/extras/autotune_tmc.py \
   /usr/share/klipper/klippy/extras/motor_constants.py \
   /usr/share/klipper/klippy/extras/motor_database.cfg
```

…then delete any `[autotune_tmc ...]` sections from `printer.cfg` (they may live in the
`#*#` auto-save block at the bottom), remove the `[guppy_config_helper]` line you added in Step 2 if you
want, and `FIRMWARE_RESTART`. The button goes back to greyed-out, and your drivers return to Creality's
defaults.

---

## Quick reinstall (after a firmware update)

A Creality firmware update wipes the three add-on files (they live on the system partition). To put it
back in a couple of minutes:

1. SSH in and redo **Step 1** (the three-file download).
2. Check `[guppy_config_helper]` is still in `GuppyScreen/guppy_cmd.cfg` (Step 2) — your config usually
   survives, but confirm it.
3. `FIRMWARE_RESTART`. Your motor/goal choices are stored in `printer.cfg`, so they come back on their own
   — just open **Tune → TMC Autotune** to confirm.

> Bookmark this page and re-run it after **every** Creality firmware update.

---

## Troubleshooting

- **Button still greyed out after Step 3** — Klipper didn't load the add-on. In Mainsail, check the
  Klipper log/console for an error mentioning `autotune_tmc` or `motor_constants`. The usual cause is a
  missing file from Step 1 (all three are required) or a download that didn't complete. Re-run Step 1 and
  `FIRMWARE_RESTART`.
- **Tapping Save does nothing / "Unknown command _GUPPY_SAVE_CONFIG"** — you skipped Step 2. Add
  `[guppy_config_helper]` to `GuppyScreen/guppy_cmd.cfg` and restart.
- **I see a config error about `motor_constants`** — you have `autotune_tmc.py` but not
  `motor_constants.py`. Re-run the Step 1 block (it grabs all three).
- **The Sensorless Threshold number won't do anything** — correct; it's unused on the KE's TMC2208
  drivers. Ignore it.
