#!/bin/sh
# guppycam dispatcher  (installed AS /usr/bin/auto_uvc.sh; the stock original is
# saved as /usr/bin/auto_uvc.stock by guppycam-mode.sh).
#
# This is invoked by udev (60-persistent-v4l.rules) with ENV ACTION + MDEV when a
# camera appears/disappears. When guppycam mode is ENABLED, the MAIN camera is
# served by guppycam on a PRISTINE Helix codec -- cam_app NEVER opens it, which
# removes the codec-handoff freeze by design. Everything else (sub camera, mode
# disabled, any other action) delegates to the untouched stock launcher.
DIR=/usr/data/guppycam

if [ -f "$DIR/ENABLED" ] && [ -x "$DIR/guppycam" ]; then
  case "${MDEV}" in
    main-video*)
      exec /bin/sh "$DIR/guppycam-boot.sh" "${ACTION}" "${MDEV}"
      ;;
  esac
fi

# stock behaviour (cam_app + mjpg_streamer), unchanged
exec /bin/sh /usr/bin/auto_uvc.stock "$@"
