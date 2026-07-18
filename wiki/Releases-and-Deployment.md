# Releases and Deployment

## CI pipeline

Release artifacts are produced by `.github/workflows/build.yml`, which runs inside the
`ghcr.io/coreflake1/guppydev:latest` toolchain container (built from
[`docker/Dockerfile`](https://github.com/coreflake1/guppyscreen/blob/main/docker/Dockerfile)) on
pushes to `main`/`develop` (nightly prerelease) and on tags (stable release). The workflow checks out submodules, applies the three patches, builds the bundled
libraries, builds GuppyScreen, then packages each asset with `scripts/release.sh`.

### Build output

This is a KE-focused fork, so CI builds **only the Ender-3 V3 KE asset**:

| Asset | Toolchain | Theme | Small screen | Notes |
|---|---|---|---|---|
| `guppyscreen-smallscreen.tar.gz` | `mipsel-buildroot-linux-musl-` | material | **yes** | **The Ender-3 V3 KE asset** |

- Non-tag pushes set `GUPPYSCREEN_VERSION=nightly-<sha>` and publish a `nightly` prerelease.
- Tag pushes set `GUPPYSCREEN_VERSION=<tag>` and publish a stable release. **The CI-generated notes
  are just GitHub's bare auto-generated commit list + a changelog link — genuinely written,
  plain-language release notes (the style every past release actually has) are a manual step, not
  something CI does.** See "Writing the release notes" below - don't skip this, it's easy to miss
  since the release exists and looks published either way.

> The toolchain image still ships an aarch64 cross-compiler, so other targets (standard-screen K1,
> Z-Bolt icons, aarch64) can be built manually if ever needed — they're just not released.

## Packaging locally

After a successful build (see [Building from Source](Building-from-Source)), package a tarball with:

```bash
GUPPYSCREEN_VERSION=0.1.1-ke-gui-fixes GUPPY_THEME=blue \
  bash scripts/release.sh guppyscreen-smallscreen
```

`scripts/release.sh`:

1. Strips the binary (using `$CROSS_COMPILE`strip).
2. Copies the binary, `k1/k1_mods`, `k1/scripts`, `themes/`, the installers (`installer.sh`,
   `installer-deb.sh`), `update.sh`, and `debian/` into `releases/guppyscreen/`.
3. Writes a `.version` file (`{version, theme, asset_name}`).
4. Produces `<asset>.tar.gz`.

The on-printer installer downloads the matching `*.tar.gz` from the GitHub release.

## Release tags

Stable releases are tagged `vX.Y.Z-OpenKE` on `main` (e.g. `v1.5.0-OpenKE`) — check the
[GitHub Releases page](https://github.com/coreflake1/guppyscreen/releases) for the current one
rather than trusting a version number hardcoded in this doc, since it goes stale fast. Earlier
pre-rename tags (`v0.1.1-ke-gui-fixes`, `v0.1.0-ke-bedmesh`, the `0.0.x-beta` series) exist further
back in the repository history from before the `-OpenKE` naming convention.

### Testing an unreleased `ke-next` build

Every push to `ke-next` also publishes a moving `nightly-ke-next` prerelease (same CI, see
`.github/workflows` for the exact build job name). To try it before it's merged to `main`:

```sh
PINNED_RELEASE=nightly-ke-next sh -c "$(curl -sL https://raw.githubusercontent.com/coreflake1/guppyscreen/ke-next/scripts/installer.sh)"
```

(Use `curl`, not the `wget` form shown elsewhere in this wiki, if your printer's `wget` can't
complete a TLS handshake against GitHub — see [Troubleshooting](Troubleshooting).) Re-run the
normal install command with no `PINNED_RELEASE` to go back to the latest stable release.

See **[Troubleshooting](Troubleshooting)** for current installer/upgrade caveats.

## Writing the release notes (manual step, every stable release)

CI publishes the tag with bare auto-generated notes (a commit list + changelog link) — fine for
developers, not for end users, most of whom are not developers. **After the tag build succeeds,
replace the notes** (`gh release edit <tag> --notes-file <file>` or the GitHub web UI) with real,
plain-language writing in the established house style — look at any past release
(e.g. `gh release view v1.4.2-OpenKE --json body`) for the exact tone to match:

- A one-line `## What's new in vX.Y.Z` title, then a short plain-English summary sentence.
- If the update needs the **full installer** (not just the in-app "Update Guppy" button) - e.g. it
  touches Klipper macros/config, not just the GuppyScreen binary - say so up front with a `⚠️`
  callout, since "Update Guppy" alone silently won't apply those parts.
- One `###` section per real change, in plain language: what a user would have actually noticed
  going wrong (not the internal root cause), and what's different now. No jargon, no file names,
  no internal code identifiers - a non-developer should be able to read every line.
- End with the upgrade command and a `**Full Changelog**:` compare link (same two things CI's
  bare notes already have, just keep them).

Skipping this is easy to miss precisely because the release already looks published and complete
without it - it isn't done until the notes are rewritten.
