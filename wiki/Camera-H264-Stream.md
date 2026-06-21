# Hardware H.264 Camera Stream (Nebula) — removed in v1.2.0

> **This feature has been removed.** Starting in **`v1.2.0-OpenKE`**, OpenKE no longer ships or installs
> the hardware H.264 camera stream (the [go2rtc](https://github.com/AlexxIT/go2rtc) add-on). If you had it,
> **re-run the installer** and it removes it automatically (the on-screen Update Guppy button only swaps
> the binary — it won't remove go2rtc). The stock camera and the persistent
> [image tuning](Camera-Image-Tuning) are unaffected.

## Why it was removed

The KE has only **197 MB of RAM**. The H.264 add-on ran **go2rtc + ffmpeg (~44 MB)** on top of the stock
camera stack — and that extra memory pressure is the main driver of the **freeze symptom** users hit:
the screen goes dark, SSH stops responding, and only a power-cycle recovers it (memory exhaustion →
swap-thrash on the eMMC). Removing the add-on and going back to the stock camera is the reliable fix.

The H.264 win was bandwidth/latency, not image quality (the bitrate was fixed at ~1 Mbps/1080p and the
stock MJPEG is actually higher per-frame). On this box that trade-off isn't worth the instability.

## What happens when you upgrade

**Re-run the [installer](Installation) once** — the removal lives in the installer, so it runs there (not
via the on-screen **Update Guppy** button, which only swaps the screen binary). When you re-run the
installer it detects an existing go2rtc install and removes it automatically:

- stops `/etc/init.d/S96h264cam` and kills `go2rtc` / `memfd_h264_dump`,
- deletes `/etc/init.d/S96h264cam` and `/usr/data/h264cam/`,
- removes the **"Nebula H264"** webcam from Mainsail/Moonraker.

> ⚠️ Updating through the screen alone will **not** free the memory — it leaves go2rtc running. Re-run the
> installer to actually fix the freeze. (If you never installed the H.264 stream, there's nothing to do.)

Your **stock camera** ("Creality Cam", MJPEG) and the persistent image-tuning macros are left exactly as
they were. You'll get back ~44 MB of free RAM.

## Confirm it's gone

```sh
pidof go2rtc            # (empty)
ls /usr/data/h264cam    # No such file or directory
ls /etc/init.d/S96h264cam   # No such file or directory
```

If for some reason go2rtc is still present (e.g. you never re-ran the updater), remove it by re-running
the OpenKE installer once — the removal runs unconditionally on every install/upgrade.

## I relied on the H.264 stream in another app

If you pointed an external app (Frigate, Home Assistant, Obico, OctoEverywhere, VLC…) at
`rtsp://<printer-ip>:8554/nebula` or the go2rtc URLs on port `1984`, switch it to the **stock camera**
feed instead (the MJPEG stream Mainsail uses). The `nebula` go2rtc endpoints no longer exist.

---

*Looking to improve the picture? The camera's brightness/contrast/saturation can still be tuned and made
to persist across reboots — see [Camera Image Tuning](Camera-Image-Tuning).*
