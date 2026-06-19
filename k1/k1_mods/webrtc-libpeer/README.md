# guppy-webrtc — low-RAM WebRTC camera for the Creality KE

A featherweight WebRTC H.264 stream that forwards **cam_app's hardware-encoded
H.264** to the browser via [libpeer](https://github.com/sepfy/libpeer), with
**on-device signaling and no cloud**. It is a drop-in, low-memory replacement
for the `go2rtc` + `ffmpeg` `h264cam` mod.

## Why

The KE pad has only ~197 MB RAM and swaps to a file on slow eMMC. The `go2rtc`
h264cam stack adds **~44 MB resident** (go2rtc ~33 MB + on-demand ffmpeg) on top
of Creality's own camera stack, and — because go2rtc only has H.264 — every
JPEG **snapshot** Mainsail polls forces a **software H.264→JPEG decode** via
ffmpeg. Under load that pushes the box into eMMC swap-thrash → the whole pad
freezes (SSH included), recoverable only by power-cycle.

This mod does what Creality's own `webrtc` binary does (it too is libpeer under
the hood) but **locally**:

- **~a few MB RSS** instead of ~33 MB — one small static binary, no Go server.
- **Zero software transcode.** It forwards cam_app's hardware H.264 untouched.
- **Snapshots come from the stock hardware MJPEG** (`:8080`), not a decode — so
  the snapshot-decode freeze driver is gone entirely.
- **No cloud / no account.** On the LAN the answer SDP carries the printer's own
  host ICE candidate, so the browser connects peer-to-peer directly.

## How it works

```
cam_app (HW H.264 encoder) ──memfd──▶ guppy-webrtc (libpeer) ──WebRTC──▶ browser
                                          ▲ HTTP signaling (:8585, POST /webrtc)
```

`main.c` serves a self-contained **viewer page** (`GET /`) that performs the
WebRTC handshake in-page against its own `/webrtc` endpoint (POST, accepts both
JSON `{type:"offer",sdp}`→`{type:"answer",sdp}` and raw SDP). We register **that
page** in Mainsail/Fluidd as an **`iframe` webcam** — the iframe owns both ends
of the handshake, which sidesteps interop friction with Mainsail's own
`webrtc-camera-streamer` client (mid/BUNDLE/payload-type/msid quirks in libpeer's
answer). Snapshots still come from the stock hardware MJPEG (`:8080`).
`memfd_reader.c` pulls complete H.264 access units from cam_app's `main_memfd`
(same proven logic as `h264cam/memfd_h264_dump.c`).

## Build

```bash
bash build.sh        # cross-compiles in the guppydev MIPS toolchain image
```
Output: `guppy-webrtc` — ELF 32-bit MIPS, statically linked (musl), ~2.4 MB,
runs on the glibc KE. libpeer is pinned in `build.sh` (`LIBPEER_REF`).

## Install (on the printer)

```sh
sh install.sh             # install + start, registers the Mainsail webcam
sh install.sh uninstall   # stop + remove
```

Standalone test viewer: `http://<printer-ip>:8585/`

## Status / things to verify on-device

- [x] Browser interop: real browser plays the libpeer answer. Fixed the
      mid/BUNDLE match, `sendrecv`→`sendonly`, hardcoded PT 96 → negotiated PT,
      and injected msid; mDNS ICE candidates rewritten to the host IP.
- [x] First-frame latency: viewer shows picture on cam_app's next IDR (GOP ~1–2 s).
- [x] Mainsail integration: registered as an **`iframe`** webcam pointing at the
      built-in viewer (`:8585/`), not the native `webrtc-camera-streamer` service.
- [ ] Measure RSS under a live viewer; compare against go2rtc.
- [ ] Multiple simultaneous viewers (current build serves one session at a time).
