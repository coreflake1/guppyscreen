# Releases and Deployment

## CI pipeline

Release artifacts are produced by `.github/workflows/build.yml`, which runs inside the
`ghcr.io/coreflake1/guppydev:latest` toolchain container (built from
[`docker/Dockerfile`](https://github.com/coreflake1/guppyscreen/blob/main/docker/Dockerfile)) on
pushes to `main`/`develop` (nightly prerelease) and on tags (stable release). The workflow checks out submodules, applies the three patches, builds the bundled
libraries, builds GuppyScreen, then packages each asset with `scripts/release.sh`.

### Build matrix

| Asset | Toolchain | Theme | Small screen | Notes |
|---|---|---|---|---|
| `guppyscreen-smallscreen.tar.gz` | `mipsel-buildroot-linux-musl-` | material | **yes** | **The Ender-3 V3 KE asset** |
| `guppyscreen.tar.gz` | `mipsel-buildroot-linux-musl-` | material | no | MIPS, standard screen |
| `guppyscreen-zbolt.tar.gz` | `mipsel-buildroot-linux-musl-` | zbolt | no | MIPS, Z-Bolt icons |
| `guppyscreen-arm.tar.gz` | `aarch64-none-linux-gnu-` | material | no | aarch64 — **will not run on the KE** |

- Non-tag pushes set `GUPPYSCREEN_VERSION=nightly-<sha>` and publish a `nightly` prerelease.
- Tag pushes set `GUPPYSCREEN_VERSION=<tag>` and publish a stable release with generated notes.

## Packaging locally

After a successful build (see [Building from Source](Building-from-Source.md)), package a tarball with:

```bash
GUPPYSCREEN_VERSION=0.1.1-ke-gui-fixes GUPPY_THEME=blue \
  bash scripts/release.sh guppyscreen-smallscreen
```

`scripts/release.sh`:

1. Strips the binary (using `$CROSS_COMPILE`strip).
2. Copies the binary, `k1/k1_mods`, `k1/scripts`, `themes/`, the installers (`installer.sh`,
   `installer-deb.sh`), `update.sh`, `reinstall-creality.sh`, and `debian/` into `releases/guppyscreen/`.
3. Writes a `.version` file (`{version, theme, asset_name}`).
4. Produces `<asset>.tar.gz`.

The on-printer installer downloads the matching `*.tar.gz` from the GitHub release.

## Release tags

The current release baseline is the `v0.1.1-ke-gui-fixes` tag on `main`. Earlier tags
(`v0.1.0-ke-bedmesh`, `v0.1.0`, the `0.0.x-beta` series) also exist in the repository history.

See **[Known Issues](Known-Issues.md)** for caveats about which release the installer/updater scripts
currently point at.
