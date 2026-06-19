/* guppy-webrtc - low-RAM WebRTC H.264 streamer for the Creality KE.
 *
 * Forwards cam_app's hardware-encoded H.264 (read from its memfd) to a browser
 * over WebRTC via libpeer, with on-device HTTP signaling and no cloud. On the
 * LAN the answer SDP carries the printer's own host ICE candidate, so the
 * browser connects peer-to-peer directly - no STUN/TURN/Creality cloud.
 *
 * This is a drop-in low-memory alternative to the go2rtc + ffmpeg h264cam
 * stack: ~a few MB RSS (vs go2rtc's ~33 MB) and zero software transcode.
 *
 * Signaling endpoint (POST /webrtc) accepts both JSON {type:"offer",sdp} ->
 * {type:"answer",sdp} and raw SDP offer->answer. GET / serves a self-contained
 * viewer that does the handshake in-page; we register that page in
 * Mainsail/Fluidd as an "iframe" webcam (the iframe controls both ends of the
 * handshake, sidestepping Mainsail's own webrtc-camera-streamer client).
 *
 *   usage: guppy-webrtc <h264_memfd_path> [http_port]   (default port 8585)
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include "cJSON.h"
#include "memfd_reader.h"
#include "peer.h"

static volatile int g_run = 1;
static PeerConnection* g_pc = NULL;
static pthread_mutex_t g_pc_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile PeerConnectionState g_state = PEER_CONNECTION_CLOSED;
static MemfdReader* g_reader = NULL;
/* Optional: forward browser-reported packet loss to guppycam's adaptive-bitrate
 * control socket, closing the loop (bad link -> encoder backs off). */
static int g_ctrl_fd = -1;
static struct sockaddr_un g_ctrl_addr;

static const char VIEWER_HTML[] =
    "<!doctype html><meta charset=utf-8><title>Guppy WebRTC</title>"
    "<style>body{margin:0;background:#000}video{width:100vw;height:100vh;object-fit:contain}</style>"
    "<video id=v autoplay playsinline muted></video><script>"
    "(async()=>{const pc=new RTCPeerConnection();"
    "pc.addTransceiver('video',{direction:'recvonly'});"
    "pc.ontrack=e=>{document.getElementById('v').srcObject=new MediaStream([e.track])};"
    "const o=await pc.createOffer();await pc.setLocalDescription(o);"
    "await new Promise(r=>{if(pc.iceGatheringState==='complete')r();"
    "else pc.onicegatheringstatechange=()=>pc.iceGatheringState==='complete'&&r()});"
    "const res=await fetch('/webrtc',{method:'POST',headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({type:'offer',sdp:pc.localDescription.sdp})});"
    "await pc.setRemoteDescription(await res.json());})();"
    "</script>";

static void on_state(PeerConnectionState s, void* u) {
  (void)u;
  g_state = s;
}

/* libpeer fires this from RTCP receiver reports (fraction_loss in 0..1). Forward
 * it as "loss <percent>" to guppycam's control socket for adaptive bitrate. */
static void on_loss(float fraction_loss, uint32_t total_loss, void* u) {
  (void)total_loss;
  (void)u;
  if (g_ctrl_fd < 0) return;
  char msg[32];
  int n = snprintf(msg, sizeof(msg), "loss %.1f", fraction_loss * 100.0f);
  sendto(g_ctrl_fd, msg, n, MSG_DONTWAIT, (struct sockaddr*)&g_ctrl_addr, sizeof(g_ctrl_addr));
}

/* Chrome obfuscates its host ICE candidates as "<hash>.local" mDNS names; the
 * real UDP port is kept, only the address is hidden. libpeer can't resolve them
 * and has no peer-reflexive fallback, so it ends up with zero remote candidates
 * and ICE fails. We know the browser's real LAN IP (the source address of this
 * HTTP request), so rewrite every "*.local" token in the offer to that IP -
 * reconstructing a valid host candidate that works on any LAN. */
static void rewrite_mdns(const char* in, const char* ip, char* out, size_t cap) {
  size_t oi = 0;
  const char* p = in;
  while (*p && oi + 1 < cap) {
    if (*p == ' ' || *p == '\r' || *p == '\n' || *p == '\t') {
      out[oi++] = *p++;
      continue;
    }
    const char* start = p;
    while (*p && !(*p == ' ' || *p == '\r' || *p == '\n' || *p == '\t')) p++;
    size_t tlen = (size_t)(p - start);
    const char* repl = start;
    size_t rlen = tlen;
    if (tlen >= 6 && strncmp(p - 6, ".local", 6) == 0) {
      repl = ip;
      rlen = strlen(ip);
    }
    if (oi + rlen >= cap) break;
    memcpy(out + oi, repl, rlen);
    oi += rlen;
  }
  out[oi < cap ? oi : cap - 1] = 0;
}

/* libpeer setter (defined in patched src/peer_connection.c) to make the RTP
 * encoder stamp packets with the browser's negotiated H.264 payload type
 * instead of libpeer's hardcoded 96. */
void peer_connection_set_video_pt(PeerConnection* pc, int pt);
void peer_connection_refresh_pairs(PeerConnection* pc);
void peer_connection_restart_ice(PeerConnection* pc);

/* Find the H.264 payload type the offer wants: prefer the rtpmap whose fmtp has
 * packetization-mode=1 (what we answer with), else the first H.264, else 96. */
static int parse_h264_pt(const char* sdp) {
  int best = -1, any = -1;
  const char* p = sdp;
  while ((p = strstr(p, "a=rtpmap:")) != NULL) {
    int pt = atoi(p + 9);
    const char* eol = strchr(p, '\n');
    const char* h = strstr(p, "H264/90000");
    if (h && (!eol || h < eol)) {
      if (any < 0) any = pt;
      char needle[24];
      snprintf(needle, sizeof(needle), "a=fmtp:%d ", pt);
      const char* f = strstr(sdp, needle);
      if (f) {
        const char* fe = strchr(f, '\n');
        const char* pm = strstr(f, "packetization-mode=1");
        if (pm && (!fe || pm < fe)) best = pt;
      }
    }
    p += 9;
  }
  return best > 0 ? best : (any > 0 ? any : 96);
}

/* Copy `in` to `out`, replacing every occurrence of `find` with `repl`. */
static void str_replace(const char* in, const char* find, const char* repl,
                        char* out, size_t cap) {
  size_t oi = 0, flen = strlen(find), rlen = strlen(repl);
  const char* p = in;
  while (*p && oi + 1 < cap) {
    if (strncmp(p, find, flen) == 0) {
      if (oi + rlen >= cap) break;
      memcpy(out + oi, repl, rlen);
      oi += rlen;
      p += flen;
    } else {
      out[oi++] = *p++;
    }
  }
  out[oi] = 0;
}

/* libpeer's answer hardcodes mid "video", BUNDLE "video", and sendrecv. Browsers
 * require the answer's mid/BUNDLE to match the offer's mid and the direction to
 * be the reverse of the offer (recvonly -> sendonly), else setRemoteDescription
 * rejects it. Rewrite those to match the offer. */
static void fixup_answer(const char* answer, const char* offer, int pt, char* out, size_t cap) {
  char mid[32] = "0";
  const char* m = strstr(offer, "a=mid:");
  if (m) {
    m += 6;
    int i = 0;
    while (m[i] && m[i] != '\r' && m[i] != '\n' && i < 31) { mid[i] = m[i]; i++; }
    mid[i] = 0;
  }
  char bundle[64], midline[64];
  snprintf(bundle, sizeof(bundle), "a=group:BUNDLE %s", mid);
  snprintf(midline, sizeof(midline), "a=mid:%s", mid);

  static char A[16384], B[16384];
  str_replace(answer, "a=sendrecv", "a=sendonly", A, sizeof(A));
  str_replace(A, "a=group:BUNDLE video", bundle, B, sizeof(B));
  str_replace(B, "a=mid:video", midline, A, sizeof(A));

  /* Rewrite libpeer's hardcoded H.264 PT 96 to the browser's negotiated PT. */
  if (pt != 96) {
    char f[24], r[24];
    snprintf(f, sizeof(f), "SAVPF 96");      snprintf(r, sizeof(r), "SAVPF %d", pt);      str_replace(A, f, r, B, sizeof(B));
    snprintf(f, sizeof(f), "a=rtpmap:96 ");  snprintf(r, sizeof(r), "a=rtpmap:%d ", pt);  str_replace(B, f, r, A, sizeof(A));
    snprintf(f, sizeof(f), "a=fmtp:96 ");    snprintf(r, sizeof(r), "a=fmtp:%d ", pt);    str_replace(A, f, r, B, sizeof(B));
    snprintf(f, sizeof(f), "a=rtcp-fb:96 "); snprintf(r, sizeof(r), "a=rtcp-fb:%d ", pt); str_replace(B, f, r, A, sizeof(A));
  }
  /* Add msid so the browser populates ontrack's e.streams[0] (Mainsail attaches
   * video via e.streams[0]; libpeer's answer omits msid). */
  str_replace(A, "a=ssrc:1 cname:webrtc-h264",
              "a=msid:guppy guppyv\r\na=ssrc:1 cname:webrtc-h264\r\na=ssrc:1 msid:guppy guppyv",
              B, sizeof(B));
  snprintf(out, cap, "%s", B);
}

/* Replace any current session with a fresh PeerConnection negotiated from the
 * browser's offer. Writes the answer SDP into out (cap bytes). Returns 0 ok. */
static int negotiate(const char* offer_raw, const char* peer_ip, char* out, size_t cap) {
  static char offer[16384];
  if (peer_ip && peer_ip[0])
    rewrite_mdns(offer_raw, peer_ip, offer, sizeof(offer));
  else
    snprintf(offer, sizeof(offer), "%s", offer_raw);
  PeerConfiguration cfg = {
      .ice_servers = {{0}},      /* LAN: host candidates only, no STUN/TURN */
      .datachannel = DATA_CHANNEL_NONE,
      .video_codec = CODEC_H264,
      .audio_codec = CODEC_NONE,
  };
  int rc = -1;
  pthread_mutex_lock(&g_pc_lock);
  if (g_pc) {
    peer_connection_destroy(g_pc);
    g_pc = NULL;
    g_state = PEER_CONNECTION_CLOSED;
  }
  g_pc = peer_connection_create(&cfg);
  if (g_pc) {
    peer_connection_oniceconnectionstatechange(g_pc, on_state);
    peer_connection_on_receiver_packet_loss(g_pc, on_loss);
    int pt = parse_h264_pt(offer);
    peer_connection_set_remote_description(g_pc, offer, SDP_TYPE_OFFER);
    const char* answer = peer_connection_create_answer(g_pc);
    peer_connection_set_video_pt(g_pc, pt);  /* stamp RTP with the negotiated PT */
    if (answer && strlen(answer) < cap) {
      fixup_answer(answer, offer, pt, out, cap);
      rc = 0;
    }
  }
  pthread_mutex_unlock(&g_pc_lock);
  return rc;
}

static void* pc_loop_task(void* arg) {
  (void)arg;
  while (g_run) {
    pthread_mutex_lock(&g_pc_lock);
    if (g_pc) peer_connection_loop(g_pc);
    pthread_mutex_unlock(&g_pc_lock);
    usleep(1000);
  }
  return NULL;
}

static void* video_task(void* arg) {
  (void)arg;
  uint8_t* frame;
  int size;
  while (g_run) {
    if (g_state == PEER_CONNECTION_COMPLETED &&
        mr_next_frame(g_reader, &frame, &size)) {
      pthread_mutex_lock(&g_pc_lock);
      if (g_pc && g_state == PEER_CONNECTION_COMPLETED)
        peer_connection_send_video(g_pc, frame, size);
      pthread_mutex_unlock(&g_pc_lock);
    } else {
      usleep(2000);
    }
  }
  return NULL;
}

/* ---- minimal HTTP/1.1 signaling server (one request at a time) ---- */

static void send_all(int fd, const char* p, size_t n) {
  while (n) {
    ssize_t w = write(fd, p, n);
    if (w <= 0) break;
    p += w;
    n -= (size_t)w;
  }
}

static void http_reply(int fd, const char* status, const char* ctype,
                       const char* body, size_t blen) {
  char hdr[512];
  int n = snprintf(hdr, sizeof(hdr),
                   "HTTP/1.1 %s\r\n"
                   "Content-Type: %s\r\n"
                   "Content-Length: %zu\r\n"
                   "Access-Control-Allow-Origin: *\r\n"
                   "Access-Control-Allow-Headers: Content-Type\r\n"
                   "Access-Control-Allow-Methods: POST, GET, OPTIONS, DELETE, PATCH\r\n"
                   "Connection: close\r\n\r\n",
                   status, ctype, blen);
  send_all(fd, hdr, (size_t)n);
  if (body && blen) send_all(fd, body, blen);
}

static void handle_client(int fd) {
  char peer_ip[64] = {0};
  struct sockaddr_in pa;
  socklen_t pl = sizeof(pa);
  if (getpeername(fd, (struct sockaddr*)&pa, &pl) == 0)
    inet_ntop(AF_INET, &pa.sin_addr, peer_ip, sizeof(peer_ip));

  static char req[8192];
  int total = 0, n;
  /* Read headers (and as much body as arrives) up to buffer. */
  while (total < (int)sizeof(req) - 1 &&
         (n = read(fd, req + total, sizeof(req) - 1 - total)) > 0) {
    total += n;
    req[total] = 0;
    char* hend = strstr(req, "\r\n\r\n");
    if (!hend) continue;
    int header_len = (int)(hend - req) + 4;
    int clen = 0;
    char* cl = strcasestr(req, "Content-Length:");
    if (cl) clen = atoi(cl + 15);
    if (total >= header_len + clen) break;  /* full body in */
  }
  if (total <= 0) return;

  if (strncmp(req, "OPTIONS", 7) == 0) {
    http_reply(fd, "204 No Content", "text/plain", "", 0);
    return;
  }
  if (strncmp(req, "DELETE", 6) == 0) {  /* WHEP session teardown */
    http_reply(fd, "200 OK", "text/plain", "", 0);
    return;
  }
  if (strncmp(req, "PATCH", 5) == 0) {  /* WHEP trickle ICE: add remote candidates */
    char* body = strstr(req, "\r\n\r\n");
    if (body) {
      body += 4;
      static char frag[8192];
      rewrite_mdns(body, peer_ip, frag, sizeof(frag));
      pthread_mutex_lock(&g_pc_lock);
      /* Only feed trickle candidates while ICE is still trying. Once connected,
       * leave the agent alone - rebuilding pairs would invalidate the nominated
       * pair and break the in-flight DTLS handshake. */
      int added = 0;
      if (g_pc && g_state != PEER_CONNECTION_CONNECTED && g_state != PEER_CONNECTION_COMPLETED) {
        char* line = frag;
        while (line && *line) {
          char* nl = strpbrk(line, "\r\n");
          int len = nl ? (int)(nl - line) : (int)strlen(line);
          if (len > 12 && len < 500 && strncmp(line, "a=candidate:", 12) == 0) {
            char cand[512];
            memcpy(cand, line, len);
            cand[len] = 0;
            if (peer_connection_add_ice_candidate(g_pc, cand) == 0) added++;
          }
          if (!nl) break;
          line = nl + 1;
        }
        if (added) {
          peer_connection_refresh_pairs(g_pc);
          peer_connection_restart_ice(g_pc);  /* re-arm ICE: offer was candidate-less (trickle) */
        }
      }
      pthread_mutex_unlock(&g_pc_lock);
    }
    http_reply(fd, "204 No Content", "text/plain", "", 0);
    return;
  }
  if (strncmp(req, "GET / ", 6) == 0 || strncmp(req, "GET /index", 10) == 0) {
    http_reply(fd, "200 OK", "text/html", VIEWER_HTML, sizeof(VIEWER_HTML) - 1);
    return;
  }
  if (strncmp(req, "POST ", 5) == 0) {
    char* body = strstr(req, "\r\n\r\n");
    if (!body) {
      http_reply(fd, "400 Bad Request", "text/plain", "", 0);
      return;
    }
    body += 4;
    static char answer[16384];

    /* Two client dialects:
     *  - JSON {type,sdp} -> JSON {type:"answer",sdp}  (our viewer, go2rtc-style)
     *  - raw SDP offer    -> raw SDP answer            (Mainsail webrtc-camerastreamer) */
    if (body[0] == '{') {
      cJSON* in = cJSON_Parse(body);
      cJSON* sdp = in ? cJSON_GetObjectItem(in, "sdp") : NULL;
      if (!sdp || !cJSON_IsString(sdp)) {
        if (in) cJSON_Delete(in);
        http_reply(fd, "400 Bad Request", "text/plain", "", 0);
        return;
      }
      int rc = negotiate(sdp->valuestring, peer_ip, answer, sizeof(answer));
      cJSON_Delete(in);
      if (rc != 0) {
        http_reply(fd, "500 Internal Server Error", "text/plain", "", 0);
        return;
      }
      cJSON* out = cJSON_CreateObject();
      cJSON_AddStringToObject(out, "type", "answer");
      cJSON_AddStringToObject(out, "sdp", answer);
      char* js = cJSON_PrintUnformatted(out);
      http_reply(fd, "200 OK", "application/json", js, strlen(js));
      cJSON_free(js);
      cJSON_Delete(out);
    } else {
      int rc = negotiate(body, peer_ip, answer, sizeof(answer));
      if (rc != 0) {
        http_reply(fd, "500 Internal Server Error", "text/plain", "", 0);
        return;
      }
      /* WHEP (Mainsail webrtc-mediamtx): must be 201 Created + a Location header,
       * with Location exposed for the cross-origin client to read. */
      char hdr[640];
      int n = snprintf(hdr, sizeof(hdr),
                       "HTTP/1.1 201 Created\r\n"
                       "Content-Type: application/sdp\r\n"
                       "Content-Length: %zu\r\n"
                       "Location: /webrtc/whep/guppy\r\n"
                       "Access-Control-Allow-Origin: *\r\n"
                       "Access-Control-Expose-Headers: Location\r\n"
                       "Connection: close\r\n\r\n",
                       strlen(answer));
      send_all(fd, hdr, (size_t)n);
      send_all(fd, answer, strlen(answer));
    }
    return;
  }
  http_reply(fd, "404 Not Found", "text/plain", "", 0);
}

static void on_sigint(int s) {
  (void)s;
  g_run = 0;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <h264_memfd_path> [http_port] [control_socket]\n", argv[0]);
    return 2;
  }
  int port = (argc > 2) ? atoi(argv[2]) : 8585;
  const char* ctrl_path = (argc > 3) ? argv[3] : NULL;
  if (ctrl_path) {
    g_ctrl_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    memset(&g_ctrl_addr, 0, sizeof(g_ctrl_addr));
    g_ctrl_addr.sun_family = AF_UNIX;
    snprintf(g_ctrl_addr.sun_path, sizeof(g_ctrl_addr.sun_path), "%s", ctrl_path);
  }
  setvbuf(stdout, NULL, _IONBF, 0);  /* flush libpeer's INFO logs to the logfile */
  setvbuf(stderr, NULL, _IONBF, 0);
  /* No SA_RESTART: SIGTERM/SIGINT must interrupt accept() so the process
   * actually exits on `stop` (default signal() restarts the syscall). */
  struct sigaction sa = {0};
  sa.sa_handler = on_sigint;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);
  signal(SIGPIPE, SIG_IGN);

  g_reader = mr_open(argv[1]);
  if (!g_reader) {
    fprintf(stderr, "guppy-webrtc: cannot open camera memfd %s\n", argv[1]);
    return 1;
  }

  peer_init();

  pthread_t t_loop, t_video;
  pthread_create(&t_loop, NULL, pc_loop_task, NULL);
  pthread_create(&t_video, NULL, video_task, NULL);

  int srv = socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
  setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);
  if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) < 0 || listen(srv, 4) < 0) {
    fprintf(stderr, "guppy-webrtc: cannot bind port %d\n", port);
    return 1;
  }
  fprintf(stderr, "guppy-webrtc: signaling on :%d, camera %s\n", port, argv[1]);

  while (g_run) {
    int c = accept(srv, NULL, NULL);
    if (c < 0) continue;
    /* Per-connection timeouts: a stalled/half-open client must never block the
     * single-threaded accept loop (that froze the whole server). */
    struct timeval tv = {5, 0};
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    handle_client(c);
    close(c);
  }

  close(srv);
  g_run = 0;
  pthread_join(t_loop, NULL);
  pthread_join(t_video, NULL);
  pthread_mutex_lock(&g_pc_lock);
  if (g_pc) peer_connection_destroy(g_pc);
  pthread_mutex_unlock(&g_pc_lock);
  peer_deinit();
  mr_close(g_reader);
  return 0;
}
