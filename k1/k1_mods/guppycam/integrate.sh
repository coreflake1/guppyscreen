#!/bin/sh
# guppycam 3-stream integration (REVERSIBLE runtime swap, no boot changes by itself;
# the S99guppycam init script calls this at boot for a permanent setup).
# ONE guppycam process, NO guppy-webrtc:
#   :8554  raw H.264 / WebSocket  -> Mainsail "Raw H264 (jmuxer)"  Local  1080p  (low latency)
#   :8080  MJPEG (native passthrough @ master) /?action=stream|snapshot  Apps  1080p
#   :8081  MJPEG 640x360 (HW JPEG re-encode)   /?action=stream|snapshot  Remote 360p
#
#   integrate.sh start   - stop stock stack, run guppycam, register 3 webcams
#   integrate.sh revert  - restore stock (cam_app + mjpg_streamer + guppy-webrtc)
#   integrate.sh status  - show what's running
# A reboot restores stock unless S99guppycam is installed.
GC=${GC:-/usr/data/guppycam/guppycam}
W=${W:-1920}; H=${H:-1080}; FPS=${FPS:-15}; BR=${BR:-3000000}
WS=8554; MID=8080; LOW=8081
CTLSOCK=/tmp/guppycam.sock

# ============================================================================
# CRITICAL: the Helix codec MUST be released cleanly between camera apps.
# The codec is a single shared HW unit; if a user is killed with `kill -9` while
# streaming, it leaves the codec mid-stream (and reserved-memory buffers dirty),
# and the NEXT app to open it triggers an IRQ storm on CPU1 that HARD-FREEZES the
# board (verified 2026-06-20 via /dev/kmsg). So we NEVER kill -9 a streaming
# camera app. cam_app is stopped via the vendor path (auto_uvc.sh -> SIGTERM +
# wait); guppycam is stopped via SIGTERM + wait (its handler does STREAMOFF +
# encoder drain). -9 is a last resort only after a graceful wait times out.
# ============================================================================
AUTO_UVC=/usr/bin/auto_uvc.sh
MAIN_DEV=main-video-4

gcpid() { p=$(pidof guppycam); [ -z "$p" ] && p=$(pidof guppycam_test); echo "$p"; }
ip() { ip route get 1.1.1.1 2>/dev/null | grep -oE 'src [0-9.]+' | awk '{print $2}' | head -1; }

# Stop guppycam GRACEFULLY so it releases the Helix codec (STREAMOFF + drain).
# SIGTERM -> wait up to ~6s -> only then -9 (which would leave the codec dirty).
gc_stop() {
  p=$(gcpid); [ -z "$p" ] || ! [ -d /proc/$p ] && return 0
  kill -TERM $p 2>/dev/null
  i=0; while [ $i -lt 30 ]; do [ -d /proc/$p ] || { echo "  guppycam stopped cleanly"; return 0; }; i=$((i+1)); sleep 0.2; done
  echo "  guppycam did not exit on SIGTERM -> kill -9 (codec may need a reboot)"; kill -9 $p 2>/dev/null
}

# Build guppycam's --cam-ctrl string from the saved Camera-Image-Tuning values
# (variables.cfg, written by the CAM_* macros). guppycam applies these on its
# OWN fd at startup, so no separate v4l2-ctl ever opens /dev/video4. Falls back
# to the feature's documented defaults when a value hasn't been saved yet.
camctrl() {
  VF=/usr/data/printer_data/config/Helper-Script/variables.cfg
  gv() { v=$(grep -iE "^[[:space:]]*$1[[:space:]]*=" "$VF" 2>/dev/null | head -1 | sed 's/.*=//; s/[^0-9-]//g'); [ -n "$v" ] && echo "$v" || echo "$2"; }
  echo "brightness=$(gv cam_brightness 115),contrast=$(gv cam_contrast 100),saturation=$(gv cam_saturation 95),hue=$(gv cam_hue 50),white_balance_temperature_auto=$(gv cam_wb_auto 1)"
}

# Register exactly 3 webcams (IP only for the jmuxer one, which Mainsail forces to ws://).
# Resolution is shown in the name. Cleans any prior guppycam/stock entries first.
register_webcams() {
  python3 - "$(ip)" "$H" 2>/dev/null <<'PY'
import sys, json, urllib.request, urllib.parse
ip, h = sys.argv[1], sys.argv[2]
B = "http://localhost:7125/server/webcams"
try:
    cur = json.load(urllib.request.urlopen(B + "/list", timeout=5))["result"]["webcams"]
except Exception:
    raise SystemExit
for w in cur:
    if w["name"].startswith("guppycam") or w["name"] in ("Creality Cam", "Nebula WebRTC"):
        try: urllib.request.urlopen(urllib.request.Request(B+"/item?name="+urllib.parse.quote(w["name"]), method="DELETE"), timeout=5)
        except Exception: pass
def reg(n, s, st, sn):
    b = json.dumps({"name": n, "service": s, "target_fps": 15, "aspect_ratio": "16:9",
                    "stream_url": st, "snapshot_url": sn}).encode()
    try: urllib.request.urlopen(urllib.request.Request(B+"/item", data=b, headers={"Content-Type":"application/json"}, method="POST"), timeout=5)
    except Exception as e: print("reg err", n, e)
reg("guppycam Apps (%sp)" % h, "mjpegstreamer-adaptive", "/webcam/?action=stream", "/webcam/?action=snapshot")
reg("guppycam Remote (360p)", "mjpegstreamer-adaptive", "/webcam2/?action=stream", "/webcam2/?action=snapshot")
reg("guppycam Local H264 (%sp)" % h, "jmuxer-stream", "ws://%s:8554/" % ip, "/webcam/?action=snapshot")
PY
}

start() {
  echo "stopping stock camera stack (graceful -> clean codec handoff)..."
  /etc/init.d/S96guppywebrtc stop 2>/dev/null          # guppy-webrtc (reads cam_app's memfd)
  gc_stop                                              # any existing guppycam (graceful)
  # Stop cam_app + mjpg_streamer the VENDOR way: SIGTERM + wait for exit, which
  # lets cam_app STREAMOFF and release the Helix codec. A kill -9 here is what
  # wedged the board. (auto_uvc.sh 'stop' iterates all UVC devices.)
  ACTION=stop "$AUTO_UVC" 2>/dev/null
  sleep 2
  CAMCTRL=$(camctrl)
  echo "starting guppycam (${W}x${H}@${FPS}: WS H264 / :$MID MJPEG / :$LOW 360p; cam: $CAMCTRL)..."
  setsid "$GC" --input mjpeg -w "$W" -h "$H" -f "$FPS" -b "$BR" --gop "$FPS" \
      --ws "$WS" --mjpeg "$MID" --mjpeg-low "$LOW" \
      --control "$CTLSOCK" --cam-ctrl "$CAMCTRL" </dev/null >/tmp/guppycam.log 2>&1 &
  sleep 4
  [ -z "$(gcpid)" ] && { echo "ERROR: guppycam did not start"; tail -5 /tmp/guppycam.log; return 1; }
  echo "registering webcams..."
  register_webcams
  status
}

revert() {
  echo "reverting to stock stack (graceful -> clean codec handoff)..."
  gc_stop                                              # stop guppycam cleanly (releases codec)
  sleep 1
  # Restart the stock camera via the VENDOR launcher (handles cam_app + mjpg_streamer
  # + pidfiles + LD_LIBRARY_PATH exactly as at boot). Re-launching cam_app by hand
  # is fragile (mjpg plugin quoting/env); auto_uvc.sh does it right.
  ACTION=add MDEV=$MAIN_DEV "$AUTO_UVC" 2>/dev/null
  sleep 4
  /etc/init.d/S96guppywebrtc start 2>/dev/null
  sleep 3
  status
}

status() {
  echo "  cam_app:[$(pidof cam_app)] guppycam:[$(gcpid)] guppy-webrtc:[$(pidof guppy-webrtc)] mjpg:[$(pidof mjpg_streamer)]"
  echo "  ports:$(netstat -ln 2>/dev/null | grep -oE ':(8554|8080|8081|8585)' | sort -u | tr '\n' ' ')"
}

case "$1" in
  start) start ;;
  revert) revert ;;
  status) status ;;
  *) echo "usage: $0 {start|revert|status}" ;;
esac
