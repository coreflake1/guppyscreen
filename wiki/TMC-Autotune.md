# Quieter, Cooler Steppers (TMC Autotune)

OpenKE has a built-in **TMC Autotune** screen (**Tune → TMC Autotune**) that re-tunes your stepper
drivers from the *motor type* — quieter motion, cooler drivers, and a touch smoother surface, applied
automatically on every boot. You pick the motor and a goal from two dropdowns and tap Save. No tuning
math, no datasheets.

The OpenKE installer sets up everything this needs, so on a normal install the button is **already
enabled** — skip to [Pick your motors and tune](#pick-your-motors-and-tune). If your button is greyed
out, the section just below explains why and how to fix it in one step.

---

## If the button is greyed out

Autotuning is done by a small **Klipper add-on** that doesn't come with Creality's firmware. OpenKE
checks for it at startup — specifically `klippy/extras/motor_database.cfg` — and **only un-greys the
button when it's present.** The installer puts it there for you, so normally it's enabled.

If it's still greyed out, you either declined the print-quality mods at install time, or a firmware
update wiped them. The fix is one step: **re-run the OpenKE [installer](Installation)**, answer **Y** at
the mods prompt (it's safe to run again), then **Restart Klipper**. Done. *(If it shuts down with a
`serialqueue … NoneType` error, that's a harmless KE restart race — just restart once more, see
[Troubleshooting](Troubleshooting).)*

A couple of things this is *not*:

- It is **not** related to **TMC Metrics** (the live driver dashboard) — separate button, separate
  feature. You don't need metrics enabled.
- It does **not** need a print running. Autotune works at idle.

(Want to know exactly what the installer placed, or set it up by hand? See
[Set it up manually](#set-it-up-manually-advanced) at the bottom.)

---

## Will this actually help me? (Honest answer for the KE)

**Yes, but moderately.** The Ender-3 V3 KE uses **TMC2208** drivers on X, Y, and Z. The 2208 supports the
chopper-timing and stealthChop/PWM tuning that autotune computes from the motor specs — so you *will* get
**quieter, cooler, slightly smoother** motors. That part is real.

What the 2208 *doesn't* have is **CoolStep** and **StallGuard** (those are 2209-and-up features). So:

- You won't see a **"Sensorless Threshold"** field on the KE. The panel only shows it for drivers that
  support StallGuard (2209/2130/2240/2660/5160); on the 2208 it's hidden. So you've nothing to set there.
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
> firmware update, see [After a firmware update](#after-a-firmware-update) at the bottom.

### What this guide was tested on

| | |
|---|---|
| Printer | Creality Ender-3 V3 KE |
| Firmware | **V1.1.0.15** |
| Drivers | **TMC2208** on X / Y / Z (extruder is not TMC) |
| Interface | Mainsail (Fluidd works too) |

---

## What the installer did

When you ran the OpenKE [installer](Installation) and said **Y** to the print-quality mods, it copied the
autotune add-on (three files) into Klipper **and** shipped the `[guppy_config_helper]` line that lets the
on-screen **Save** button work. No SSH, no downloads — the **Tune → TMC Autotune** button is enabled and
the screen lists each stepper (X, Y, Z) with two dropdowns.

> **Which add-on?** OpenKE vendors [evgarthub/klipper_tmc_autotune_k1](https://github.com/evgarthub/klipper_tmc_autotune_k1)
> — a version of the popular [Klipper TMC Autotune](https://github.com/andrewmcgr/klipper_tmc_autotune)
> adapted for the Creality K1/KE. The stock upstream assumes a Raspberry Pi layout and won't load cleanly
> on the KE.

---

## Pick your motors and tune

![TMC Autotune panel](images/tmc-autotune.png)

For each axis, choose the **Motor** from the dropdown, then a **Tuning Goal**, then tap **Save/Restart**.

**Recommended on the KE** (these are the stock motors):

| Stepper | Motor | Tuning Goal |
|---------|-------|-------------|
| `stepper_x` | `creality-42-34` | **performance** |
| `stepper_y` | `creality-42-34` | **performance** |
| `stepper_z` | `creality-42-40` | **silent** |

The Tuning Goal dropdown has **four** choices:

- **`auto`** *(the default)* — picks for you based on the motor: **performance** for X/Y, **silent** for Z.
  On the KE this lands on exactly the recommended table above, so `auto` is a perfectly good "just do the
  right thing" choice.
- **`performance`** — spreadCycle at all speeds (crisp, accurate motion, a bit more audible). Good for the
  moving X/Y.
- **`silent`** — stealthChop at all speeds (quietest). Ideal for the Z lead-screw, which doesn't need the
  precision.
- **`autoswitch`** — **experimental.** Runs stealthChop at low speed and switches to spreadCycle once the
  motor moves fast enough. The idea is "quiet when slow, strong when fast." It's flagged experimental in the
  add-on and isn't needed on the KE — stick with `auto`/`performance`/`silent` unless you're deliberately
  experimenting.

You'll see just the two dropdowns per axis — there's no "Sensorless Threshold" field on the KE, because
the 2208 drivers don't have StallGuard (the panel hides it).

Tap **Save/Restart**. Klipper writes your choice into its auto-save block, restarts, and applies the tuned
driver registers. From now on it re-applies them automatically on every boot — set once and forget.

> The motor names come from the bundled motor database. If you've swapped to non-stock motors, pick the
> closest match in the dropdown (or look your motor up in `motor_database.cfg`).

---

## Undoing it

To turn a single axis back to stock, open its entry on the screen, set **Motor** back to *"Not
Configured,"* and tap **Save/Restart** — OpenKE removes that `[autotune_tmc]` section for you.

To remove the add-on entirely, over SSH:

```sh
rm /usr/share/klipper/klippy/extras/autotune_tmc.py \
   /usr/share/klipper/klippy/extras/motor_constants.py \
   /usr/share/klipper/klippy/extras/motor_database.cfg
```

…then delete any `[autotune_tmc ...]` sections from `printer.cfg` (they may live in the
`#*#` auto-save block at the bottom) and `FIRMWARE_RESTART`. The button goes back to greyed-out, and your
drivers return to Creality's defaults.

---

## After a firmware update

A Creality firmware update wipes the three add-on files (they live on the system partition). Putting it
back takes a couple of minutes: **re-run the OpenKE [installer](Installation)** and answer **Y** at the
mods prompt, then `FIRMWARE_RESTART`. Your motor/goal choices are stored in `printer.cfg`, so they come
back on their own — just open **Tune → TMC Autotune** to confirm.

> Bookmark this page and revisit it after **every** Creality firmware update.

---

## Set it up manually (advanced)

The installer does all of this for you. If you'd rather do it by hand (or you want to know exactly what
the installer changed), here are the steps.

<details>
<summary>Manual steps</summary>

Over SSH (`ssh root@<printer-ip>`):

**1. Install the add-on** — three files into Klipper's add-on folder. **Use the exact commit OpenKE
vendors and tests against**, not the upstream `main` branch — `main` can move on without notice and end
up different from what this page (and the installer) was actually verified with:

```sh
BASE="https://raw.githubusercontent.com/evgarthub/klipper_tmc_autotune_k1/1cafcf42bfb7aa1985cfd35f3bf7e83f54b0c3d2"
DEST="/usr/share/klipper/klippy/extras"
for f in autotune_tmc.py motor_constants.py motor_database.cfg; do
  wget --no-check-certificate "$BASE/$f" -O "$DEST/$f"
done
```

All three are required; `motor_database.cfg` is read automatically (no `[include]` needed).

**2. Enable the Save button** — add `[guppy_config_helper]` on its own line to
`/usr/data/printer_data/config/GuppyScreen/guppy_cmd.cfg` (OpenKE now ships this by default, so you may
already have it). Without it, the on-screen Save fails with "Unknown command".

**3. Restart** — **Restart Klipper** (Mainsail → *Restart*, or `FIRMWARE_RESTART`). The button lights up.
If Klipper shuts down with a `serialqueue` error on the first try, restart once more — it's a harmless KE
host-MCU race ([Troubleshooting](Troubleshooting)).

</details>

---

## Troubleshooting

- **Button still greyed out** — Klipper didn't load the add-on. Re-run the [installer](Installation)
  (answer **Y** at the mods prompt) and `FIRMWARE_RESTART`. If it persists, check the Klipper log for an
  error mentioning `autotune_tmc` or `motor_constants`.
- **Tapping Save does nothing / "Unknown command _GUPPY_SAVE_CONFIG"** — `[guppy_config_helper]` is
  missing from `GuppyScreen/guppy_cmd.cfg`. Re-running the installer adds it; then restart.
- **Config error about `motor_constants`** — you have `autotune_tmc.py` but not `motor_constants.py`
  (e.g. a partial manual install). Re-run the installer, which copies all three.
- **Where's the Sensorless Threshold field?** — it isn't shown on the KE. OpenKE only displays it for
  drivers with StallGuard (2209 and up); the KE's TMC2208 drivers don't have it, so the panel hides it.
  Nothing's broken — there's just nothing to set there.
