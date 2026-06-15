# TMC Autotune — vendored into OpenKE

**Upstream:** [evgarthub/klipper_tmc_autotune_k1](https://github.com/evgarthub/klipper_tmc_autotune_k1)
(a K1/KE-targeted fork of [andrewmcgr/klipper_tmc_autotune](https://github.com/andrewmcgr/klipper_tmc_autotune))
**Pinned commit:** `1cafcf42bfb7aa1985cfd35f3bf7e83f54b0c3d2`
**License:** GPL-3.0 (see [`LICENSE`](LICENSE))

Computes optimal TMC stepper-driver registers from the motor's physical constants at every boot —
quieter, cooler, often smoother motion. The `_k1` fork adapts upstream for the Creality K1/KE stack.

## Files here (all verbatim upstream)
- `autotune_tmc.py` — the Klipper module (copied to `klippy/extras/`).
- `motor_constants.py` — motor-constant helpers.
- `motor_database.cfg` — known motor specs.

## Activation
The on-screen TMC Autotune button stays greyed out until `[guppy_config_helper]` is present so it can
save. OpenKE now ships that section in `GuppyScreen/guppy_cmd.cfg`, so no manual edit is needed.

To refresh from upstream, see [`docs/VENDORING.md`](../../../../docs/VENDORING.md).
