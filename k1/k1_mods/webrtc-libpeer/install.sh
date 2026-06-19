#!/bin/sh
# Install guppy-webrtc: a low-RAM WebRTC H.264 camera stream for the Creality KE
# that forwards cam_app's hardware encoder via libpeer (no go2rtc, no ffmpeg,
# no cloud). Coexists with / replaces the go2rtc h264cam mod.
#
#   sh install.sh            install + start
#   sh install.sh uninstall  stop + remove
set -e
red='\033[0;31m'; green='\033[0;32m'; yellow='\033[0;33m'; white='\033[0m'
DIR=/usr/data/guppy-webrtc
INIT=/etc/init.d/S96guppywebrtc
SRC="$(cd "$(dirname "$0")" && pwd)"

STOCK_WEBRTC=/etc/init.d/S97webrtc
STOCK_WEBRTC_OFF=/etc/init.d/DISABLED_S97webrtc

if [ "$1" = "uninstall" ]; then
  printf "${yellow}Stopping and removing guppy-webrtc...${white}\n"
  [ -x "$INIT" ] && "$INIT" stop 2>/dev/null || true
  curl -s -X DELETE "http://localhost:7125/server/webcams/item?name=Nebula%20WebRTC" >/dev/null 2>&1 || true
  rm -f "$INIT"
  rm -rf "$DIR"
  # Re-enable Creality's stock cloud webrtc that we disabled on install.
  if [ -f "$STOCK_WEBRTC_OFF" ] && [ ! -f "$STOCK_WEBRTC" ]; then
    mv "$STOCK_WEBRTC_OFF" "$STOCK_WEBRTC"
    "$STOCK_WEBRTC" start 2>/dev/null || true
    printf "${green}Re-enabled stock webrtc (S97webrtc).${white}\n"
  fi
  printf "${green}Uninstalled.${white}\n"
  exit 0
fi

[ -f "$SRC/guppy-webrtc" ] || { printf "${red}guppy-webrtc binary missing - run build.sh first${white}\n"; exit 1; }

printf "${green}Installing guppy-webrtc to ${DIR}...${white}\n"
mkdir -p "$DIR"
cp "$SRC/guppy-webrtc" "$DIR/guppy-webrtc"
chmod +x "$DIR/guppy-webrtc"
cp "$SRC/S96guppywebrtc" "$INIT"
chmod +x "$INIT"

# Disable Creality's stock cloud webrtc: guppy-webrtc replaces it locally, and
# leaving it running wastes RAM and can contend for the camera. Renaming with a
# DISABLED_ prefix stops it auto-starting on boot; uninstall restores it.
if [ -f "$STOCK_WEBRTC" ]; then
  "$STOCK_WEBRTC" stop 2>/dev/null || true
  mv "$STOCK_WEBRTC" "$STOCK_WEBRTC_OFF"
  printf "${yellow}Disabled stock cloud webrtc (S97webrtc -> DISABLED_S97webrtc).${white}\n"
fi

# If the heavy go2rtc h264cam mod is installed, point out the overlap.
if [ -x /etc/init.d/S96h264cam ]; then
  printf "${yellow}Note: the go2rtc h264cam mod is also installed (~33MB RSS).\n"
  printf "Consider 'sh /usr/data/h264cam/install.sh uninstall' to reclaim that memory;\n"
  printf "guppy-webrtc replaces it at a fraction of the RAM.${white}\n"
fi

"$INIT" start
sleep 1
printf "${green}=== Done ===${white}\n"
printf "${white}Webcam '${green}Nebula WebRTC${white}' registered in Mainsail/Fluidd (iframe -> built-in viewer).\n"
printf "Standalone test viewer: ${green}http://<printer-ip>:8585/${white}\n"
printf "Logs: ${DIR}/guppy-webrtc.log\n"
