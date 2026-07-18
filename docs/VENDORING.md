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
got the nginx treatment and are now **fully wired into the installer and confirmed working on
real hardware**, not just built:

- **Pillow**: built from source via `scripts/build-pillow-mipsel.sh` — cross-compiles a real
  Pillow 10.4.0 wheel for the device's exact Python (3.8.2, mipsel-linux-gnu, glibc, hard-float
  FP64, NaN2008 — verified via `readelf`), using the same `pellcorp/k1-bash-build` toolchain
  nginx's script uses, plus a cross-compiled CPython 3.8.2 header set (the device ships no
  Python dev headers at all) cached in `scripts/vendor/pillow-src/cross-include/`. **Read the
  script's own header comment before touching it** — it documents four real, non-obvious bugs
  hit building this the first time, most notably a genuine GCC `--sysroot`+`-I` interaction bug
  (an explicit `-I$SYSROOT/usr/include` placed first on the command line still loses to the
  build host's own `-I/usr/include` once `--sysroot` is in play — GCC silently demotes any `-I`
  that matches one of its own sysroot-rewritten default directories). See
  `scripts/lib/ensure-mips-qemu.sh` for the shared QEMU/NaN2008 emulation setup both this and
  the nginx script use.
- **streaming-form-data**: built from source via `scripts/build-streaming-form-data-mipsel.sh`,
  reusing the exact same toolchain and cross-compiled CPython headers as Pillow. Turned out much
  simpler in practice — no `pyproject.toml`/PEP-621 metadata bug (this sdist's `setup.py` passes
  `name`/`version` as plain kwargs) and zero external library dependencies (just its own
  pre-generated `_parser.c`) — built clean on the first attempt, real confirmation the
  Pillow-proven toolchain is genuinely reusable rather than a one-off.
- **Both wheels are wired into `scripts/installer.sh`'s real in-place upgrade path** (search
  `PILLOW_TARGET_VERSION`): on install/update it checks the currently-installed Pillow/
  streaming-form-data versions against the target, and if either is behind, downloads the
  matching wheels from a **pinned commit** (not a moving branch — a wheel filename that used to
  get silently reused forever otherwise) and installs them via
  `pip install --no-index --find-links=<dir>`. **Real gotcha, since fixed**: the wheels are
  built and named with the `linux_mipsel` platform tag (matching the interpreter's own C-extension
  import convention), but this device's `pip` specifically wants `linux_mips` (no "el") in the
  *wheel filename* to accept it as installable — the two mechanisms disagree on the tag, confirmed
  by testing both a real install and an import on-device. The committed wheel filenames under
  `scripts/vendor/wheels/` are the renamed `linux_mips` versions; don't rename them back.
- Confirmed via multiple real on-device installer runs (not just build-time ABI checks): clean
  install, idempotent re-run, and upgrade-from-old-version all tested against the real printer.

**curl-mipsel, OpenRC (`k1/k1_mods/respawn/`), and `ft2font.so`**: built via
`scripts/build-curl-mipsel.sh`, `scripts/build-openrc-mipsel.sh`, and
`scripts/build-ft2font-mipsel.sh` respectively. Exact versions were reconstructed from the real
binaries rather than assumed (curl 7.68.0 from its own version string; OpenRC 0.41.2 the same
way; matplotlib 3.3.4/FreeType 2.10.1/NumPy 1.16.4 for `ft2font.so` by matching embedded
deprecation strings and symbol names against real matplotlib source, and confirming NumPy's
version live against the device over SSH). **Read each script's own header comment before
touching it** — `build-ft2font-mipsel.sh`'s in particular documents a real GCC `--sysroot` + `-L`
interaction gotcha (the toolchain's own bundled `libstdc++` becomes unreachable once a custom
`--sysroot` is set, and pointing `-L` at the toolchain's whole internal sysroot to fix it
introduces a *second*, conflicting glibc copy — the fix is copying just the needed `libstdc++`
files into an isolated directory instead). **All three are now wired into `installer.sh`** -
`curl-mipsel` is fetched from a pinned commit URL (`CURL_BOOTSTRAP_URL`) and used for the
installer's own downloads once bootstrapped; OpenRC's `libeinfo.so.1`/`librc.so.1` are symlinked
into `/lib/` for `supervise-daemon`; `ft2font.so` replaces the stock matplotlib module, backed up
first. All three confirmed working on real hardware, not just built.

This whole chain (nginx, Moonraker wheels, curl-mipsel) also went through a real multi-bug
shakeout finding actual installer defects on live hardware - wheel platform-tag mismatches,
an nginx upgrade path that clobbered Mainsail's own config, a false-negative health check, and a
wget-vs-GitHub TLS/SNI incompatibility (BusyBox `wget` can't complete a TLS handshake against
Fastly-fronted `github.com`/`raw.githubusercontent.com` - `download_file()` now goes straight to
`curl` for GitHub URLs instead of trying `wget` first and wasting a retry cycle). See the git
history for `scripts/installer.sh` around this release for the specific fixes if debugging
something in this area - each was a real, reproduced-then-fixed bug, not a guess.

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

## Rebuilding a vendored system package (nginx, Moonraker's Pillow/streaming-form-data, curl-mipsel, OpenRC, ft2font)

These are real cross-compiles, not config file copies - each has its own `scripts/build-*.sh`,
needs Docker, and shares one QEMU/MIPS emulation setup:

```sh
# One-time, shared by every script below: sets up QEMU user-mode emulation for this MIPS target.
# Idempotent - safe to re-run. Read this file first if any build script fails at the "run inside
# QEMU" step - it documents a real, non-obvious NaN2008 floating-point gap in the standard
# multiarch/qemu-user-static tooling for this exact target, and how the fix works.
bash scripts/lib/ensure-mips-qemu.sh

bash scripts/build-nginx-mipsel.sh                    # -> scripts/vendor/nginx.tar.gz
bash scripts/build-pillow-mipsel.sh                   # -> scripts/vendor/wheels/pillow-*.whl
bash scripts/build-streaming-form-data-mipsel.sh      # -> scripts/vendor/wheels/streaming_form_data-*.whl
bash scripts/build-curl-mipsel.sh                     # -> scripts/vendor/curl-mipsel
bash scripts/build-openrc-mipsel.sh                   # -> k1/k1_mods/respawn/ (supervise-daemon + libs)
bash scripts/build-ft2font-mipsel.sh                  # -> ft2font.cpython-38-mipsel-linux-gnu.so
```

**Read each script's own header comment before touching it** - every one of them documents a
real, hard-won gotcha specific to cross-compiling for this target (see the sections above for
what each one is). None of this is guesswork the second time; the scripts exist so it doesn't
have to be figured out again.

After rebuilding a wheel or `curl-mipsel`: update the pinned commit URL in `scripts/installer.sh`
(`PILLOW_TARGET_VERSION`/`SFD_TARGET_VERSION`/`CURL_BOOTSTRAP_URL`) to point at the commit that
adds the new file - **never let these silently point at a moving branch**, a wheel filename
mismatch (mipsel vs mips, see above) has already caused a real, shipped bug once. After rebuilding
nginx: bump the version in `scripts/vendor/nginx-src/build.sh` first, then re-run the build script.

## Installer contract (what to keep true)

- Re-running the installer must be **idempotent** — copies overwrite, `patch_probe.py` no-ops on an
  already-patched file, and `probe.py.bak` is taken only once.
- `patch_probe.py` must exit non-zero with a `STOP:` message (changing nothing) when `probe.py` doesn't
  match the expected anchor, so a firmware change can't half-apply the graft.
- Adding config via `GuppyScreen/*.cfg` is preferred over editing `printer.cfg` sections.
