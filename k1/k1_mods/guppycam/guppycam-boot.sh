#!/bin/sh
# guppycam-boot.sh  ACTION [MDEV]
# Main-camera handler for guppycam mode (called by the auto_uvc.sh dispatcher from
# udev). It does NOT start cam_app -> the Helix codec stays pristine (no handoff,
# no IRQ-storm freeze). It also does NOT start guppycam immediately: launching it
# during the boot stampede wedges it. Instead, on add it backgrounds the DELAYED
# starter, which waits for the box to settle and then starts guppycam. Returns
# immediately so udev's RUN handler never blocks.
DIR=/usr/data/guppycam

gc_stop() {
  p=$(pidof guppycam); [ -z "$p" ] && return 0
  kill -TERM $p 2>/dev/null
  i=0; while [ $i -lt 30 ]; do [ -d /proc/$p ] || return 0; i=$((i+1)); sleep 0.2; done
  kill -9 $p 2>/dev/null
}

case "${1:-$ACTION}" in
  add)
    setsid /bin/sh "$DIR/guppycam-delayed-start.sh" </dev/null >/tmp/guppycam-start.log 2>&1 &
    ;;
  remove)
    gc_stop
    ;;
esac
exit 0
