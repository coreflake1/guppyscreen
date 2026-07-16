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

## Vendored system binaries (Moonraker, nginx)

Separate from the Klipper mods above: `scripts/vendor/nginx.tar.gz` and `scripts/vendor/moonraker.tar.gz`
are full system binaries the installer fetches directly (not through `GuppyScreen/*.cfg`), pinned by SHA
in `scripts/installer.sh`.

**nginx**: built from source via `scripts/build-nginx-mipsel.sh` — cross-compiles a real, current nginx
for the KE (mipsel, glibc, hard-float FP64, NaN2008 - verified via `readelf` to match the KE's actual
runtime ABI) using [pellcorp/k1-nginx](https://github.com/pellcorp/k1-nginx)'s build recipe, vendored
unmodified in `scripts/vendor/nginx-src/` so this never depends on that repo staying available. Just run
the script (needs Docker) — it handles QEMU emulation setup itself (idempotent, safe to re-run) and
writes a ready-to-commit `scripts/vendor/nginx.tar.gz`. **Read the script's own header comment before
touching it** — it documents a real, non-obvious QEMU/MIPS emulation gap (NaN2008 floating-point support)
that silently breaks the standard multiarch/qemu-user-static tooling for this specific target, and how
the fix (a small CPU-model wrapper) works. To bump to a newer nginx release: update the version in
`scripts/vendor/nginx-src/build.sh` and swap in a newer `nginx-*.tar.gz` (pcre2 rarely needs bumping),
then re-run the build script.

**Moonraker**: the `git_repo` portion (Moonraker itself) is still a one-time vendored snapshot
(real upstream clone + prebuilt Python venv). Its two C-extension Python deps that need a real
compiler (`pillow`, `streaming-form-data` — no prebuilt mipsel wheel exists for either on PyPI)
are being given the nginx treatment one at a time:

- **Pillow**: built from source via `scripts/build-pillow-mipsel.sh` — cross-compiles a real
  Pillow 10.4.0 wheel for the device's exact Python (3.8.2, mipsel-linux-gnu, glibc, hard-float
  FP64, NaN2008 — verified via `readelf`), using the same `pellcorp/k1-bash-build` toolchain
  nginx's script uses, plus a cross-compiled CPython 3.8.2 header set (the device ships no
  Python dev headers at all) cached in `scripts/vendor/pillow-src/cross-include/`. **Read the
  script's own header comment before touching it** — it documents four real, non-obvious bugs
  hit building this the first time, most notably a genuine GCC `--sysroot`+`-I` interaction bug
  (an explicit `-I$SYSROOT/usr/include` placed first on the command line still loses to the
  build host's own `-I/usr/include` once `--sysroot` is in play — GCC silently demotes any `-I`
  that matches one of its own sysroot-rewritten default directories). Output goes to
  `scripts/vendor/wheels/`, not yet wired into `moonraker.tar.gz` — see
  `scripts/lib/ensure-mips-qemu.sh` for the shared QEMU/NaN2008 emulation setup both this and
  the nginx script use.
- **streaming-form-data**: built from source via `scripts/build-streaming-form-data-mipsel.sh`,
  reusing the exact same toolchain and cross-compiled CPython headers as Pillow. Turned out much
  simpler in practice — no `pyproject.toml`/PEP-621 metadata bug (this sdist's `setup.py` passes
  `name`/`version` as plain kwargs) and zero external library dependencies (just its own
  pre-generated `_parser.c`) — built clean on the first attempt, real confirmation the
  Pillow-proven toolchain is genuinely reusable rather than a one-off. Output also goes to
  `scripts/vendor/wheels/`.
- Once both wheels exist, dropping them into
  `moonraker/moonraker/scripts/python_wheels/` inside `scripts/vendor/moonraker.tar.gz` is all
  that's needed (that dir + `--find-links=python_wheels` is already how the existing vendored
  `zeroconf` wheel gets found — no code changes required).

**curl-mipsel, OpenRC (`k1/k1_mods/respawn/`), and `ft2font.so`**: the last three vendored
binaries that had zero known build recipe, all reconstructed via
`scripts/build-curl-mipsel.sh`, `scripts/build-openrc-mipsel.sh`, and
`scripts/build-ft2font-mipsel.sh` respectively — each built, ABI-verified, and confirmed
reproducible via a clean re-run. Exact versions were reconstructed from the real binaries
rather than assumed (curl 7.68.0 from its own version string; OpenRC 0.41.2 the same way;
matplotlib 3.3.4/FreeType 2.10.1/NumPy 1.16.4 for `ft2font.so` by matching embedded deprecation
strings and symbol names against real matplotlib source, and confirming NumPy's version live
against the device over SSH). **Read each script's own header comment before touching it** —
`build-ft2font-mipsel.sh`'s in particular documents a real GCC `--sysroot` + `-L` interaction
gotcha (the toolchain's own bundled `libstdc++` becomes unreachable once a custom `--sysroot`
is set, and pointing `-L` at the toolchain's whole internal sysroot to fix it introduces a
*second*, conflicting glibc copy — the fix is copying just the needed `libstdc++` files into an
isolated directory instead). None of the three are wired into the installer or vendored
tarballs yet, and none have been tested on-device — build-time + static ABI verification only,
same as nginx/Pillow/streaming-form-data.

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
