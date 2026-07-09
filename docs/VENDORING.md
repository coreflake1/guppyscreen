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
| **Adaptive Print Setup** (Smart_Park/Line_Purge only - Adaptive_Meshing is now original, see below) | `config/GuppyScreen/modules/` + `Settings.cfg` (auto-included) | [kyleisah/Klipper-Adaptive-Meshing-Purging](https://github.com/kyleisah/Klipper-Adaptive-Meshing-Purging) | `b0dad8ec9ee31cb644b94e39d4b8a8fb9d6c9ba0` |
| **Axis Twist Compensation** | `klippy/extras/axis_twist_compensation.py` + `probe.py` patch + cfg | [Klipper3d/klipper](https://github.com/Klipper3d/klipper) `klippy/extras/axis_twist_compensation.py` | tag `v0.12.0` |
| **TMC Autotune** | `klippy/extras/{autotune_tmc,motor_constants}.py`, `motor_database.cfg` | [evgarthub/klipper_tmc_autotune_k1](https://github.com/evgarthub/klipper_tmc_autotune_k1) (fork of [andrewmcgr](https://github.com/andrewmcgr/klipper_tmc_autotune)) | `1cafcf42bfb7aa1985cfd35f3bf7e83f54b0c3d2` |
| **Skew Correction** | `[skew_correction]` cfg (written by installer) | Klipper native — no vendored source | n/a |
| **Screws Tilt Adjust** | `klippy/extras/screws_tilt_adjust.py` + cfg | [Klipper3d/klipper](https://github.com/Klipper3d/klipper) `klippy/extras/screws_tilt_adjust.py` | tag `v0.12.0` |

All vendored sources are GPL-3.0, compatible with OpenKE's GPL-3.0. Each mod folder has a `NOTICE.md`
(provenance + any KE edits) and the upstream `LICENSE`.

## KE-specific edits

`adaptive_print_setup/modules/Adaptive_Meshing.cfg` is **no longer vendored KAMP at all** — it's
original OpenKE code that delegates to Klipper's native `BED_MESH_CALIBRATE ADAPTIVE=1` instead
(ported onto the KE's older Klipper fork via `patch_bed_mesh.py`, since that fork predates
upstream's adaptive-mesh merge). This happened because KAMP's own maintainer recommends against
KAMP's macro-based adaptive mesh now that Klipper has it natively — see `NOTICE.md` in that
directory for the full rationale.

`adaptive_print_setup/modules/Line_Purge.cfg` and `Smart_Park.cfg` are still adapted from KAMP
(structurally near-verbatim; only the settings-macro reference was renamed from `_KAMP_Settings`
to `_OPENKE_ADAPTIVE_SETTINGS`). `Settings.cfg` replaces `KAMP_Settings.cfg` with the same
variables minus `fuzz_amount` (dropped, no longer used by anything). The installer migrates an
existing KAMP install's settings values automatically (`migrate_settings.py`) and removes the old
KAMP files on upgrade - no manual steps.

## Refreshing a vendored mod

```sh
# Adaptive Print Setup (Line_Purge.cfg / Smart_Park.cfg only - Adaptive_Meshing.cfg is ours now)
git clone --depth 1 https://github.com/kyleisah/Klipper-Adaptive-Meshing-Purging.git /tmp/kamp
#   then re-apply the _KAMP_Settings -> _OPENKE_ADAPTIVE_SETTINGS rename (see git history of this dir)

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
