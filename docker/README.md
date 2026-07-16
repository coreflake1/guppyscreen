# Vendored cross-compile toolchain

*(Developer doc.)* Every `scripts/build-*-mipsel.sh` script (nginx, Pillow, streaming-form-data,
curl, OpenRC, ft2font) pins its Docker image by exact sha256 digest, which is reproducible as long
as that image stays on Docker Hub — but if it ever disappears, the whole chain breaks.
`docker/k1-bash-build/` lets you rebuild that toolchain from scratch, independent of Docker Hub,
the same vendoring philosophy as `scripts/vendor/nginx-src/`, `pillow-src/`, etc.

**Not the upstream maintainer's own Dockerfile source** — pellcorp hasn't published one anywhere
we could find. Reconstructed from `docker history --no-trunc` against the real pinned image, which
(since it was built with BuildKit) preserves the exact `RUN`/`COPY`/`ENV` commands used,
byte-for-byte. **Verified, not just plausible-looking**: built it locally and confirmed
`build-nginx-mipsel.sh`/`build-pillow-mipsel.sh`/etc. behave identically against it.

- **`docker/k1-bash-build/`** — glibc, dynamically-linked. Used by every
  `scripts/build-{nginx,pillow,streaming-form-data,curl,openrc,ft2font}-mipsel.sh` script
  (reconstructs `pellcorp/k1-bash-build`). Bundles `mips-gcc720-glibc229` (a real, versioned
  GitHub release from `ballaswag/k1-discovery` — the Dockerfile downloads it directly, not
  vendored here since it's already stably hosted) plus a device-mirroring sysroot
  (`k1-sysroot-min.tar.gz`, vendored here, ~11MB — glibc runtime headers/libs only, no dev
  userland, see `scripts/build-pillow-mipsel.sh`'s own header comment for what that does and
  doesn't contain).

## Building and using it

```sh
docker build -t openke-k1-bash-build:local docker/k1-bash-build/
# then edit the relevant scripts/build-*-mipsel.sh's K1_BASH_BUILD_IMAGE variable to
# "openke-k1-bash-build:local" instead of the pinned upstream digest, or just retag:
docker tag openke-k1-bash-build:local pellcorp/k1-bash-build@sha256:0b96d1d65175c5a2e3a83a64c3212d08dd774fef0900f991e0ebc570ba896c85
```

Only rebuild this from source if the pinned upstream image ever actually becomes unavailable —
day to day, the pinned digest is simpler and already reproducible.

## GuppyScreen's own toolchain (a separate, already-solved problem)

GuppyScreen itself (the touchscreen C++ binary) is built with a *different* toolchain — musl,
fully static — via `scripts/build-mips.sh`. That one already has a proper from-source vendored
replacement, predating this doc: **[`docker/Dockerfile`](../docker/Dockerfile)** (top-level, not
under this directory), published to `ghcr.io/coreflake1/guppydev` and already wired into CI
(`.github/workflows/build.yml`) — see `wiki/Building-from-Source.md`. It downloads the musl
toolchain straight from Bootlin's own permanent, checksum-verified URL instead of vendoring the
~122MB tarball in git, which is why it doesn't hit the size/LFS problems a naive "extract it from
the pinned image" approach runs into. Nothing to add here — it was already done right.
