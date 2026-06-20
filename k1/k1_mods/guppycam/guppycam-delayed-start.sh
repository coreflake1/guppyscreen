#!/bin/sh
# guppycam-delayed-start.sh  (backgrounded by guppycam-boot.sh on camera add)
#
# Starting guppycam during the boot stampede (klippy + moonraker + guppyscreen all
# initialising at once) wedges it. On a SETTLED box it is rock-solid (verified:
# 750 frames, flat memory). So we WAIT until services are up, then start guppycam
# on the pristine codec. cam_app is never started in this mode -> no handoff.
DIR=/usr/data/guppycam
GC=$DIR/guppycam
ATTEMPT=$DIR/.boot-attempt
W=1920; H=1080; FPS=15; BR=3000000

camctrl() {
  VF=/usr/data/printer_data/config/Helper-Script/variables.cfg
  gv() { v=$(grep -iE "^[[:space:]]*$1[[:space:]]*=" "$VF" 2>/dev/null | head -1 | sed 's/.*=//; s/[^0-9-]//g'); [ -n "$v" ] && echo "$v" || echo "$2"; }
  echo "brightness=$(gv cam_brightness 115),contrast=$(gv cam_contrast 100),saturation=$(gv cam_saturation 95),hue=$(gv cam_hue 50),white_balance_temperature_auto=$(gv cam_wb_auto 1)"
}

# ONE-SHOT SAFETY: a leftover marker means last boot started guppycam but it never
# confirmed alive (probably wedged). This boot, fall back to stock cam_app so there
# is a working camera, and clear the marker. (register.sh clears it on success.)
if [ -f "$ATTEMPT" ]; then
  rm -f "$ATTEMPT"
  echo "guppycam: previous boot did not confirm -> stock cam_app this boot" > /dev/console 2>/dev/null
  ACTION=add MDEV=main-video-4 /bin/sh /usr/bin/auto_uvc.stock main-video-4
  exit 0
fi

# Wait for the box to SETTLE: guppyscreen running AND Moonraker answering, then a margin.
i=0
while [ $i -lt 150 ]; do
  if pidof guppyscreen >/dev/null 2>&1 && wget -q -O /dev/null http://localhost:7125/server/info 2>/dev/null; then break; fi
  i=$((i + 1)); sleep 1
done
sleep 12   # extra settle margin so guppycam starts cleanly, not mid-startup-burst

# Re-check the ENABLED flag here: if guppycam mode was disabled while we waited
# (e.g. a recovery), do NOT start guppycam (this closes the lingering-subshell hole).
[ -f "$DIR/ENABLED" ] || { echo "guppycam: disabled during settle -> not starting" > /dev/console 2>/dev/null; exit 0; }
[ -x "$GC" ] || exit 0
: > "$ATTEMPT"                          # arm one-shot (register.sh clears it on success)
# Forensic logger (persists to eMMC across a freeze) - enabled for the capture run.
setsid /bin/sh "$DIR/guppycam-freezelog.sh" </dev/null >/dev/null 2>&1 &
CAMCTRL=$(camctrl)
setsid "$GC" --input mjpeg -w "$W" -h "$H" -f "$FPS" -b "$BR" --gop "$FPS" \
    --ws 8554 --mjpeg 8080 --mjpeg-low 8081 --control /tmp/guppycam.sock \
    --cam-ctrl "$CAMCTRL" </dev/null >/tmp/guppycam.log 2>&1 &
setsid /bin/sh "$DIR/guppycam-register.sh" </dev/null >/tmp/guppycam-reg.log 2>&1 &
exit 0
