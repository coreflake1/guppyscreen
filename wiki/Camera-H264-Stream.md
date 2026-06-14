# Hardware H.264 Camera Stream (Nebula)

The Creality **Nebula** camera streams to Mainsail as **MJPEG** by default — which works, but it's heavy (~14 Mbps for 1080p) and choppy. The camera's hardware *also* encodes **H.264** the whole time (that's what the Creality app uses), but Creality only exposes it through their cloud. This mod taps that hardware H.264 **locally** and serves it via [go2rtc](https://github.com/AlexxIT/go2rtc) as a second webcam — **WebRTC, RTSP, MJPEG, HLS, snapshot** — alongside the stock MJPEG, which stays untouched.

**Why you'd want it:** ~14× less bandwidth, lower latency, smoother motion — much easier on the KE's WiFi and far better for remote/mobile viewing. (It is *not* sharper per-frame — see [Honest limitations](#honest-limitations).)

---

## How it works (short version)

`cam_app` (Creality's camera daemon) hardware-encodes H.264 continuously into a shared `memfd`. A tiny reader maps that buffer and emits the H.264 elementary stream (a pure *copy* — **no re-encoding**, so negligible CPU), piped through `ffmpeg -c copy` into **go2rtc**, which serves every common protocol. Nothing about the stock camera pipeline changes; we just add a second *reader* of the buffer (exactly like Creality's own webrtc binary does).

### What this guide was tested on

| | |
|---|---|
| Printer | Creality Ender-3 V3 KE |
| Camera | Creality **Nebula** (1080p, hardware H.264) |
| go2rtc | v1.9.14 (`linux_mipsel`, static) |
| Interface | Mainsail (Fluidd works too — both read Moonraker's webcam list) |

---

## Install

Over SSH (the mod ships in `…/guppyscreen/k1_mods/h264cam/`):

```sh
sh /usr/data/guppyscreen/k1_mods/h264cam/install.sh
```

It will: copy the reader + wrapper + config to `/usr/data/h264cam/`, download the pinned go2rtc binary, install a boot service (`/etc/init.d/S96h264cam`), register a **"Nebula H264"** webcam in Moonraker (auto-detecting your printer's IP), and start it.

Then in Mainsail/Fluidd, open the webcam panel and pick **"Nebula H264"** (it sits next to the stock "Creality Cam").

> ⏱️ First load takes a few seconds — go2rtc spins the pipeline up **on-demand** (idle = ~no CPU), then does the WebRTC handshake and waits for a keyframe. After that it's smooth. This is the same on-demand behaviour Creality's own stream uses.

---

## Other apps — one go2rtc, every protocol

All clients point at the **same** go2rtc instance (the `nebula` stream); each just uses the URL for the protocol it speaks. Replace `<printer-ip>`:

| Client | Protocol | URL |
|---|---|---|
| **Mainsail / Fluidd** | WebRTC | auto-configured by the installer |
| **VLC, Frigate, Home Assistant, Obico, OctoEverywhere, mobile apps** | **RTSP** | `rtsp://<printer-ip>:8554/nebula` |
| Still image / thumbnail | JPEG | `http://<printer-ip>:1984/api/frame.jpeg?src=nebula` |
| Legacy MJPEG-only clients | MJPEG | `http://<printer-ip>:1984/api/stream.mjpeg?src=nebula` |
| HLS players | HLS | `http://<printer-ip>:1984/api/stream.m3u8?src=nebula` |
| Browser / diagnostics | go2rtc UI | `http://<printer-ip>:1984/` |

**RTSP is the universal one** — almost every other app speaks it. WebRTC and RTSP are *passthrough* (instant, no transcode); the JPEG/MJPEG/HLS endpoints make go2rtc transcode, so they have a slower first hit.

### Remote (off-LAN) viewing
Ports `1984` / `8554` / `8555` must be reachable. Either port-forward/VPN, or — recommended — run a relay service (**Obico**, **GuppyFLO**, **OctoEverywhere**) that consumes the local `rtsp://…:8554/nebula` and tunnels it out. The low H.264 bitrate is exactly what makes remote viewing pleasant.

---

## Honest limitations

- **Quality ceiling ≈ 1 Mbps / 1080p15.** The camera's H.264 bitrate/QP is fixed inside `cam_app` and **cannot be raised** — there's no CLI flag, the live V4L2 controls are per-file-handle (an external `v4l2-ctl` can't reach cam_app's session), and the defaults live in a shared library linked by ~380 system binaries (including boot/display), so patching it is unsafe. This is *the same stream the Creality app shows.*
- **Not sharper than MJPEG per-frame** — the stock MJPEG actually uses a higher bitrate. H.264's win is **bandwidth + latency + smoothness**, not still-image detail.
- The image controls still apply: the `CAM_*` macros (see [Camera Image Tuning](Camera-Image-Tuning.md)) act on the capture *upstream* of both encoders, so brightness/contrast/saturation affect this stream too.

---

## Uninstall

```sh
sh /usr/data/guppyscreen/k1_mods/h264cam/install.sh uninstall
```
Stops + removes the service, deletes `/usr/data/h264cam/`, and removes the Moonraker webcam entry. The stock Creality camera is completely untouched, so you're back to exactly before.

---

## Troubleshooting

- **Webcam stuck on "connecting" in Mainsail** — go2rtc rejecting the cross-origin WebSocket. The shipped `go2rtc.yaml` sets `api: origin: "*"` to allow it; if you edited the config, make sure that line is present, then restart: `/etc/init.d/S96h264cam restart`.
- **Slow/black on first open** — expected cold-start (on-demand pipeline + keyframe wait, a few seconds). If it never loads, check `/usr/data/h264cam/go2rtc.log`.
- **Nothing after a reboot** — confirm `pidof go2rtc` returns a PID; `/etc/init.d/S96h264cam start` to start manually. (The init script uses `pidof`/`kill`, not `pkill`, which isn't present on this firmware.)
- **Wrong IP after a DHCP change** — the webcam/URLs bake in the IP detected at install; just re-run `install.sh`.
- **A firmware update wiped it** — re-run `install.sh` (everything lives in `/usr/data` + `/etc/init.d`, which a Creality FW update can reset).
