#!/bin/sh
# cam_set <control> <value> | --list
#
# Apply one camera image-tuning control (brightness/contrast/saturation/hue/
# white_balance_temperature_auto) the SAFE way.
#
# While guppycam owns the camera it is the SINGLE owner of /dev/video4, and all
# controls must go through its control socket. We must NEVER run v4l2-ctl while
# guppycam is up: a second process opening /dev/video4 collides on the UVC
# control bus (-32 EPIPE) and wedges the system - that was the boot freeze.
# v4l2-ctl is used ONLY for the stock cam_app stack (guppycam not running).
#
# Called from the Camera-Image-Tuning macros via [gcode_shell_command cam_set].
SOCK=/tmp/guppycam.sock
CTL="$1"; VAL="$2"
[ -z "$CTL" ] && exit 0

# --list: show current controls (read-only diagnostic).
if [ "$CTL" = "--list" ]; then
  if pidof guppycam >/dev/null 2>&1; then
    echo "guppycam owns the camera; tuned values are saved in variables.cfg (cam_*)."
  else
    v4l2-ctl -d /dev/video4 -l
  fi
  exit 0
fi

if pidof guppycam >/dev/null 2>&1; then
  # guppycam mode: route to its control socket so nothing else opens video4.
  # If the socket isn't up yet (early boot), guppycam already applied the saved
  # values itself at startup (--cam-ctrl), so skipping here is correct & safe.
  if [ -S "$SOCK" ]; then
    python3 - "$SOCK" "cam $CTL $VAL" <<'PY'
import socket, sys
s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
try:
    s.sendto(sys.argv[2].encode(), sys.argv[1])
except Exception as e:
    print("cam_set:", e)
PY
  fi
else
  # stock cam_app stack: guppycam not running, v4l2-ctl is safe (as before).
  v4l2-ctl -d /dev/video4 --set-ctrl "$CTL=$VAL"
fi
exit 0
