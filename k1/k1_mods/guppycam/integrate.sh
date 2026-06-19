#!/bin/sh
# guppycam <-> guppy-webrtc live integration (REVERSIBLE runtime swap, no boot changes).
#
#   integrate.sh start   - stop cam_app/mjpg_streamer, run guppycam (MJPEG->H.264 memfd +
#                          adaptive control socket), point guppy-webrtc at it with loss feedback.
#   integrate.sh revert  - restore the stock stack (cam_app + mjpg_streamer + guppy-webrtc).
#   integrate.sh status  - show what's running.
#
# Notes:
#  - A reboot always restores the stock stack (cam_app autostarts at boot).
#  - While guppycam is active, the Creality MJPEG cam on :8080 is unavailable (the optional
#    guppycam MJPEG server is a future addition); the Nebula WebRTC cam on :8585 is served by
#    guppycam->guppy-webrtc.
GC=${GC:-/usr/data/guppycam/guppycam}
WEBRTC=${WEBRTC:-/usr/data/guppy-webrtc/guppy-webrtc}
SOCK=/tmp/guppycam.sock
W=${W:-1280}; H=${H:-720}; FPS=${FPS:-15}; BR=${BR:-2000000}
# INPUT=mjpeg is the full stack: captures MJPEG -> serves :8080 (passthrough) +
# transcodes to H.264 for :8585 (with adaptive bitrate). INPUT=auto/h264 is the
# lighter passthrough path (correct image, zero transcode) but serves only :8585.
INPUT=${INPUT:-mjpeg}
MJPEG_PORT=${MJPEG_PORT:-8080}
CAMARGS="-i /dev/v4l/by-id/main-video-4 -t 0 -w 1920 -h 1080 -f 15 -c"
MJPGARGS="-i input_memfd.so -t 0 -o output_http.so -w /usr/share/mjpg-streamer/www/ -p 8080"

gcpid() { p=$(pidof guppycam); [ -z "$p" ] && p=$(pidof guppycam_test); echo "$p"; }

start() {
  echo "stopping stock camera stack..."
  /etc/init.d/S96guppywebrtc stop 2>/dev/null
  kill -9 $(pidof guppy-webrtc) 2>/dev/null
  kill -9 $(pidof mjpg_streamer) 2>/dev/null
  kill -9 $(pidof cam_app) 2>/dev/null
  sleep 3
  echo "starting guppycam (input=$INPUT -> memfd + MJPEG :$MJPEG_PORT, adaptive @ $SOCK)..."
  MJOPT=""; [ "$INPUT" = "mjpeg" ] && MJOPT="--mjpeg $MJPEG_PORT"
  setsid "$GC" --input "$INPUT" -w "$W" -h "$H" -f "$FPS" -b "$BR" --gop "$FPS" \
      --memfd --control "$SOCK" $MJOPT </dev/null >/tmp/guppycam.log 2>&1 &
  sleep 4
  GP=$(gcpid)
  [ -z "$GP" ] && { echo "ERROR: guppycam did not start"; tail -5 /tmp/guppycam.log; return 1; }
  MFD=""
  for f in /proc/$GP/fd/*; do case "$(readlink $f 2>/dev/null)" in *main_memfd*) MFD=$f;; esac; done
  [ -z "$MFD" ] && { echo "ERROR: guppycam memfd not found"; return 1; }
  echo "guppycam pid=$GP memfd=$MFD"
  echo "starting guppy-webrtc -> $MFD (loss feedback -> $SOCK)..."
  setsid "$WEBRTC" "$MFD" 8585 "$SOCK" </dev/null >/tmp/gw.log 2>&1 &
  sleep 3
  echo "guppy-webrtc pid=$(pidof guppy-webrtc)"
  status
}

revert() {
  echo "reverting to stock stack..."
  kill -9 $(pidof guppy-webrtc) 2>/dev/null
  kill -9 $(gcpid) 2>/dev/null
  sleep 2
  setsid /usr/bin/cam_app $CAMARGS </dev/null >/tmp/camapp.log 2>&1 &
  sleep 5
  setsid /usr/bin/mjpg_streamer $MJPGARGS </dev/null >/tmp/mjpg.log 2>&1 &
  sleep 1
  /etc/init.d/S96guppywebrtc start 2>/dev/null
  sleep 3
  status
}

status() {
  echo "  cam_app:[$(pidof cam_app)] guppycam:[$(gcpid)] guppy-webrtc:[$(pidof guppy-webrtc)] mjpg:[$(pidof mjpg_streamer)]"
  echo "  ports:$(netstat -ln 2>/dev/null | grep -oE ':(8080|8585)' | sort -u | tr '\n' ' ')"
}

case "$1" in
  start) start ;;
  revert) revert ;;
  status) status ;;
  *) echo "usage: $0 {start|revert|status}" ;;
esac
