# KAMP — vendored into OpenKE

**Upstream:** [kyleisah/Klipper-Adaptive-Meshing-Purging](https://github.com/kyleisah/Klipper-Adaptive-Meshing-Purging)
**Pinned commit:** `b0dad8ec9ee31cb644b94e39d4b8a8fb9d6c9ba0`
**License:** GPL-3.0 (see [`LICENSE.md`](LICENSE.md))

Klipper Adaptive Meshing & Purging — meshes only the print's footprint and lays an adaptive purge
line. Vendored here so OpenKE is self-contained; full credit to kyleisah and contributors.

## KE-specific changes vs. upstream

`KAMP/Line_Purge.cfg`, `KAMP/Smart_Park.cfg`, `KAMP/Voron_Purge.cfg` are **verbatim** upstream.

`KAMP/Adaptive_Meshing.cfg` has three edits so it does **not** override the KE's built-in
`BED_MESH_CALIBRATE` (the KE's older Klipper doesn't like that), exposing a separate
`ADAPTIVE_BED_MESH_CALIBRATE` instead:
1. macro renamed `BED_MESH_CALIBRATE` → `ADAPTIVE_BED_MESH_CALIBRATE`
2. `rename_existing:` line removed
3. the inner call `_BED_MESH_CALIBRATE …` → `BED_MESH_CALIBRATE …` (calls the built-in)

`KAMP_Settings.cfg` is upstream with the three includes we use (Adaptive_Meshing, Line_Purge,
Smart_Park) uncommented; the full `_KAMP_Settings` macro and all its variables are kept intact.

To refresh from upstream, see [`docs/VENDORING.md`](../../../../docs/VENDORING.md).
