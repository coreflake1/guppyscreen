# Upgrading from an older version

Already running an older **GuppyKE** (or plain GuppyScreen) on your KE? Here's what changed and exactly
what to do. **Short version: updating is safe and your calibrations are kept.**

## v1.2.0 — the hardware H.264 camera stream was removed (auto-handled)

`v1.2.0-OpenKE` **removes the optional H.264 camera stream** (the go2rtc add-on). On the KE's tiny
197 MB box, the go2rtc + ffmpeg stack (~44 MB) is the main driver of memory-pressure freezes — the
"screen goes dark, SSH dies, only a power-cycle recovers" symptom. Dropping it and going back to the
stock camera is the reliable fix.

**You don't have to do anything.** When you update to v1.2.0 (on-screen **Update Guppy**, or re-running
the installer), it **automatically removes go2rtc** if it's installed — stops the service, deletes
`/usr/data/h264cam/` and `/etc/init.d/S96h264cam`, and removes the "Nebula H264" webcam from Mainsail.
Your **stock camera and persistent image tuning are untouched** (the stock feed was never modified by the
H.264 add-on). After it's gone you'll have ~44 MB more free RAM. If you relied on the H.264 stream in an
external app (Frigate, Home Assistant, Obico…), point that app at the stock camera instead.

## First, the name: GuppyKE → OpenKE

Same project, new name. Older releases were tagged `vX.Y-GuppyKE`; new ones are `vX.Y-OpenKE`. You'll
still see `GuppyKE` in a few file names and older messages — it's the same thing. The repo URL is
**unchanged** (`coreflake1/guppyscreen`), so your in-app updater keeps working without any action.

## Will I lose my settings or have to recalibrate?

**No.** Your Z-offset, bed mesh, input shaper, skew, axis-twist, and TMC-autotune values live in
`printer.cfg`'s `SAVE_CONFIG` block (and `variables.cfg`). Updating or re-running the installer **never
rewrites those** — it only adds includes and copies files. (We verified this on a live KE: after a full
run, every calibration value was byte-for-byte identical.) The installer also drops a timestamped
`printer.cfg` backup in `/usr/data/guppyify-backup/` on every run, just in case.

## How to update

**You're on v0.3.0 or newer** → update right from the screen: **Settings → Update Guppy**. It pulls the
latest release and restarts. Done.

**You're on something older, or not sure** → re-run the one-line install command over SSH (see
[Installation](Installation)). It keeps your config and replaces the updater with the fixed one; after
that, the on-screen **Update Guppy** button works for every future release.

> **Special case — v0.2.0:** its built-in updater points at the wrong repository, so the on-screen button
> can't fetch releases. Just re-run the install command **once** — it keeps your printer config and fixes
> the updater. After that you're on the normal update path.

## After updating: grab the newly-bundled extras (optional)

Recent OpenKE folds what used to be manual add-ons into the installer, under a prompt
(**install all / skip all / choose each**): KAMP, Axis Twist Compensation, TMC Autotune, Skew Correction,
the Creality Nebula camera (persistent image tuning), the Pause/Resume layer-shift fix,
and the Creality macros (M600, Save Z-Offset, useful macros, Exclude Object). See
[Installation](Installation) for the full list.

> **Buzzer beeps & songs — a one-time installer re-run (only if you're crossing this version).** Buzzer
> sounds (real-pitch `M300`, `PLAY_TUNE`, the soft touch click) first ship in **`v1.1.0-OpenKE`**. This
> note **only** applies if you're upgrading from a release **older than that** — and only because the
> on-screen **Update Guppy** button swaps the screen binary but doesn't touch your Klipper config dir,
> where the `M300`/`PLAY_TUNE` macros and default `songs.conf` live. **Re-run the
> [installer](Installation) once** (safe, keeps your config) to pick them up.
>
> **Going forward this is a non-issue:** fresh installs already include everything, and once you're on
> `v1.1.0-OpenKE` or newer your `songs.conf` is preserved on every future install/update. Same
> "config-side features come from the installer" rule as the items above.

- **Already set these up yourself** — by hand or via the **Creality Helper Script**? You don't have to do
  anything. And if you *do* re-run the installer, it **detects what you already have and skips it** — no
  duplicate sections, nothing overwritten, your working setup left intact.
- **Want something you don't have yet?** Re-run the installer and pick it at the prompt. Some features
  need a one-time calibration afterward (the guide for each walks you through it).

## If Klipper shuts down right after updating

On the KE, the *first* restart after a config change occasionally shuts down with a
`serialqueue … NoneType` error. It's a harmless host-MCU reconnect race, **not** a config problem — just
restart Klipper again (a full **Restart Klipper** is more reliable than a bare `FIRMWARE_RESTART`), and a
cold boot always clears it. See [Troubleshooting](Troubleshooting).

## Coming from upstream GuppyScreen (ballaswag / probielodan)?

Just run the OpenKE [installer](Installation). It swaps in the KE build and adds the KE-specific setup,
while keeping your `printer.cfg` and calibrations. From then on you're on the OpenKE update track.
