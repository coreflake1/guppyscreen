# Vendored Klipper mods

*(Developer doc.)* OpenKE bundles a few third-party Klipper mods directly in the repo instead of making
users clone other repositories and hand-edit configs. They live under
[`k1/k1_mods/klipper_mods/`](../k1/k1_mods/klipper_mods/) and ship in the release tarball automatically
(`scripts/release.sh` copies all of `k1/k1_mods`).

The installer (`scripts/installer.sh`) sets them up under an opt-in prompt. Everything except the Axis
Twist probe patch installs through the existing `[include GuppyScreen/*.cfg]` mechanism, so it touches
**no** `printer.cfg` sections and is idempotent (plain file copies).

## What's vendored

| Mod | On-device target | Upstream | Pinned |
|---|---|---|---|
| **KAMP** | `config/GuppyScreen/KAMP/` + `KAMP_Settings.cfg` (auto-included) | [kyleisah/Klipper-Adaptive-Meshing-Purging](https://github.com/kyleisah/Klipper-Adaptive-Meshing-Purging) | `b0dad8ec9ee31cb644b94e39d4b8a8fb9d6c9ba0` |
| **Axis Twist Compensation** | `klippy/extras/axis_twist_compensation.py` + `probe.py` patch + cfg | [Klipper3d/klipper](https://github.com/Klipper3d/klipper) `klippy/extras/axis_twist_compensation.py` | tag `v0.12.0` |
| **TMC Autotune** | `klippy/extras/{autotune_tmc,motor_constants}.py`, `motor_database.cfg` | [evgarthub/klipper_tmc_autotune_k1](https://github.com/evgarthub/klipper_tmc_autotune_k1) (fork of [andrewmcgr](https://github.com/andrewmcgr/klipper_tmc_autotune)) | `1cafcf42bfb7aa1985cfd35f3bf7e83f54b0c3d2` |
| **Skew Correction** | `[skew_correction]` cfg (written by installer) | Klipper native — no vendored source | n/a |
| **Screws Tilt Adjust** | `klippy/extras/screws_tilt_adjust.py` + cfg | [Klipper3d/klipper](https://github.com/Klipper3d/klipper) `klippy/extras/screws_tilt_adjust.py` | tag `v0.12.0` |

All vendored sources are GPL-3.0, compatible with OpenKE's GPL-3.0. Each mod folder has a `NOTICE.md`
(provenance + any KE edits) and the upstream `LICENSE`.

## KE-specific edits (only KAMP is modified)

`kamp/KAMP/Adaptive_Meshing.cfg` is changed so it does **not** override the KE's built-in
`BED_MESH_CALIBRATE` (the KE's older Klipper dislikes that):
1. macro `BED_MESH_CALIBRATE` → `ADAPTIVE_BED_MESH_CALIBRATE`
2. `rename_existing:` removed
3. inner `_BED_MESH_CALIBRATE …` → `BED_MESH_CALIBRATE …`

`kamp/KAMP_Settings.cfg` = upstream with the three includes we use (Adaptive_Meshing, Line_Purge,
Smart_Park) uncommented. Everything else (`Line_Purge.cfg`, `Smart_Park.cfg`, `Voron_Purge.cfg`, the ATC
module, all TMC files) is verbatim upstream.

## Refreshing a vendored mod

```sh
# KAMP (re-apply the 3 edits — keep them in sync with installer expectations)
git clone --depth 1 https://github.com/kyleisah/Klipper-Adaptive-Meshing-Purging.git /tmp/kamp
#   then re-run the three sed edits on Adaptive_Meshing.cfg (see git history of this dir)

# Axis Twist (single file, pin a Klipper tag)
curl -O https://raw.githubusercontent.com/Klipper3d/klipper/<TAG>/klippy/extras/axis_twist_compensation.py

# Screws Tilt Adjust (single file, pin a Klipper tag)
curl -O https://raw.githubusercontent.com/Klipper3d/klipper/<TAG>/klippy/extras/screws_tilt_adjust.py

# TMC Autotune
git clone --depth 1 https://github.com/evgarthub/klipper_tmc_autotune_k1.git /tmp/tmc
```

After refreshing: bump the pinned commit/tag in the table above and in each mod's `NOTICE.md`, and
re-run the installer dry-run idempotency check before releasing.

## Installer contract (what to keep true)

- Re-running the installer must be **idempotent** — copies overwrite, `patch_probe.py` no-ops on an
  already-patched file, and `probe.py.bak` is taken only once.
- `patch_probe.py` must exit non-zero with a `STOP:` message (changing nothing) when `probe.py` doesn't
  match the expected anchor, so a firmware change can't half-apply the graft.
- Adding config via `GuppyScreen/*.cfg` is preferred over editing `printer.cfg` sections.
