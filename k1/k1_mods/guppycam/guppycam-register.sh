#!/bin/sh
# guppycam-register.sh
# Runs in the background after guppycam starts at boot. Waits until guppycam is
# confirmed alive AND Moonraker is up, registers the 3 webcams, then clears the
# one-shot boot marker (signalling "this boot is good"). If guppycam dies/wedges
# before confirmation, the marker is left in place so the next boot falls back to
# stock. Registration is pure HTTP -- it never touches the codec.
DIR=/usr/data/guppycam
ATTEMPT=$DIR/.boot-attempt
H=1080

ip() { ip route get 1.1.1.1 2>/dev/null | grep -oE 'src [0-9.]+' | awk '{print $2}' | head -1; }

# 1) confirm guppycam is actually alive and STAYED up (60s). This must be longer
# than any observed wedge delay (the boot wedge hit ~36s after start) so that a
# wedge leaves the one-shot marker in place -> next boot falls back to stock.
i=0
while [ $i -lt 120 ]; do
  pidof guppycam >/dev/null 2>&1 || exit 0   # guppycam gone -> leave marker, bail
  i=$((i + 1)); sleep 1
done

# guppycam survived the danger window -> this boot is good. Clear the one-shot marker.
rm -f "$ATTEMPT"

# 2) wait for Moonraker, then (re)register the 3 webcams.
i=0
while [ $i -lt 60 ]; do
  wget -q -O /dev/null http://localhost:7125/server/info 2>/dev/null && break
  i=$((i + 1)); sleep 2
done

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
exit 0
