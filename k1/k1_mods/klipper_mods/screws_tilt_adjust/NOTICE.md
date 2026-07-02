# Screws Tilt Adjust — vendored into OpenKE

**Upstream:** [Klipper3d/klipper](https://github.com/Klipper3d/klipper), file
`klippy/extras/screws_tilt_adjust.py`
**Pinned tag:** `v0.12.0`
**License:** GPL-3.0 (Klipper's license; OpenKE is also GPL-3.0)

`screws_tilt_adjust.py` is **verbatim** from Klipper v0.12.0. Creality's stock KE firmware ships a
Klipper build with this module stripped out, so `[screws_tilt_adjust]` in a config previously failed
with "Section 'screws_tilt_adjust' is not a valid config section" — the .cfg was installed but the
module backing it was never present. We vendor the module itself for the same reason Axis Twist
Compensation is vendored (see the sibling `axis_twist_compensation/NOTICE.md`).

## Files here
- `screws_tilt_adjust.py` — the Klipper module (copied to `klippy/extras/` by the installer).
- `screws_tilt_adjust.cfg` — the `[screws_tilt_adjust]` config section with KE bed-corner screw
  positions.

Depends only on `klippy/extras/probe.py`, which already ships on the KE (also required by Axis Twist
Compensation).

To refresh from upstream, see [`docs/VENDORING.md`](../../../../docs/VENDORING.md).
