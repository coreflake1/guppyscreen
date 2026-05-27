# Known Issues & Limitations

## Stale references in the install/update scripts

As of this writing, the install/update scripts predate the merge to `main` / `v0.1.1-ke-gui-fixes` and
still reference older locations. These should be updated:

- **`scripts/installer.sh`** pins `PINNED_RELEASE="v0.1.0-ke-bedmesh"` and the asset URL points at
  `coreflake1/guppyscreen` releases. The installer *script itself* is fetched fresh from `main` (so
  the install command on the [Installation](Installation.md) page is correct), but the **tarball it
  downloads** is the older pinned tag until `PINNED_RELEASE` is bumped.
- **`scripts/installer-deb.sh`** likewise pins `v0.1.0-ke-bedmesh` and references the
  `ke-advanced-3d-bedmesh` branch in its banner. (This installer is for aarch64/Debian, not the KE.)
- **`scripts/update.sh`** checks `probielodan/guppyscreen` for the latest release, **not** this fork —
  it will not pick up this fork's releases until updated.

## Functional limitations

- The `View Job` / `Queue Job` buttons in the "print already in progress" dialog are unimplemented stubs.
- **No automated unit-test suite.** LVGL's own tests are disabled (`LV_BUILD_TEST=0`); verification is
  via the CI build matrix, the simulator, and on-device testing. See [Development and Simulator](Development-and-Simulator.md).
- The KE install path is hardware-specific. aarch64 and standard-screen MIPS targets are built by CI
  but are less exercised in this fork.

## Minor small-screen layout items (lower priority)

From `docs/dev-notes.md`:

- Temperature-chart Y-axis tick labels can overlap at high tick density on the 272 px height.
- The limits panel's four sliders in a single column can overflow 272 px; a 2×2 grid would fit better.

## Security caveats

- The installer and updater use `wget --no-check-certificate` because the KE's BusyBox `wget` lacks a
  CA bundle. This skips TLS verification — be mindful when installing over untrusted networks.
- GuppyScreen typically runs as `root` on the printer (direct wpa_supplicant access).
- `guppyconfig.json` may hold a Moonraker API key (`moonraker_api_key`); the shipped template sets it
  to `false`. Keep real keys out of version control.
