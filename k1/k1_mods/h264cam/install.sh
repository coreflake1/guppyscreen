#!/bin/sh
# GuppyKE — H.264 camera stream installer (Ender-3 V3 KE / K1 family)
#
# Adds a second, hardware-H.264 webcam alongside the stock MJPEG one. It reads
# the camera's already-encoded H.264 straight out of cam_app's shared memfd
# (zero re-encoding, no extra load) and serves it via go2rtc as WebRTC / RTSP /
# MJPEG / HLS / snapshot. The stock Creality MJPEG + app streams are untouched.
#
# Usage:   sh install.sh            # install
#          sh install.sh uninstall  # remove (stock camera left intact)
#
# See wiki/Camera-H264-Stream.md for details.

green="\033[01;32m"; yellow="\033[01;33m"; red="\033[01;31m"; white="\033[m"
SRC="$(dirname "$(readlink -f "$0")")"
DIR=/usr/data/h264cam
INIT=/etc/init.d/S96h264cam
GO2RTC_VER="v1.9.14"
GO2RTC_URL="https://github.com/AlexxIT/go2rtc/releases/download/${GO2RTC_VER}/go2rtc_linux_mipsel"
DLCURL_URL="https://raw.githubusercontent.com/ballaswag/k1-discovery/main/bin/curl"
MOONRAKER="http://localhost:7125"
WEBCAM_NAME="Nebula H264"

detect_ip() {
    ip route get 1.1.1.1 2>/dev/null | grep -oE 'src [0-9.]+' | awk '{print $2}' | head -1
}

uninstall() {
    printf "${green}=== Uninstalling H.264 camera stream ===${white}\n"
    [ -f "$INIT" ] && "$INIT" stop 2>/dev/null
    for p in $(pidof go2rtc) $(pidof memfd_h264_dump); do kill "$p" 2>/dev/null; done
    rm -f "$INIT"
    rm -rf "$DIR"
    # remove the Moonraker webcam entry (URL-encode the space in the name)
    curl -s -X DELETE "$MOONRAKER/server/webcams/item?name=Nebula%20H264" >/dev/null 2>&1
    printf "${green}Removed. The stock Creality camera is untouched.${white}\n"
    exit 0
}

[ "$1" = "uninstall" ] && uninstall

printf "${green}=== Installing H.264 camera stream (go2rtc) ===${white}\n"

# --- sanity: bundled files present ---
for f in memfd_h264_dump h264cam.sh go2rtc.yaml S96h264cam; do
    [ -f "$SRC/$f" ] || { printf "${red}Missing bundled file: $SRC/$f${white}\n"; exit 1; }
done

# --- 1. install files ---
mkdir -p "$DIR"
cp "$SRC/memfd_h264_dump" "$DIR/memfd_h264_dump"
cp "$SRC/h264cam.sh"      "$DIR/h264cam.sh"
cp "$SRC/go2rtc.yaml"     "$DIR/go2rtc.yaml"
cp "$SRC/S96h264cam"      "$INIT"
chmod +x "$DIR/memfd_h264_dump" "$DIR/h264cam.sh" "$INIT"

# --- 2. fetch go2rtc (the printer's curl has no -L; grab a capable curl first) ---
if [ ! -s "$DIR/go2rtc" ]; then
    printf "${green}Downloading go2rtc ${GO2RTC_VER}...${white}\n"
    wget -q --no-check-certificate "$DLCURL_URL" -O /tmp/dlcurl 2>/dev/null && chmod +x /tmp/dlcurl
    if [ -x /tmp/dlcurl ]; then
        /tmp/dlcurl -s -L "$GO2RTC_URL" -o "$DIR/go2rtc"
    else
        wget -q --no-check-certificate "$GO2RTC_URL" -O "$DIR/go2rtc"
    fi
    chmod +x "$DIR/go2rtc" 2>/dev/null
fi
if [ ! -s "$DIR/go2rtc" ]; then
    printf "${red}go2rtc download failed. Check internet / download manually to $DIR/go2rtc${white}\n"; exit 1
fi

# --- 3. register the webcam in Moonraker (Mainsail + Fluidd both read this) ---
IP=$(detect_ip); [ -z "$IP" ] && IP="127.0.0.1"
printf "${green}Registering '${WEBCAM_NAME}' webcam (IP ${IP})...${white}\n"
cat > /tmp/h264_webcam.json <<JEOF
{"name":"${WEBCAM_NAME}","service":"webrtc-go2rtc","target_fps":15,"target_fps_idle":5,"aspect_ratio":"16:9","stream_url":"http://${IP}:1984/stream.html?src=nebula&mode=webrtc","snapshot_url":"http://${IP}:1984/api/frame.jpeg?src=nebula"}
JEOF
curl -s -X POST "$MOONRAKER/server/webcams/item" -H "Content-Type: application/json" -d @/tmp/h264_webcam.json >/dev/null 2>&1
rm -f /tmp/h264_webcam.json

# --- 4. start ---
"$INIT" start
sleep 2

printf "${green}=== Done ===${white}\n"
printf "${white}In Mainsail/Fluidd: pick the '${WEBCAM_NAME}' webcam (alongside the stock one).\n"
printf "Other apps share ONE go2rtc instance — same stream, pick your protocol:\n"
printf "  ${green}RTSP${white}     rtsp://${IP}:8554/nebula            (VLC, Frigate, Home Assistant, Obico, OctoEverywhere)\n"
printf "  ${green}Snapshot${white} http://${IP}:1984/api/frame.jpeg?src=nebula\n"
printf "  ${green}MJPEG${white}    http://${IP}:1984/api/stream.mjpeg?src=nebula\n"
printf "  ${green}HLS${white}      http://${IP}:1984/api/stream.m3u8?src=nebula\n"
printf "  ${green}go2rtc UI${white} http://${IP}:1984/\n"
printf "${yellow}Note: if the printer's IP changes, re-run this installer (the URLs above bake in ${IP}).${white}\n"
printf "${white}Uninstall: sh $0 uninstall${white}\n"
