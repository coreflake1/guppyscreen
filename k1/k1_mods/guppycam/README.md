# guppycam — an open, controllable camera daemon for the Creality KE

A from-scratch replacement for Creality's closed **`cam_app`**, written against the
plain Linux **V4L2** API on the Ingenic X2000 ("Helix") hardware codec. One small
static binary, no proprietary SDK, no cloud, no AI middleware — and **every encoder
parameter is under our control** (the thing Creality's black box denied us).

> **Status (2026-06-19):** built and cross-compiles clean to a 71 KB static mipsel
> binary. **Not yet run on the printer** — on-hardware bring-up is deliberately
> deferred to a joint session (see _Test plan_). The camera stack on the printer is
> untouched and healthy.

---

## 1. What Creality's `cam_app` actually does (reverse-engineered)

`cam_app` is a closed 27 KB binary. By inspecting it live we established the full pipeline:

```
USB UVC camera (/dev/video4, "CCX2F3298")  ── MJPEG 1920x1080 ──┐
                                                                 │
            ┌────────────────────────────────────────────────────┘
            ▼
   Helix HW JPEG decode (/dev/video1)   MJPEG ─► NV12
            ▼
   Helix HW H.264 encode (/dev/video1)  NV12  ─► H.264  ─► shared memfd ("main_memfd")
            │                                                  └─► consumed by go2rtc / guppy-webrtc
            └─► raw MJPEG also forwarded to a second memfd ─► mjpg_streamer :8080 (snapshots)
```

Key limitations we hit because it's closed:
- **No encoder control.** Fixed bitrate / GOP / profile; no way to set them, no way to request a keyframe.
- **Opaque memfd interface** we had to reverse-engineer to consume.
- Ships entangled with Creality's cloud/AI stack (`cx_ai_middleware`, cloud webrtc).

The crucial discovery: **it is *not* the Ingenic IMP SDK** — it's standard V4L2 mem-to-mem
on the in-kernel `helix-venc` driver. That means we can do everything it does, openly.

## 2. What guppycam does

Exactly the same hardware path, but open and parameterised:

```
UVC video4 (MJPEG) ─► [V4L2 capture, MMAP]
   ├─► snapshot: MJPEG passthrough (zero transcode)         → --snapshot file.jpg
   └─► Helix JPEG-decode (video1)  MJPEG ─► NV12
          └─► Helix H.264-encode (video1)  NV12 ─► H.264    → stdout | --out file.h264
                 ▲ bitrate / GOP / rate-control / profile / force-IDR  (all live-controllable)
```

### How it works, stage by stage ([guppycam.c](guppycam.c))
1. **Capture** (`cap_*`): opens the UVC camera, sets `MJPG` at the requested resolution,
   MMAPs 4 buffers, and `poll()`s for frames. MJPEG is the only way to get 1080p over
   USB 2.0 (raw YUYV 1080p exceeds the bus), so the camera hands us compressed JPEG.
2. **Decode** (`M2M` #1): a Helix mem-to-mem context with `OUTPUT=JPEG`, `CAPTURE=NV12`.
   Hardware JPEG-decodes each frame to raw NV12. Zero CPU cost.
3. **Encode** (`M2M` #2): a second Helix context with `OUTPUT=NV12`, `CAPTURE=H264`.
   Hardware H.264-encodes to an Annex-B stream. Zero CPU cost.
4. **Output**: writes the H.264 to stdout or `--out`. `--snapshot` skips encode entirely
   and just writes one native MJPEG frame (that's all a snapshot needs).
5. **Control**: `SIGUSR1` forces an IDR/keyframe on the next frame (instant first-frame
   for a new viewer). All encoder params are set once at startup via standard `V4L2_CID_*`.

The decode and encode each run as an independent Helix M2M instance (the driver supports
multiple — verified in the kernel log), mirroring how `cam_app` itself uses the block.

## 3. Encoder controls we expose (verified present on-device via `v4l2-ctl -L`)

| Control | CID | guppycam flag | Range (device) |
|---|---|---|---|
| Bitrate | `MPEG_VIDEO_BITRATE` | `--bitrate` | 1 … 4 000 000 bit/s |
| Rate-control mode | `MPEG_VIDEO_BITRATE_MODE` | `--rc cbr\|vbr\|cq` | VBR / CBR / CQ |
| GOP size (keyframe interval) | `MPEG_VIDEO_GOP_SIZE` | `--gop` | 0 … 65535 frames |
| H.264 profile | `MPEG_VIDEO_H264_PROFILE` | `--profile baseline\|main\|high` | Baseline … High |
| Force keyframe | `MPEG_VIDEO_FORCE_KEY_FRAME` | `SIGUSR1` | on demand |
| Header mode (SPS/PPS) | `MPEG_VIDEO_HEADER_MODE` | (set to "with each IDR") | — |
| Frame-level rate control | `MPEG_VIDEO_FRAME_RC_ENABLE` | (enabled) | — |

Also available for later use: per-frame I/P QP, min/max QP, H.264 level, JPEG quality.

## 4. How much better than Creality — at a glance

| | Creality `cam_app` | **guppycam** |
|---|---|---|
| Source | closed 27 KB binary | open, documented C (~600 lines) |
| Encoder control | none (fixed) | bitrate, GOP, profile, rate-control, **force-IDR** |
| First-frame latency tool | none | `SIGUSR1` force-IDR → instant join |
| Bitrate for remote/slow links | fixed 4 Mbps | tunable down to KB/s |
| Cloud / AI dependency | entangled | none |
| Output interface | opaque RE'd memfd | plain H.264 (pipe/file), trivially consumable |
| Snapshot | via separate mjpg_streamer | built-in MJPEG passthrough |
| Footprint | cam_app + mjpg_streamer | single static binary, CPU ~0 (HW codec) |
| Maintainability | reverse-engineering required | standard V4L2, fully ours |

The headline win is **control**: the two things we previously *could not do* — pick the
bitrate and force a keyframe on connect — are now first-class. That directly enables
adaptive-bitrate streaming and instant first-frame for new viewers, neither possible
through Creality's binary.

## 5. Build

```bash
bash build.sh          # cross-compiles a static mipsel binary in the guppydev image
```
Output: `guppycam` — ELF 32-bit MIPS, statically linked, ~71 KB.

## 6. Usage

```bash
guppycam -w 1280 -h 720 -f 15 -b 2000000 --gop 15 --profile main -o out.h264
guppycam --snapshot snap.jpg                 # one MJPEG frame, no encode
kill -USR1 <pid>                             # force a keyframe now
```

## 7. Test plan (joint, on-printer — NOT yet done)

Each step stops `cam_app`, runs the test, and **restores `cam_app` + guppy-webrtc** so the
camera is never left broken. Exact restore command:
`/usr/bin/cam_app -i /dev/v4l/by-id/main-video-4 -t 0 -w 1920 -h 1080 -f 15 -c &`

1. **Encode smoke test (lowest risk):** feed a synthetic NV12 file to a Helix encode
   instance, verify valid H.264 out with `ffmpeg`. Confirms the M2M encode path before
   touching the camera.
2. **Snapshot:** `guppycam --snapshot /tmp/s.jpg`, pull it, confirm it's a valid JPEG.
3. **Full pipeline:** `guppycam --frames 150 -o /tmp/t.h264` (10 s @ 15 fps), pull it,
   `ffprobe`/`ffmpeg` to confirm resolution, frame count, keyframe cadence.
4. **Control verification:** vary `--bitrate` / `--gop`, measure the output bitrate and
   keyframe interval actually change; `SIGUSR1` produces an out-of-cadence keyframe.
5. **Compare vs Creality:** measure CPU and RSS of guppycam vs cam_app; confirm parity or
   better and that the HW encoder keeps CPU ~0.
6. **Restore + verify camera healthy** every time.

## 8. Integration roadmap (after bring-up)

guppycam currently emits H.264 to stdout/file. To slot it into the live stack we pick one:
- **A — feed `guppy-webrtc` directly:** smallest change; guppycam writes frames into a
  shared buffer guppy-webrtc reads (replaces the RE'd cam_app memfd with one we both own).
- **B — cam_app-memfd-compatible mode:** emit the exact `main_memfd` layout so guppy-webrtc
  works unchanged (drop-in; needs the launcher to point at guppycam's pid).
- **C — built-in HTTP:** serve MJPEG + snapshots on :8080 to replace `mjpg_streamer` too,
  making guppycam the *entire* camera stack in one process.

Recommended: **A** for the H.264 path + **C**'s snapshot server, so one process owns
capture + encode + snapshots with no Creality binaries at all.
