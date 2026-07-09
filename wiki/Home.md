# OpenKE — perfect prints on the Ender-3 V3 KE

So you've got a **Creality Ender-3 V3 KE** and you want it to print *really* well — clean first layers,
square parts, quiet motors, no babysitting. That's the whole point of OpenKE.

OpenKE is three things in one project:

1. **A better screen.** A fast touch UI that replaces the stock interface — full print control, an
   interactive 3D bed mesh you can spin around, and an on-screen calibration suite. It runs right on the
   printer's display (no PC, no display server). The UI is a KE-focused fork of
   [GuppyScreen](https://github.com/ballaswag/guppyscreen).
2. **The mods that actually make prints good.** Adaptive bed mesh + purge + park, Axis Twist
   Compensation, TMC Autotune, skew correction — the stuff people normally hunt down across a dozen
   GitHub repos, gathered and documented here for the KE.
3. **Guides that explain the *why*.** Not just "tap this button" — *when* to recalibrate, what each
   tool fixes, and how to chase down a specific print problem.

> **Heads-up on the name:** this project used to be called **GuppyKE** and is now **OpenKE**. Recent
> releases are tagged `vX.Y-OpenKE` (older ones were `vX.Y-GuppyKE`), and you'll still see `GuppyKE` in
> a few file names — same project. **Already running an older version?** See
> [Upgrading from an older version](Upgrading).

## New here? Do this

1. **[Install OpenKE](Installation)** — one command over SSH, keeps your config (and backs it up).
2. **[Take the screen tour](Using-GuppyKE)** — every panel, every setting, explained for newcomers.
3. **[Calibrate your printer](Calibration-Explained)** — the short path from "it prints" to "it
   prints *well*."

## Want better prints specifically?

Start with **[Calibration walkthrough (A→Z)](Calibration-Explained)**, then dig into whichever matches
your problem:

- **First layer squished on one side, lifting on the other?** →
  [Axis Twist Compensation](Axis-Twist-Compensation)
- **Want smarter bed meshing + auto purge line?** → [Adaptive Meshing](Adaptive-Print-Setup)
- **Parts come out as parallelograms / not square?** → [Skew Correction](Skew-Correction)
- **Motors loud or hot?** → [TMC Autotune](TMC-Autotune)
- **Layer shift right after a pause/resume?** → [Troubleshooting](Troubleshooting)

## What's actually in it

- Print status & control, with MCU temp on the print screen (plus chamber temp, if you've configured a
  chamber sensor)
- **Interactive 3D bed mesh** — colour height map you can rotate/zoom/pan (plus a table view)
- **On-screen calibration suite** — Axis Twist wizard, Skew Correction, TMC Autotune, live Z-offset
  baby-stepping (down to 0.001 mm), input-shaper & belt graphs
- **Fine-tune mid-print** — speed, flow, Z-offset, pressure advance, firmware retraction
- File browser (incl. USB sticks), macro/console shell, Spoolman, TMC metrics
- **WiFi Low-Latency toggle** — snappier Mainsail/screen (power-save off, roam off, Bluetooth off)
- **Buzzer beeps & songs** — real-pitch `M300`, `PLAY_TUNE` jingles, and a soft touchscreen click ([Beeps & songs](Buzzer-and-Sounds))
- **Print-state safety locks** — anything that could wreck a running job is blocked or asks first
- Power-loss recovery, and a 480×272 layout tuned for the KE (screen the right way up)

## Will it run on my printer?

OpenKE is built and verified for the **Creality Ender-3 V3 KE** specifically.

| | |
|---|---|
| Printer | Creality Ender-3 V3 KE |
| Chip / arch | Ingenic XBurst2 X2000 — **MIPS (mipsel)**, *not* aarch64 |
| Display | 480×272 |

The code can build for other boards/screens (and an x86 simulator for development), but the installer and
the on-device fixes here are written for the KE. If you have a different printer, this isn't the droid
you're looking for.

## Building it / contributing

Developer docs live in the **🛠️ Developers** section of the sidebar — start with
[Build from source](Building-from-Source) and [Architecture](Architecture).

## License & credits

GPL-3.0. The touch UI builds on [ballaswag/guppyscreen](https://github.com/ballaswag/guppyscreen) and
[probielodan/guppyscreen](https://github.com/probielodan/guppyscreen), with the 3D bed mesh from
[prestonbrown/guppyscreen](https://github.com/prestonbrown/guppyscreen). The vendored Klipper mods keep
their own upstream licenses and credits — see [Contributing](Contributing). Full credits there too.
