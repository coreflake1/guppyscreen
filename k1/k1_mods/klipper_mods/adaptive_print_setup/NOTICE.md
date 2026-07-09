# Adaptive Print Setup — provenance

This started as a vendored copy of [kyleisah/Klipper-Adaptive-Meshing-Purging](https://github.com/kyleisah/Klipper-Adaptive-Meshing-Purging)
("KAMP", GPL-3.0, pinned at `b0dad8ec9ee31cb644b94e39d4b8a8fb9d6c9ba0`). It no longer is — KAMP
itself now recommends Klipper's native adaptive meshing over its own macro-based implementation
(now that upstream Klipper has absorbed the feature), and OpenKE didn't want a dependency on a
package it wasn't tracking or planning to sync from again. Current state per file:

- **`modules/Adaptive_Meshing.cfg`** — original OpenKE code, not derived from KAMP. Delegates to
  Klipper's native `BED_MESH_CALIBRATE ADAPTIVE=1` (see `patch_bed_mesh.py`, which ports that
  upstream Klipper feature onto the KE's older Klipper fork, which predates the merge).
- **`modules/Smart_Park.cfg`** and **`modules/Line_Purge.cfg`** — still adapted from KAMP
  (structurally near-verbatim; only the settings-macro reference was renamed from `_KAMP_Settings`
  to `_OPENKE_ADAPTIVE_SETTINGS`, plus a couple of console-message wording tweaks). Full credit to
  kyleisah and contributors; still GPL-3.0, compatible with OpenKE's own GPL-3.0 (see the repo's
  root `LICENSE` — not duplicated here since the whole project is already under it).
- **`Settings.cfg`** — replaces KAMP's `_KAMP_Settings` with `_OPENKE_ADAPTIVE_SETTINGS`. Same
  variables (mesh margin, purge, park, dockable-probe support), minus `fuzz_amount` (probe-point
  randomization to spread nozzle-probe wear — no equivalent once meshing moved to Klipper's native
  code, and not relevant to this printer's probe anyway).

The installer migrates an existing KAMP install's settings values into `Settings.cfg` automatically
(`migrate_settings.py`) and removes the old KAMP files - no manual steps needed on upgrade.
