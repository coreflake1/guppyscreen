# guppycam — investigation log & status (parked 2026-06-20)

Open V4L2 camera daemon to replace Creality's closed `cam_app` on the KE
(Ingenic X2000, "Helix" HW codec). This branch (`ke-webcam`) holds all webcam
work so `ke-next` can stay clean for other features. **Status: NOT shippable —
one unresolved blocker (MCU shutdown → 100% CPU → freeze when all 3 streams are
viewed live). guppycam itself is sound; the blocker is a full-system interaction.**

---

## What works (proven on-device)

- **guppycam core**: UVC MJPEG capture → Helix HW JPEG-decode → NV12 → Helix HW
  H.264 encode, + native-H264 passthrough, + HW JPEG re-encode for a low stream.
  All HW paths verified. Snapshot, bitrate/GOP control, adaptive bitrate.
- **3-stream architecture**: `--ws 8554` (H.264 over WebSocket for Mainsail
  jmuxer) + `--mjpeg 8080` (MJPEG passthrough) + `--mjpeg-low 8081` (360p HW JPEG).
  All 3 register as Mainsail webcams and render correct images.
- **In isolation guppycam is rock-solid**: measured **~25% of one core**
  (all-threads; stock cam_app = 12%), **no memory leak**, no fd/thread explosion,
  ran **96s+ under WS+MJPEG client load** with flat RSS/avail. The 360p SW
  downscale is cheap (full 3-stream 24% vs ws-only 15%).
- **Camera image tuning** preserved: `cam_set.sh` routes the CAM_* macros to
  guppycam's control socket (no 2nd opener of /dev/video4). See [[project_guppycam]].

## Root causes found & fixed this session

1. **Codec-handoff freeze (SOLVED).** The Helix codec is a single shared HW unit.
   `kill -9` on a *streaming* camera app leaves it mid-stream/dirty; the next app
   to open it triggers an **IRQ storm on CPU1 → hard freeze** (verified via
   `/dev/kmsg`: `RMEM: source not free, name=cam_app` then freeze right after
   `helix-venc: jpge start streaming`). Fix: never `kill -9` a streaming camera
   app — stop it gracefully (SIGTERM + wait, vendor `auto_uvc.sh stop`) so it
   STREAMOFFs and releases the codec. Verified: after a clean release, guppycam's
   heavy 3-stream ran fine (75 frames). **The "one camera per boot, no hot-swap"
   design eliminates the handoff entirely** (guppycam on a pristine codec).

2. **Boot-stampede wedge (SOLVED).** Starting guppycam during the boot stampede
   (klippy + moonraker + guppyscreen all initialising) wedged it. Fix: **delayed
   start** — wait until guppyscreen + Moonraker are up + a margin, then start
   guppycam (the proven-good "settled box" condition). Verified: guppycam came up
   at ~86–91s, all 3 cameras live in Moonraker.

## The UNRESOLVED blocker

With the redesign, guppycam boots cleanly and all 3 streams work — **but ~60s
after the cameras are open in a browser, Klipper reports the MCU shut down**
(`{"code":"key298","msg":"Can not update MCU mcu config as it is shutdown"}`),
and shortly after the box goes to **100% CPU and freezes**.

Observed sequence (2026-06-20 capture run, box stayed alive long enough to see it):
- guppycam up ~91s, all 3 streams serving, **memory stable ~132 MB** (no leak).
- Top CPU: **`[dhd_dpc]` (Broadcom WiFi driver) 29–36%** (streaming 3 cams to the
  browser over WiFi), guppycam ~20%, guppyscreen ~10%, klippy ~8%.
- **MCU shuts down** while streams still work (box responsive).
- Then **100% CPU** (cascade — klippy/guppyscreen reacting to the dead MCU) → freeze.

**Leading hypothesis:** the combined load of guppycam encoding + heavy WiFi packet
processing for 3 simultaneous streams adds enough scheduling latency that Klipper
can't service the MCU UART (`/dev/ttyS1`) in time → MCU shutdown. cam_app stays
light enough (12%, and fewer/lighter streams) that it never does this. The MCU
shutdown then triggers a CPU-spin cascade → freeze. **Not confirmed** — we were
about to read the exact MCU shutdown reason from klippy.log (Timer-too-close vs
Lost-communication vs Got-EOF) when we parked.

### Why it's hard to reproduce in isolation
Every isolated test (guppycam + synthetic clients, even 96s) was clean. The
failure needs the **full live system**: real browser viewing all 3 streams
**through nginx** over **WiFi**, + guppyscreen + Klipper, on a 197 MB / dual-core
box. Frozen runs also prevented klippy from logging the shutdown reason.

## Next steps when resumed (do A before B)

- **A — capture the mechanism (do first).** The box often stays alive at the MCU-
  shutdown moment → **read `/usr/data/printer_data/logs/klippy.log` for the MCU
  shutdown reason** immediately. That single line (Timer too close / Lost
  communication / Got EOF / Rescheduled in the past) tells us whether it's CPU
  starvation, serial-latency, or a host-process death (OOM). The forensic logger
  `guppycam-freezelog.sh` writes mem/CPU + a fresh `dmesg` to eMMC every 1s
  (survives the freeze) → read `/usr/data/guppycam-freeze.log` and
  `/usr/data/guppycam-freeze-dmesg.txt` after a power-cycle.
- **B — fix per the data.** Likely candidates once A is known:
  - Reduce footprint to give the MCU headroom: **720p not 1080p**, **2 streams not 3**,
    lower fps/bitrate (less WiFi `dhd_dpc` load).
  - If serial-latency: pin Klipper / raise its priority, or move guppycam off the
    MCU's CPU, or reduce IRQ load.
  - If host-process OOM: bound memory.
- Reference: `X2000_PM_*.pdf` (programming manual — UART/serial details) in `docs hw/`.

## File map

| File | Role |
|---|---|
| `guppycam.c`, `guppycam`, `build.sh` | the daemon (source + static mipsel binary) |
| `guppycam-mode.sh` | `install/enable/disable/uninstall/status` — the boot-camera toggle |
| `auto_uvc.dispatch.sh` | installed over `/usr/bin/auto_uvc.sh`; routes main cam to guppycam when enabled, else delegates to saved `auto_uvc.stock` |
| `guppycam-boot.sh` | udev handler: on add, kicks the delayed starter; on remove, graceful stop |
| `guppycam-delayed-start.sh` | waits for settle, re-checks ENABLED flag, starts guppycam + logger + register |
| `guppycam-register.sh` | confirms guppycam alive (clears one-shot marker), registers 3 webcams |
| `guppycam-freezelog.sh` | forensic logger → eMMC every 1s (survives freeze) |
| `integrate.sh` | manual runtime swap (graceful both ways); secondary to the boot toggle |
| `cam_set.sh`, `camera-settings.cfg` | Camera-Image-Tuning routed through guppycam (collision-proof) |
| `S99guppycam` | OBSOLETE old boot hook (superseded by the dispatcher); do not use |

## Safety / recovery (hard-won)

- **One-shot boot safety**: `guppycam-delayed-start.sh` drops `.boot-attempt`
  before starting; `guppycam-register.sh` clears it only after guppycam is
  confirmed alive **120s**. A freeze (hits ~60s after start) leaves the marker →
  next boot **falls back to stock cam_app**. So a single power-cycle auto-recovers.
- If the lingering delayed-start subshell is a worry, it now re-checks the ENABLED
  flag after the settle wait (won't start if disabled meanwhile).
- Manual recovery if needed: race SSH in the early-boot window (~15s) and
  `guppycam-mode.sh disable` / `uninstall`; or unplug the camera + power-cycle (no
  camera → guppycam never starts → boots clean).
- **Never `kill -9` guppycam or cam_app while streaming** — leaves the codec dirty.
  Use SIGTERM + wait (`guppycam-mode`/`integrate.sh` do this).

## On-device changes made (to revert for a clean open-KE 1.1.0 state)

- `/usr/data/guppycam/` (binary + scripts) — guppycam disabled (`guppycam.OFF`).
- `/usr/bin/auto_uvc.sh` → restore from `/usr/bin/auto_uvc.stock` (uninstall does this).
- `/usr/data/printer_data/config/Helper-Script/camera-settings.cfg` → restore
  `.bak-pre-guppycam`.
- nginx `/usr/data/nginx/nginx/nginx.conf` → the `/webcam-h264/` location was added;
  restore `.bak-guppycam`.
- Moonraker webcams: remove `guppycam *`; restore stock entry.
- `/etc/init.d/`: `DISABLED_S99guppycam` (old hook) can be removed.
- guppy-webrtc (`/usr/data/guppy-webrtc/`, `S96guppywebrtc`, `DISABLED_S97webrtc`)
  is from the earlier webrtc-cam mod (also not in 1.1.0/main).
