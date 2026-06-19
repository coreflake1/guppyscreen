#!/bin/bash
# Cross-compile guppy-webrtc (libpeer + our glue) into a static mipsel binary
# for the Creality KE. Runs the build inside the guppydev MIPS toolchain image,
# same as scripts/build-mips.sh. Output: ./guppy-webrtc (ELF 32-bit MIPS, static).
set -e

LIBPEER_REF=9319aa434cb9e893faed0293ba9d2a21eca59c8b   # pinned, validated commit
IMAGE=ballaswag/guppydev:latest
HERE="$(cd "$(dirname "$0")" && pwd)"

docker run --rm -e GUPPY_WEBRTC_DEBUG -v "$HERE":/work -w /work "$IMAGE" bash -lc '
set -e
TC=/toolchains/mips32el--musl--stable-2024.02-1
GCC=$TC/bin/mipsel-linux-gcc
LP=/work/.libpeer

if [ ! -d "$LP/.git" ]; then
  git clone https://github.com/sepfy/libpeer "$LP"
fi
cd "$LP"
git fetch --all -q || true
git checkout -f -q '"$LIBPEER_REF"'   # -f: discard prior in-tree patches so builds are deterministic
git submodule update --init --recursive

# Patch: advertise packetization-mode=1 in the H.264 answer. cam_app emits
# 1080p frames that must be FU-A fragmented; without mode 1 the browser assumes
# single-NAL mode and silently drops every fragment (connects but black video).
# This matches the fmtp Creality ships in their own (libpeer-based) webrtc binary.
sed -i "s/profile-level-id=42e01f;level-asymmetry-allowed=1/profile-level-id=42e01f;packetization-mode=1;level-asymmetry-allowed=1/" src/sdp.c
grep -q "packetization-mode=1" src/sdp.c || { echo "PATCH FAILED: sdp.c H264 fmtp line changed upstream"; exit 1; }

# Quiet the per-RTP-packet INFO spam so the log stays readable (keeps ICE/DTLS INFO).
sed -i "s/LOGI(\"markbit/LOGD(\"markbit/" src/rtp.c || true

# Demote the "Only UDP transport is supported" line from ERROR to DEBUG: it fires
# once per TCP ICE candidate the browser offers (Chrome always sends TCP host
# candidates), which libpeer correctly ignores - normal, non-actionable, and
# misleading at ERROR level. Stays visible under GUPPY_WEBRTC_DEBUG.
sed -i "s/LOGE(\"Only UDP transport is supported\")/LOGD(\"Only UDP transport is supported\")/" src/ice.c || true

# cam_app streams 15 fps; libpeer hardcodes the RTP timestamp step for 30 fps.
sed -i "s#rtp_encoder->timestamp_increment = 90000 / 30;#rtp_encoder->timestamp_increment = 90000 / 15;#" src/rtp.c

# DEBUG diag build: set GUPPY_WEBRTC_DEBUG=1 to enable libpeer LOGD (DTLS/agent step trace).
[ -n "$GUPPY_WEBRTC_DEBUG" ] && sed -i "s/#define LOG_LEVEL LEVEL_INFO/#define LOG_LEVEL LEVEL_DEBUG/" src/utils.h
[ -n "$GUPPY_WEBRTC_DEBUG" ] && sed -i "s/#define CONFIG_MBEDTLS_DEBUG 0/#define CONFIG_MBEDTLS_DEBUG 1/" src/config.h

# DEBUG: log the first bytes of the first few outgoing RTP packets (pre-encrypt)
# to verify the wire header (data[0] should be 0x80; data[1] = marker|PT).
[ -n "$GUPPY_WEBRTC_DEBUG" ] && perl -0pi -e "s/(static void peer_connection_outgoing_rtp_packet\(uint8_t\* data, size_t size, void\* user_data\) \{\n  PeerConnection\* pc = \(PeerConnection\*\)user_data;)/\1\n  {static int _n=0; if(_n++<8) fprintf(stderr,\"RTPWIRE b0=%02x b1=%02x seq=%02x%02x ts=%02x%02x%02x%02x ssrc=%02x%02x%02x%02x nal=%02x sz=%zu\\\\n\", data[0],data[1],data[2],data[3],data[4],data[5],data[6],data[7],data[8],data[9],data[10],data[11],data[12],size);}/" src/peer_connection.c

# Patch: the DTLS recv callback returns 0 on timeout, which mbedtls treats as
# EOF (-0x7280) and aborts the handshake after a single ClientHello - no DTLS
# retransmission. Return MBEDTLS_ERR_SSL_WANT_READ (-0x6900) instead so mbedtls
# retransmits the ClientHello until the browser side is ready.
perl -0pi -e "s/recv_max\+\+;\n  \}\n  return ret;/recv_max++;\n  }\n  return ret > 0 ? ret : -0x6900;/" src/peer_connection.c
grep -q "ret > 0 ? ret : -0x6900" src/peer_connection.c || { echo "PATCH FAILED: dtls recv return"; exit 1; }
# Shorten the recv poll window so WANT_READ is returned ~every 300ms, letting the
# DTLS handshake retransmit promptly instead of every 3s.
sed -i "s/#define CONFIG_TLS_READ_TIMEOUT 3000/#define CONFIG_TLS_READ_TIMEOUT 300/" src/config.h

# Add a setter so our glue can stamp outgoing RTP with the browser-negotiated
# H.264 payload type (libpeer hardcodes PT 96; browsers/Mainsail use dynamic PTs).
cat >> src/peer_connection.c <<EOFPT

void peer_connection_set_video_pt(PeerConnection* pc, int pt) { pc->vrtp_encoder.type = pt; }
void peer_connection_refresh_pairs(PeerConnection* pc) { pc->agent.candidate_pairs_num = 0; agent_update_candidate_pairs(&pc->agent); }
void peer_connection_restart_ice(PeerConnection* pc) { if (pc->state != PEER_CONNECTION_COMPLETED && pc->state != PEER_CONNECTION_CONNECTED) pc->state = PEER_CONNECTION_CHECKING; }
EOFPT

# DTLS role for answers: keep libpeer default CLIENT/active role. The client
# handshake path is the well-tested one; with the WANT_READ recv fix above it
# retransmits the ClientHello until the browser DTLS server side responds.
# NOTE: the SERVER role was tried and hit mbedtls DECODE_ERROR in the cookie path.

# Configure + build libpeer deps (mbedtls/srtp2/usrsctp/cjson) and the lib,
# all static against the musl mipsel toolchain.
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=/work/mips-musl-static.cmake \
  -DBUILD_SHARED_LIBS=OFF -DENABLE_TESTS=OFF >/dev/null
cmake --build build --target cjson mbedtls srtp2 usrsctp -j"$(nproc)"
cmake --build build --target peer -j"$(nproc)"

# Compile our glue and statically link everything.
D=build/dist
$GCC -Os -static -o /work/guppy-webrtc \
  /work/main.c /work/memfd_reader.c \
  -I"$LP/include" -I"$D/include" -I"$D/include/cjson" \
  build/src/libpeer.a \
  "$D/lib/libsrtp2.a" "$D/lib/libusrsctp.a" \
  "$D/lib/libmbedtls.a" "$D/lib/libmbedx509.a" "$D/lib/libmbedcrypto.a" \
  "$D/lib/libcjson.a" \
  -lpthread -lm
$TC/bin/mipsel-linux-strip /work/guppy-webrtc
'
echo "=== built: $HERE/guppy-webrtc ==="
file "$HERE/guppy-webrtc" 2>/dev/null || true
ls -la "$HERE/guppy-webrtc"
