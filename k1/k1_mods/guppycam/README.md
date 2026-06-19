# guppycam — an open, controllable camera daemon for the Creality KE

A from-scratch replacement for Creality's closed **`cam_app`**, written against the
plain Linux **V4L2** API on the Ingenic X2000 ("Helix") hardware codec. One small
static binary, no proprietary SDK, no cloud, no AI middleware — and **every encoder
parameter is under our control** (the thing Creality's black box denied us).

> **Status (2026-06-19):** v2 — all five planned improvements + camera-portability
> implemented. Cross-compiles clean to a **139 KB static mipsel binary**; arg/flow
> paths host-smoke-tested; the binary is confirmed to **execute on the printer**
> (`--help`, opens no devices). **The camera pipeline itself has not yet been run on
> hardware** — that bring-up is deliberately deferred to a joint session (see _Test
> plan_). The printer's live camera stack was never disrupted.

---

## 1. What Creality's `cam_app` does (reverse-engineered)

```
USB UVC camera (/dev/video4, "CCX2F3298")  ── MJPEG 1920x1080 ──┐
            ┌────────────────────────────────────────────────────┘
            ▼
   Helix HW JPEG decode (/dev/video1)   MJPEG ─► NV12
            ▼
   Helix HW H.264 encode (/dev/video1)  NV12  ─► H.264  ─► shared memfd  ─► go2rtc/guppy-webrtc
            └─► raw MJPEG also forwarded ─► mjpg_streamer :8080 (snapshots)
```
A closed 27 KB binary with **no encoder control** (fixed bitrate/GOP/profile, no
force-keyframe) and an opaque RE'd memfd interface. Crucially it is **not** the Ingenic
IMP SDK — it's standard **V4L2 mem-to-mem** on the in-kernel `helix-venc` driver, which
is exactly why we can reproduce and beat it.

## 2. What guppycam does (v2)

```
camera (UVC)  ── auto-detect source format ──┐
   ├─ H.264 ─────────────────────────────────┼─► passthrough (no transcode)
   ├─ MJPEG ─► Helix HW JPEG-decode ─► NV12 ──┤
   └─ YUYV  ─► CPU convert           ─► NV12 ─┘
                                          │  (zero-copy dmabuf where the driver allows)
                                          ▼
                  one or more Helix H.264 encoders (simulcast):
                     stream0 = full res @ bitrate0   ──► stdout / file
                     stream1 = downscaled @ bitrate1 ──► file        (optional)
                                          │
        live control (unix socket): adaptive bitrate (AIMD), force-IDR, stats
```

### The five improvements over v1 (all implemented)
1. **Adaptive bitrate (closed-loop).** A unix control socket (`--control`) accepts
   `loss <pct>` / `bitrate <bps>` / `idr` / `stats`. An **AIMD** controller maps packet
   loss to a live `V4L2_CID_MPEG_VIDEO_BITRATE` change — the stream backs off on a
   congested link and probes back up when clear. (Wire guppy-webrtc's
   `on_receiver_packet_loss` to this socket and it's fully automatic.)
2. **Zero-copy DMABUF decode→encode.** The decoder's NV12 CAPTURE buffers are exported
   (`VIDIOC_EXPBUF`) and imported as the encoder's OUTPUT buffers (`V4L2_MEMORY_DMABUF`),
   eliminating the inter-stage `memcpy`. Detection is by **trial REQBUFS** (the only
   method on kernel 4.4 — the `capabilities` field doesn't exist pre-4.13); if the driver
   refuses DMABUF it **falls back to memcpy** automatically. `--zerocopy auto|on|off`.
3. **Skip-decode paths + camera portability.** The capture stage **auto-selects** the
   best source the webcam offers: native **H.264** (pure passthrough, no decode/encode) →
   **MJPEG** (HW decode) → **YUYV** (CPU `yuyv_to_nv12`). `--input` can pin one. This is
   also what makes it portable (see §5).
4. **Simulcast.** `--stream WxH@bps[:file]` (repeatable) adds extra encoder instances at
   independent resolution/bitrate from one captured frame (e.g. 1080p local + 480p remote).
   Secondary streams are NV12-downscaled in software (the `jz-rot` block only does RGB/YUYV,
   so it isn't a clean NV12 scaler).
5. **Robustness.** Camera auto-reconnect on capture stall, encoder drain on shutdown
   (`VIDIOC_ENCODER_CMD` STOP, backed by `STREAMOFF` since a 4.4 vendor codec may not honor
   `V4L2_BUF_FLAG_LAST`), bounded poll timeouts, clean teardown of all resources.

## 3. Encoder controls (verified present on-device via `v4l2-ctl -L`)

| Control | guppycam | Range (device) |
|---|---|---|
| Bitrate (live-changeable) | `--bitrate`, socket `bitrate`/`loss` | 150 kbit/s … 4 Mbit/s |
| Rate-control mode | `--rc cbr\|vbr\|cq` | VBR / CBR / CQ |
| GOP / keyframe interval | `--gop` | 0 … 65535 |
| H.264 profile | `--profile baseline\|main\|high` | Baseline … High |
| Force keyframe | `SIGUSR1` / socket `idr` | on demand |

## 4. How much better than Creality

| | Creality `cam_app` | **guppycam v2** |
|---|---|---|
| Source | closed binary | open, documented C |
| Encoder control | none | bitrate, GOP, profile, RC mode, force-IDR |
| Adaptive bitrate | no | yes (AIMD, closed-loop) |
| Simulcast | no | yes (N streams) |
| Camera support | the Nebula cam | any UVC webcam (H.264/MJPEG/YUYV auto) |
| Inter-stage copies | n/a (opaque) | zero-copy dmabuf (with fallback) |
| Cloud/AI dependency | entangled | none |
| Footprint | cam_app + mjpg_streamer | one static binary, CPU ~0 (HW codec) |

## 5. Portability — is it Nebula-specific?

**No, on the camera side.** Capture is generic V4L2/UVC and auto-detects the format the
webcam offers, so **another USB webcam works the same** — the "Nebula cam" is just a
standard UVC device (`a108:2231`, `uvcvideo`). The only assumption is the camera offers
one of H.264 / MJPEG / YUYV (essentially all do); the **YUYV auto-path** covers the
cheapest cameras that only do uncompressed.

**Encoder side is X2000-specific** — the Helix codec is this printer's SoC. But it uses
the standard V4L2 M2M API, so porting to another board's encoder is a contained change
(swap the device + format negotiation), not a rewrite.

## 6. Build & usage

```bash
bash build.sh                                   # static mipsel binary, ~139 KB
guppycam -w 1280 -h 720 -f 15 -b 2000000 -o out.h264
guppycam --stream 640x360@500000:/tmp/lo.h264   # + a low-bitrate simulcast stream
guppycam --control /tmp/guppycam.sock           # enable adaptive-bitrate control socket
guppycam --snapshot snap.jpg                     # one MJPEG frame, no encode
kill -USR1 <pid>                                 # force a keyframe
echo -n "loss 8" | socat - UNIX-SENDTO:/tmp/guppycam.sock   # report loss -> AIMD backs off
```

## 7. Test plan (joint, on-printer — NOT yet done)

Each step stops `cam_app`, runs the test, and **restores it**:
`/usr/bin/cam_app -i /dev/v4l/by-id/main-video-4 -t 0 -w 1920 -h 1080 -f 15 -c &`
then `/etc/init.d/S96guppywebrtc start`.

1. **Encode smoke (lowest risk):** synthetic NV12 → one Helix encode instance → `ffmpeg`
   verifies valid H.264. Confirms the M2M encode path before touching the camera.
2. **Snapshot** (`--snapshot`) → valid JPEG.
3. **MJPEG full pipeline** (`--frames 150 -o t.h264`) → `ffprobe` resolution/keyframes.
4. **YUYV path** (`--input yuyv -w 1280 -h 720`) → verify the CPU converter output.
5. **Controls:** vary `--bitrate`/`--gop`; `SIGUSR1` and socket `idr` produce keyframes;
   socket `loss 8` drops the measured bitrate (AIMD).
6. **Simulcast:** `--stream 640x360@500000:/tmp/lo.h264` → two valid streams at once.
7. **Zero-copy:** check the log for "zero-copy dmabuf ENABLED" vs the memcpy fallback
   (the 4.4 helix driver may not support DMABUF import — fallback is expected & fine).
8. **Compare vs cam_app:** CPU/RSS; confirm HW encoder keeps CPU ~0.
9. **Restore + verify camera healthy** every time.

### Known hardware-validation risks (from V4L2 research)
- **DMABUF import** on the OUTPUT queue may not be supported by this 4.4 vendor driver →
  auto-fallback to memcpy (correctness unaffected, just no zero-copy win).
- **Live bitrate** `S_CTRL` may return `-EBUSY` on some drivers → would then need
  drain→set→resume; verify it applies cleanly first.
- **NV12 plane count / stride**: code assumes single-plane NV12; if the driver reports 2
  planes or padded strides, the converter/scaler offsets need adjusting.
- **Drain**: `VIDIOC_ENCODER_CMD` / `V4L2_BUF_FLAG_LAST` may be unimplemented; `STREAMOFF`
  backstop handles shutdown regardless.

## 8. Integration roadmap (after bring-up)
- **A** — feed `guppy-webrtc` directly (shared buffer we both own) + wire its packet-loss
  feedback into the `--control` socket for end-to-end adaptive bitrate.
- **C** — built-in HTTP MJPEG/snapshot server to also retire `mjpg_streamer`, making
  guppycam the entire camera stack in one process.
