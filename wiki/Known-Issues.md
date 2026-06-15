# Known Issues & Limitations

## Release-pin upkeep

The install/update scripts are current (they point at `coreflake1/guppyscreen`), but the pinned release
tag must be bumped at each release:

- **`scripts/installer.sh`** / **`scripts/installer-deb.sh`** pin `PINNED_RELEASE` (currently
  `v0.5.5-GuppyKE`) — the tarball they download. Bump this when cutting a new release. The installer
  *script itself* is fetched fresh from `main`, so the install command on the
  [Installation](Installation) page always runs the latest installer.
- **`scripts/update.sh`** queries `coreflake1/guppyscreen` releases and filters out the rolling `nightly`
  prerelease.

## Functional limitations

- The `View Job` / `Queue Job` buttons in the "print already in progress" dialog are unimplemented stubs
  (Moonraker job-queue — deliberately low-priority on a single-printer KE).
- **No automated unit-test suite.** LVGL's own tests are disabled (`LV_BUILD_TEST=0`); verification is
  via the CI build, the simulator, and on-device testing. See [Development and Simulator](Development-and-Simulator).
- CI builds **only the Ender-3 V3 KE smallscreen target** (`guppyscreen-smallscreen`, MIPS). aarch64 and
  standard-screen MIPS can still be built from source, but are not produced by CI in this fork.

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
