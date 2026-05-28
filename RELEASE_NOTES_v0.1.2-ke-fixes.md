# v0.1.2-ke-fixes — Extruder UX, print status, spinner consistency

Second KE-focused improvement batch on top of `v0.1.1-ke-gui-fixes`. All
changes are on branch `ke-next`; tag/PR to `main` will be made when the
on-device verification checklist below is closed out.

## Highlights
- **Extruder panel completely reworked.** The old M109 blocking path could
  hang the panel; mashing Load/Extrude could stack multiple extrusions; a
  hotter manual target could be silently lowered. All three are fixed, and
  the UX is now press-once-and-wait: press Extrude/Retract cold, the panel
  heats, and the extrude fires automatically when temp is reached. Cooldown
  stays clickable during heat-up as the cancel.
- **Print status panel shows the G-code filename** above the progress bar
  and computes a **dynamic time-remaining** from elapsed/progress (catches
  fast prints overshooting the slicer estimate).
- **Print panel auto-dismisses** when a print finishes instead of leaving
  the user staring at a "done" panel with no obvious next step.
- **All four busy spinners (extruder, belts, input shaper, wifi) now
  match** — same 80×80 size, same animation. Wifi previously had no
  explicit size at all and was rendering at LVGL's tiny default.
- **LED is back on the home screen and Fine Tune is back on the Tune tab.**
  The print-state gating from `57b165f` is preserved; only the button
  swap is reverted.

## Fixes
- **#65** Blocking M109 could hang the extruder panel indefinitely if the
  hotend stalled. Replaced with non-blocking M104 + tolerance gate on the
  live temperature stream.
- **#90 / #116** Tapping Load/Extrude repeatedly could queue multiple
  extrusions. Action buttons are now locked from gcode dispatch until the
  Moonraker JSON-RPC response arrives (the real "done" signal), with a
  5-minute safety unlock if the websocket dies mid-script.
- **#117** Pressing Unload/Load while the user had manually set a hotter
  hotend target would lower it. `effective_temp() = max(selected_in_UI,
  current_target)` is now used for all macro EXTRUDER_TEMP values.
- **#94** Print status panel didn't dismiss when a print finished. Now
  backgrounds itself on the printing/paused → complete/standby/cancelled/
  error transition.
- **#126 (parts 1 & 3)** No filename was shown on the print view, and the
  time-remaining was a static slicer estimate that drifted on fast prints.
  Filename label added; ETA is now dynamic once progress > 5%.
- **#48** No visible indicator that a load/unload was running. Centered
  80×80 spinner shows during heating and in-flight gcode; same animation
  as the calibration spinners.

## Other improvements
- **Spinner consistency** — all four (extruder, belts calibration, input
  shaper X+Y, wifi) normalized to 80×80. Wifi gets an explicit `set_size`
  for the first time.
- **Default monitored_sensors** — fresh installs ship with extruder /
  heater_bed / mcu_temp pre-configured instead of an empty list.
- **WebSocket plumbing** — new `gcode_script(script, cb)` overload on
  `KWebSocketClient` for response-driven flows.
- **Toast helper** — new `KUtils::notify_toast()` for transient
  non-blocking status messages.
- **SIMULATOR mock data** — print status, extruder, and spoolman panels
  each ship a `sim_setup_mock_data()` (under `#ifdef SIMULATOR`) so the
  new UI is verifiable from `build-sim/bin/guppyscreen` without a live
  Moonraker connection. Zero impact on the MIPS production build.

## Issues verified as already fine (no code change)
Inspected against the current ke-next tree:

- **#7** screen auto-off — `display_sleep_sec` works; the DSI-wake bug
  was fixed in earlier ke-next commits. Caveat: backlight stays on (the
  Ingenic X2000 DSI panel can't be safely power-cycled), it's a dark
  overlay rather than a true off.
- **#91** font width — KE small-screen layout/font pass already addresses
  the K1-reported clipping for this fork.
- **#102** display_rotate:2 + touch — works in the default config; the
  touch-calibration path has a latent double-rotation bug but the
  calibration step isn't reachable from the UI.
- **#28** Z+/- arrow icons — already swapped on panel foreground.
- **#66** Home All double-homing — single `G28` call in code; doubling is
  Klipper-side macro behavior.
- **#95** file list refresh — `notify_filelist_changed` already handled.
- **#132** load extrude length — already passed from the length selector.
- **#161** default 240C low — `default_extruder_temp` is user-settable in
  Settings; selector covers the full usable KE range (180–300°C).

## Not addressed
- **#41** UI redesign request — out of scope; this fork keeps the Guppy
  UI shape.
- **#47** print-status layout bugs from KE — superseded by the layout pass
  already on `ke-next`.
- **#106** Cyrillic/CJK filenames render as squares — would need a Unicode
  font fallback (~200 KB of glyphs); deferred.
- **#32** Moonraker authentication — deferred for a later batch.

Issues at the system/installer/Klipper level (#160 SSH password, #155 /
#108 USB access, #143 Wi-Fi, #135 belts warnings, #81 input shaper disk
space, #64 TMC autotune, #39 / #58 / #59 "stuck on init") are outside the
guppyscreen codebase and aren't tracked here.

## Breaking changes
None. Existing configs work unchanged.

## On-device verification status
At time of writing, verified visually on the physical Ender-3 V3 KE:
- Filename label rendering between thumbnail and progress bar.
- LED/Fine Tune positions restored.
- Spinner consistency.

Pending on-device confirmation (next real print + filament change):
- Auto-dismiss when a print completes (#94).
- Press-once auto-extrude on temp.
- 5-minute safety unlock if a script never returns.

## Upgrade notes
- No config migration required.
- The MIPS binary lives at `/usr/data/guppyscreen/guppyscreen` on the
  printer. Replace using the standard supervise-daemon-aware flow (scp as
  `.new`, stop, mv, chmod 700, start) — see [wiki/Deployment-and-Updates.md](wiki/Deployment-and-Updates.md)
  if present.

## Included commits
- `2bffc7a sim: mock data hooks for visual verification`
- `a1f4199 revert: LED on home, Fine Tune in Tune tab`
- `2595f74 ui: normalize busy spinners to 80x80`
- `75fe926 feat(print_status): filename label, dynamic ETA, auto-dismiss on done`
- `ac9bd72 feat(extruder): non-blocking heat, auto-extrude on temp, busy spinner`
- `ec8d24d feat(utils): add KUtils::notify_toast for transient feedback`
- `0075f33 feat(ws): add gcode_script(script, cb) response-callback overload`
