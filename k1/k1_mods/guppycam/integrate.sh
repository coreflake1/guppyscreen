#!/bin/sh
# guppycam 3-stream integration (REVERSIBLE runtime swap, no boot changes).
# ONE guppycam process, NO guppy-webrtc:
#   :8554  raw H.264 over WebSocket  -> Mainsail "Raw H264 (jmuxer)"   (local, low-latency)
#   :8080  MJPEG (native passthrough @ master res) /?action=stream|snapshot (apps)
#   :8081  MJPEG 640x360 (HW JPEG)            /?action=stream|snapshot (remote/mobile)
#
#   integrate.sh start   - stop stock stack, run guppycam (3 streams), register webcams
#   integrate.sh revert  - restore stock (cam_app + mjpg_streamer + guppy-webrtc)
#   integrate.sh status  - show what's running
# A reboot also restores stock (cam_app autostarts at boot).
GC=${GC:-/usr/data/guppycam/guppycam}
W=${W:-1280}; H=${H:-720}; FPS=${FPS:-15}; BR=${BR:-2000000}
WS=8554; MID=8080; LOW=8081
MOON="http://localhost:7125"
CAMARGS="-i /dev/v4l/by-id/main-video-4 -t 0 -w 1920 -h 1080 -f 15 -c"
MJPGARGS="-i input_memfd.so -t 0 -o output_http.so -w /usr/share/mjpg-streamer/www/ -p 8080"

gcpid() { p=$(pidof guppycam); [ -z "$p" ] && p=$(pidof guppycam_test); echo "$p"; }
ip() { ip route get 1.1.1.1 2>/dev/null | grep -oE 'src [0-9.]+' | awk '{print $2}' | head -1; }

reg() { # name service streamurl snapurl  (busybox curl rejects -s/-X/-d, so use python3)
  python3 - "$1" "$2" "$3" "$4" 2>/dev/null <<'PY'
import sys, json, urllib.request
n, s, st, sn = sys.argv[1:5]
b = json.dumps({"name": n, "service": s, "target_fps": 15, "aspect_ratio": "16:9",
                "stream_url": st, "snapshot_url": sn}).encode()
try:
    urllib.request.urlopen(urllib.request.Request(
        "http://localhost:7125/server/webcams/item", data=b,
        headers={"Content-Type": "application/json"}, method="POST"), timeout=5)
except Exception as e:
    print("reg err", n, e)
PY
}
del_webcam() { # name
  python3 -c "import urllib.request,urllib.parse; urllib.request.urlopen(urllib.request.Request('http://localhost:7125/server/webcams/item?name='+urllib.parse.quote('$1'),method='DELETE'),timeout=5)" 2>/dev/null
}

# Idempotently add an nginx WebSocket proxy /webcam-h264/ -> 127.0.0.1:8554 so the
# H.264 (jmuxer) stream works via a relative path (no hard-coded IP -> survives
# DHCP changes). Validates with `nginx -t` and only reloads on success.
ensure_nginx_ws() {
  python3 - <<'PY' 2>&1
import subprocess, shutil
CONF="/usr/data/nginx/nginx/nginx.conf"; NGINX="/usr/data/nginx/sbin/nginx"
block=("""        location /webcam-h264/ {
            proxy_pass http://127.0.0.1:8554/;
            proxy_http_version 1.1;
            proxy_set_header Upgrade $http_upgrade;
            proxy_set_header Connection $connection_upgrade;
            proxy_buffering off;
            proxy_read_timeout 86400s;
        }
""")
try:
    src=open(CONF).read()
except Exception as e:
    print("nginx: cannot read config:",e); raise SystemExit
if "location /webcam-h264/" in src:
    print("nginx: /webcam-h264/ already present"); raise SystemExit
shutil.copy(CONF, CONF+".bak-guppycam")
open(CONF,"w").write(src.replace("        location /webcam/ {", block+"\n        location /webcam/ {"))
r=subprocess.run([NGINX,"-t","-c",CONF],capture_output=True,text=True)
if r.returncode==0:
    subprocess.run([NGINX,"-s","reload","-c",CONF]); print("nginx: added /webcam-h264/ + reloaded")
else:
    shutil.copy(CONF+".bak-guppycam",CONF); print("nginx: -t FAILED, restored backup")
PY
}

start() {
  echo "stopping stock camera stack..."
  /etc/init.d/S96guppywebrtc stop 2>/dev/null
  kill -9 $(pidof guppy-webrtc) $(pidof guppy-webrtc_test) 2>/dev/null
  kill -9 $(pidof mjpg_streamer) 2>/dev/null
  kill -9 $(pidof cam_app) 2>/dev/null
  sleep 3
  echo "starting guppycam (3 streams: WS:$WS H264, :$MID mid MJPEG, :$LOW low MJPEG)..."
  setsid "$GC" --input mjpeg -w "$W" -h "$H" -f "$FPS" -b "$BR" --gop "$FPS" \
      --ws "$WS" --mjpeg "$MID" --mjpeg-low "$LOW" </dev/null >/tmp/guppycam.log 2>&1 &
  sleep 4
  [ -z "$(gcpid)" ] && { echo "ERROR: guppycam did not start"; tail -5 /tmp/guppycam.log; return 1; }
  echo "configuring nginx + registering 3 IP-free webcams..."
  ensure_nginx_ws
  # exactly 3 streams, all relative (IP-drift-proof); remove leftovers
  del_webcam "Nebula WebRTC"
  del_webcam "Creality Cam"
  reg "guppycam (Apps)"        "mjpegstreamer-adaptive" "/webcam/?action=stream"   "/webcam/?action=snapshot"
  reg "guppycam Remote (low)"  "mjpegstreamer-adaptive" "/webcam2/?action=stream"  "/webcam2/?action=snapshot"
  reg "guppycam Local (H264)"  "jmuxer-stream"          "/webcam-h264/"            "/webcam/?action=snapshot"
  status
}

revert() {
  echo "reverting to stock stack..."
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
  echo "  ports:$(netstat -ln 2>/dev/null | grep -oE ':(8554|8080|8081|8585)' | sort -u | tr '\n' ' ')"
}

case "$1" in
  start) start ;;
  revert) revert ;;
  status) status ;;
  *) echo "usage: $0 {start|revert|status}" ;;
esac
