# Axis Twist Compensation — vendored into OpenKE

**Upstream:** [Klipper3d/klipper](https://github.com/Klipper3d/klipper), file
`klippy/extras/axis_twist_compensation.py`
**Pinned tag:** `v0.12.0`
**License:** GPL-3.0 (Klipper's license; OpenKE is also GPL-3.0)

`axis_twist_compensation.py` is **verbatim** from Klipper v0.12.0. The KE's stock firmware ships an
older Klipper that predates this module, so we vendor it rather than depend on the user's Klipper
version.

## Files here
- `axis_twist_compensation.py` — the Klipper module (copied to `klippy/extras/` by the installer).
- `patch_probe.py` — idempotent patcher that wires `klippy/extras/probe.py` to call the module during
  probing (Klipper needs this one-line graft; v0.12.0's probe.py isn't on the KE). Safe to re-run;
  exits "STOP" without changing anything if the firmware's probe.py differs.
- `axis_twist_compensation.cfg` — the `[axis_twist_compensation]` config section with KE bed bounds.

> **Why not the popular `.patch` file?** The widely-shared Reddit patch only applies to one exact
> firmware (V1.1.0.14) and silently fails elsewhere. `patch_probe.py` does the same edit but adapts to
> the file and refuses to half-apply.

To refresh from upstream, see [`docs/VENDORING.md`](../../../../docs/VENDORING.md).
