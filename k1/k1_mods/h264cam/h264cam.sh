#!/bin/sh
# go2rtc exec source: $1 = {output} rtsp URL provided by go2rtc
OUT="$1"
DIR=/usr/data/h264cam
# wait for cam_app to be up (camera hotplug may lag boot)
CAMPID=""
for i in $(seq 1 60); do CAMPID=$(pidof cam_app); [ -n "$CAMPID" ] && break; sleep 1; done
[ -z "$CAMPID" ] && { echo "h264cam: cam_app not running" >&2; exit 1; }
# find the H.264 main_memfd fd cam_app holds (not the mjpg one)
FD=""
for f in /proc/$CAMPID/fd/*; do
  l=$(readlink "$f" 2>/dev/null)
  case "$l" in *mjpg_main_memfd*) : ;; *main_memfd*) FD=${f##*/} ;; esac
done
[ -z "$FD" ] && { echo "h264cam: no main_memfd fd on cam_app($CAMPID)" >&2; exit 1; }
echo "h264cam: cam_app=$CAMPID fd=$FD -> $OUT" >&2
exec sh -c "$DIR/memfd_h264_dump /proc/$CAMPID/fd/$FD - 0 | /usr/bin/ffmpeg -hide_banner -loglevel warning -fflags +genpts -f h264 -i pipe:0 -c:v copy -rtsp_transport tcp -f rtsp '$OUT'"
