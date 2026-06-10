# v0.5.5-GuppyKE — WiFi "Low Latency" mode + TMC Autotune docs

A focused follow-up to `v0.5.0-GuppyKE`. The headline is a much stronger
WiFi latency control, plus a proper user guide for TMC Autotune.

## Highlights

- **WiFi "Low Latency" toggle (expanded + renamed).** The v0.4 "WiFi
  power-saving" button now drives a whole bundle behind a single on-screen
  switch, and is renamed **Low Latency** (positive framing: ON = good):
  - WiFi power-save off (`wl PM 0`)
  - radio kept awake when idle (`wl mpc 0`)
  - background roam-scans off (`wl roam_off 1`)
  - **Bluetooth stopped** — on the KE, WiFi and Bluetooth are a single
    Broadcom **BCM4343 combo chip sharing one 2.4 GHz radio and antenna**.
    The (unused) BT stack makes WiFi yield to it via coexistence, which
    injects latency spikes. Measured on-device: with BT running, an idle,
    −35 dBm, 72 Mbps link still spiked to 480–900 ms; stopping BT cut
    worst-case latency ~4–6×.

  The setting **persists and re-applies across reboots and network
  switches** (the driver resets these on every link-up, so GuppyScreen
  re-asserts them on each `CONNECTED` event and at startup). Turning it
  **off** restores stock behaviour and **restarts Bluetooth**. All of this
  lives in the screen binary — no external scripts. (`wme_apsd` is left
  alone on purpose: changing it needs `wl down`, which would drop the link.)

- **TMC Autotune user guide.** New [wiki/TMC-Autotune.md](wiki/TMC-Autotune.md)
  explains the single most common 0.5 question: **why the TMC Autotune
  button is greyed out** (the Klipper-side add-on isn't installed — it's
  gated on `klippy/extras/motor_database.cfg`), how to install the 3-file
  K1 autotune fork, the `[guppy_config_helper]` line the on-screen Save
  needs, honest KE expectations (TMC2208: real but moderate; no Sensorless
  Threshold field), and all four tuning goals (auto/silent/performance/
  autoswitch).

## Fixes / polish

- Low Latency hint text shrunk to `montserrat_12` and **left-aligned**
  under the button so it no longer collides with the floating Back button.
- TMC Autotune doc corrections: the Sensorless Threshold field is **hidden**
  on the KE's TMC2208 (not shown-but-inert), and all four tuning goals are
  now documented (autoswitch was missing).

## Docs

- README, Home, Using-GuppyKE, and Troubleshooting updated to describe the
  Low Latency toggle and what it disables.
- Added a Troubleshooting row for laggy Mainsail / stuttering webcam.

## Breaking changes

None. The `/wifi_low_latency` config key is unchanged, so an existing
saved preference carries over (the button just reads "Low Latency" now).

## Notes

- After a **cold boot**, Bluetooth is briefly up (the boot script starts
  it) until GuppyScreen's startup re-assert stops it within ~1 minute. The
  WiFi knobs apply immediately.
- Channel choice and strong nearby 2.4 GHz emitters (e.g. a neighbouring
  printer's Wi-Fi-Direct) are off-device factors this toggle can't fix —
  see the Troubleshooting note.

## Upgrade

In-place from the screen: **Settings → Update Guppy**. No config migration
required.

## Included commits (since v0.5.0-GuppyKE)
- `000af2c feat(wifi): expand the low-latency toggle into a full bundle (incl. Bluetooth)`
- `bd7c928 feat(wifi): rename the toggle "Low Latency" (was "WiFi power saving")`
- `f06adcd fix(wifi): shrink Low Latency hint font (montserrat 14->12)`
- `6efa996 fix(wifi): left-align the Low Latency hint under the button`
- `6f72a7d docs: add TMC Autotune user guide (explains the greyed-out button)`
- `b3f53a9 docs(tmc-autotune): correct the Sensorless Threshold note`
- `446e4a9 docs(tmc-autotune): document all four tuning goals`
- `f163d77 docs: explain the WiFi "Low Latency" toggle`
