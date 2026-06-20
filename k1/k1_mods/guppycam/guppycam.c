/* guppycam - an open, controllable camera daemon for the Creality KE (Ingenic X2000).
 *
 * A from-scratch, fully-documented replacement for Creality's closed `cam_app`.
 * Drives the same hardware path (USB UVC camera + Ingenic "Helix" V4L2 codec) but
 * every stage is open V4L2 and every encoder parameter is ours to control.
 *
 *   camera (UVC)          source auto-select:
 *     ├─ H.264 ───────────► passthrough            (no decode, no encode)
 *     ├─ MJPEG ──► Helix HW JPEG-decode ─► NV12
 *     └─ YUYV  ──► CPU convert            ─► NV12
 *                                            │
 *                       (zero-copy dmabuf where the driver allows, else memcpy)
 *                                            ▼
 *                    one or more Helix H.264 encoders (simulcast):
 *                       stream0 = full res @ bitrate0
 *                       stream1 = downscaled @ bitrate1 ...
 *                                            │
 *                       live control: adaptive bitrate (AIMD from loss reports),
 *                       force-IDR, snapshot, stats - over a unix control socket.
 *
 * No proprietary SDK (no libimp), no cloud, no AI middleware. One static binary.
 *
 * Portability: the camera side is generic UVC (auto-detects H.264/MJPEG/YUYV, so
 * it works with other USB webcams, not just the Nebula). The encode side is
 * Ingenic-X2000-Helix-specific but uses the standard V4L2 M2M API.
 *
 * Verified on-device (Ender-3 V3 KE, kernel 4.4.94):
 *   - video4 "CCX2F3298" uvcvideo: MJPG + YUYV, up to 1920x1080@30
 *   - video1 "helix-venc" V4L2 M2M MPLANE: OUTPUT NV12/NV21/YU12/JPEG/H264,
 *     CAPTURE H264/JPEG/NV12; controls gop_size, bitrate(<=4M), bitrate_mode,
 *     frame_rc_enable, header_mode, h264 i/p QP, i_frame_period, level, profile,
 *     force_key_frame, jpeg compression_quality
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <linux/videodev2.h>

/* ----- V4L2 control IDs (define explicitly so we don't depend on header age) ----- */
#ifndef V4L2_CID_MPEG_VIDEO_BITRATE
#define V4L2_CID_MPEG_VIDEO_BITRATE         (V4L2_CID_MPEG_BASE + 207)
#endif
#ifndef V4L2_CID_MPEG_VIDEO_BITRATE_MODE
#define V4L2_CID_MPEG_VIDEO_BITRATE_MODE    (V4L2_CID_MPEG_BASE + 206)
#endif
#ifndef V4L2_CID_MPEG_VIDEO_GOP_SIZE
#define V4L2_CID_MPEG_VIDEO_GOP_SIZE        (V4L2_CID_MPEG_BASE + 202)
#endif
#ifndef V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME
#define V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME (V4L2_CID_MPEG_BASE + 211)
#endif
#ifndef V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE
#define V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE (V4L2_CID_MPEG_BASE + 200)
#endif
#ifndef V4L2_CID_MPEG_VIDEO_HEADER_MODE
#define V4L2_CID_MPEG_VIDEO_HEADER_MODE     (V4L2_CID_MPEG_BASE + 210)
#endif
#ifndef V4L2_CID_MPEG_VIDEO_H264_PROFILE
#define V4L2_CID_MPEG_VIDEO_H264_PROFILE    (V4L2_CID_MPEG_BASE + 363)
#endif
#ifndef V4L2_PIX_FMT_H264
#define V4L2_PIX_FMT_H264 v4l2_fourcc('H', '2', '6', '4')
#endif
/* Helix H264 profile menu order (v4l2-ctl -L): 0 Baseline,1 CBaseline,2 Main,3 Ext,4 High */
#define GC_PROFILE_BASELINE 0
#define GC_PROFILE_MAIN     2
#define GC_PROFILE_HIGH     4
#define GC_RC_VBR 0
#define GC_RC_CBR 1
#define GC_RC_CQ  2
/* requestbuffers.capabilities flag (kernel >= 4.16); define for older headers */
#ifndef V4L2_BUF_CAP_SUPPORTS_DMABUF
#define V4L2_BUF_CAP_SUPPORTS_DMABUF (1 << 2)
#endif

#define MAX_BUFS 6
#define MAX_STREAMS 3

/* ----------------------------------------------------------------------------- */
/* memfd output sink - mirrors Creality cam_app's "main_memfd" layout so the      */
/* existing guppy-webrtc memfd_reader consumes our H.264 unchanged:               */
/*   - u32 current-frame offset at byte 16                                        */
/*   - each frame stored as [u32 size][Annex-B data] at that offset              */
/* Two alternating regions (double-buffer) so the reader never sees a torn frame. */
/* ----------------------------------------------------------------------------- */
#define SINK_SZ (8u << 20)
#define SINK_HDR 4096
typedef struct {
  int fd;
  uint8_t* base;
  size_t sz;
  size_t regoff[2];
  int cur;
} MemfdSink;

/* call the syscall directly - avoids depending on the libc wrapper's presence */
static int gc_memfd_create(const char* name, unsigned flags) {
  return (int)syscall(SYS_memfd_create, name, flags);
}

static int memfd_sink_open(MemfdSink* s, const char* name) {
  s->fd = gc_memfd_create(name, 0);
  if (s->fd < 0) return -1;
  s->sz = SINK_SZ;
  if (ftruncate(s->fd, s->sz) < 0) return -1;
  s->base = mmap(NULL, s->sz, PROT_READ | PROT_WRITE, MAP_SHARED, s->fd, 0);
  if (s->base == MAP_FAILED) return -1;
  memset(s->base, 0, SINK_HDR);
  s->regoff[0] = SINK_HDR;
  s->regoff[1] = SINK_HDR + (s->sz - SINK_HDR) / 2;
  s->cur = 1; /* first frame lands in region 0 */
  return 0;
}

static void memfd_sink_write(MemfdSink* s, const void* frame, size_t len) {
  int r = s->cur ^ 1;
  size_t off = s->regoff[r];
  if (off + 4 + len > s->sz) return; /* frame too big - skip */
  *(volatile uint32_t*)(s->base + off) = (uint32_t)len;
  memcpy(s->base + off + 4, frame, len);
  *(volatile uint32_t*)(s->base + 16) = (uint32_t)off; /* publish atomically-ish */
  s->cur = r;
}
#define BITRATE_MAX 4000000
#define BITRATE_MIN 150000

/* ----------------------------------------------------------------------------- */
/* globals + helpers                                                             */
/* ----------------------------------------------------------------------------- */
static volatile sig_atomic_t g_run = 1;
static volatile sig_atomic_t g_force_idr = 0;

static void logmsg(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
  fflush(stderr);
}

static int xioctl(int fd, unsigned long req, void* arg) {
  int r;
  do {
    r = ioctl(fd, req, arg);
  } while (r == -1 && errno == EINTR);
  return r;
}

static int set_ctrl(int fd, uint32_t id, int32_t val) {
  struct v4l2_control c = {.id = id, .value = val};
  if (xioctl(fd, VIDIOC_S_CTRL, &c) == -1) {
    logmsg("  ctrl 0x%08x = %d FAILED (%s)", id, val, strerror(errno));
    return -1;
  }
  return 0;
}

static uint64_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ----------------------------------------------------------------------------- */
/* NV12 helpers: YUYV->NV12 convert and NV12 box-downscale (for simulcast)        */
/* ----------------------------------------------------------------------------- */
/* YUYV (Y0 U Y1 V, 4:2:2 packed) -> NV12 (Y plane + interleaved UV, 4:2:0).
 * Vertically subsamples chroma by taking even rows. dst must be w*h*3/2. */
static void yuyv_to_nv12(const uint8_t* src, uint8_t* dst, int w, int h) {
  uint8_t* y = dst;
  uint8_t* uv = dst + w * h;
  for (int j = 0; j < h; j++) {
    const uint8_t* s = src + j * w * 2;
    uint8_t* yr = y + j * w;
    for (int i = 0; i < w; i += 2) {
      yr[i] = s[0];
      yr[i + 1] = s[2];
      if ((j & 1) == 0) { /* take chroma from even rows only -> 4:2:0 */
        uint8_t* uvr = uv + (j / 2) * w;
        uvr[i] = s[1];     /* U */
        uvr[i + 1] = s[3]; /* V */
      }
      s += 4;
    }
  }
}

/* Simple box/nearest NV12 downscale (dw<=sw, dh<=sh). For the low-bitrate
 * simulcast stream. Nearest-neighbour on Y and UV; cheap and adequate. */
static void nv12_downscale(const uint8_t* src, int sw, int sh, uint8_t* dst, int dw, int dh) {
  const uint8_t* sy = src;
  const uint8_t* suv = src + sw * sh;
  uint8_t* dy = dst;
  uint8_t* duv = dst + dw * dh;
  for (int j = 0; j < dh; j++) {
    int sj = j * sh / dh;
    for (int i = 0; i < dw; i++) dy[j * dw + i] = sy[sj * sw + (i * sw / dw)];
  }
  for (int j = 0; j < dh / 2; j++) {
    int sj = j * (sh / 2) / (dh / 2);
    for (int i = 0; i < dw; i += 2) {
      int si = (i / 2) * (sw / 2) / (dw / 2) * 2;
      duv[j * dw + i] = suv[sj * sw + si];
      duv[j * dw + i + 1] = suv[sj * sw + si + 1];
    }
  }
}

/* ----------------------------------------------------------------------------- */
/* MJPEG Huffman-table fix                                                        */
/* Many UVC cameras emit abbreviated MJPEG without Huffman tables (DHT). Software */
/* JPEG decoders insert the standard tables implicitly; the Helix HW JPEG decoder */
/* does NOT, so it produces garbage (green) frames. Splice the standard JPEG      */
/* Huffman tables in just before the SOS (FFDA) marker when none are present.     */
/* The table blob below is the canonical JPEG/IJG standard set (DHT, len 0x01A2). */
/* ----------------------------------------------------------------------------- */
static const uint8_t STD_DHT[] = {
    0xFF, 0xC4, 0x01, 0xA2,
    0x00, 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, /* DC luma */
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
    0x10, 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d, /* AC luma */
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
    0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
    0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
    0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
    0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa,
    0x01, 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, /* DC chroma */
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
    0x11, 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77, /* AC chroma */
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
    0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
    0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
    0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
    0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
    0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
    0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
    0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa};

/* Returns length written to out (0 on overflow). If the frame already has a DHT,
 * it's copied unchanged; otherwise the standard DHT is inserted before SOS. */
static size_t mjpeg_ensure_huffman(const uint8_t* in, size_t len, uint8_t* out, size_t cap) {
  size_t sos = 0;
  for (size_t i = 0; i + 1 < len; i++) {
    if (in[i] == 0xFF && in[i + 1] == 0xC4) { /* DHT already present */
      if (len > cap) return 0;
      memcpy(out, in, len);
      return len;
    }
    if (in[i] == 0xFF && in[i + 1] == 0xDA) { sos = i; break; } /* SOS - stop scan */
  }
  if (sos == 0) { /* no SOS found - pass through */
    if (len > cap) return 0;
    memcpy(out, in, len);
    return len;
  }
  size_t total = sos + sizeof(STD_DHT) + (len - sos);
  if (total > cap) return 0;
  memcpy(out, in, sos);
  memcpy(out + sos, STD_DHT, sizeof(STD_DHT));
  memcpy(out + sos + sizeof(STD_DHT), in + sos, len - sos);
  return total;
}

/* ----------------------------------------------------------------------------- */
/* MJPEG HTTP server (replaces mjpg_streamer on :8080)                            */
/* Serves /?action=stream (multipart/x-mixed-replace) and /?action=snapshot from  */
/* the latest captured MJPEG frame. Only active in MJPEG capture mode.            */
/* ----------------------------------------------------------------------------- */
/* One MJPEG output (its own latest-frame buffer + listen port). We run two:
 * mid (e.g. 720p) and low (e.g. 360p). */
typedef struct {
  pthread_mutex_t lock;
  pthread_cond_t cond;
  uint8_t* buf;
  size_t len, cap;
  unsigned seq;
  int port;
  volatile int clients; /* connected viewers - lets the pipeline encode lazily */
} MjpegOut;

/* mid = compatibility stream (native MJPEG passthrough @ master res);
 * low = remote stream (downscaled + HW JPEG re-encode). */
static MjpegOut g_mout_mid = {.lock = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER};
static MjpegOut g_mout_low = {.lock = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER};

static void mjpeg_publish(MjpegOut* m, const void* f, size_t len) {
  pthread_mutex_lock(&m->lock);
  if (len > m->cap) {
    free(m->buf);
    m->buf = malloc(len);
    m->cap = m->buf ? len : 0;
  }
  if (m->buf) { memcpy(m->buf, f, len); m->len = len; m->seq++; }
  pthread_cond_broadcast(&m->cond);
  pthread_mutex_unlock(&m->lock);
}

static int write_all(int fd, const void* p, size_t n) {
  const char* c = p;
  while (n) {
    ssize_t w = write(fd, c, n);
    if (w <= 0) return -1;
    c += w;
    n -= (size_t)w;
  }
  return 0;
}

/* Bound every client socket so a dead/slow viewer can't block a server thread
 * forever (the thread- and buffer-leak that can starve a 197MB box). */
static void set_sock_timeout(int fd, int secs) {
  struct timeval tv = {secs, 0};
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

#define MAX_CLIENTS_PER_STREAM 8
static volatile int g_ws_clients = 0;

typedef struct {
  int fd;
  MjpegOut* m;
} MjpegClient;

static void* mjpeg_client(void* arg) {
  MjpegClient* cc = arg;
  int fd = cc->fd;
  MjpegOut* m = cc->m;
  free(cc);
  set_sock_timeout(fd, 5);
  char req[1024];
  int n = read(fd, req, sizeof(req) - 1);
  if (n <= 0) { close(fd); return NULL; }
  req[n] = 0;
  int snapshot = strstr(req, "action=snapshot") != NULL;
  if (__sync_add_and_fetch(&m->clients, 1) > MAX_CLIENTS_PER_STREAM) {
    __sync_sub_and_fetch(&m->clients, 1);
    close(fd);
    return NULL;
  }
  if (snapshot) {
    pthread_mutex_lock(&m->lock);
    size_t len = m->len;
    uint8_t* tmp = len ? malloc(len) : NULL;
    if (tmp) memcpy(tmp, m->buf, len);
    pthread_mutex_unlock(&m->lock);
    if (tmp) {
      char h[256];
      int hn = snprintf(h, sizeof(h),
                        "HTTP/1.0 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n"
                        "Access-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n", len);
      if (write_all(fd, h, hn) == 0) write_all(fd, tmp, len);
      free(tmp);
    } else {
      const char* e = "HTTP/1.0 503 Service Unavailable\r\nConnection: close\r\n\r\n";
      write_all(fd, e, strlen(e));
    }
  } else { /* multipart stream */
    const char* h = "HTTP/1.0 200 OK\r\nConnection: close\r\nAccess-Control-Allow-Origin: *\r\n"
                    "Content-Type: multipart/x-mixed-replace;boundary=guppycam\r\n\r\n";
    if (write_all(fd, h, strlen(h)) == 0) {
      unsigned last = 0;
      while (g_run) {
        pthread_mutex_lock(&m->lock);
        while (m->seq == last && g_run) pthread_cond_wait(&m->cond, &m->lock);
        size_t len = m->len;
        uint8_t* tmp = len ? malloc(len) : NULL;
        if (tmp) memcpy(tmp, m->buf, len);
        last = m->seq;
        pthread_mutex_unlock(&m->lock);
        if (!tmp) break;
        char part[128];
        int pn = snprintf(part, sizeof(part),
                          "--guppycam\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n", len);
        int bad = write_all(fd, part, pn) || write_all(fd, tmp, len) || write_all(fd, "\r\n", 2);
        free(tmp);
        if (bad) break;
      }
    }
  }
  __sync_sub_and_fetch(&m->clients, 1);
  close(fd);
  return NULL;
}

static void* mjpeg_server(void* arg) {
  MjpegOut* m = arg;
  int s = socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  struct sockaddr_in a = {0};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = INADDR_ANY;
  a.sin_port = htons(m->port);
  if (bind(s, (struct sockaddr*)&a, sizeof(a)) < 0 || listen(s, 4) < 0) {
    logmsg("mjpeg: bind :%d failed: %s", m->port, strerror(errno));
    close(s);
    return NULL;
  }
  logmsg("mjpeg: serving on :%d (/?action=stream|snapshot)", m->port);
  while (g_run) {
    int c = accept(s, NULL, NULL);
    if (c < 0) continue;
    MjpegClient* cc = malloc(sizeof(*cc));
    if (!cc) { close(c); continue; }
    cc->fd = c;
    cc->m = m;
    pthread_t t;
    if (pthread_create(&t, NULL, mjpeg_client, cc) == 0) pthread_detach(t);
    else { free(cc); close(c); }
  }
  close(s);
  return NULL;
}

/* ----------------------------------------------------------------------------- */
/* WebSocket H.264 server (Mainsail "Raw H264 (jmuxer)" stream)                   */
/* jmuxer.js in the browser decodes raw H.264 NAL units delivered as binary       */
/* WebSocket frames via MediaSource. We do the WS handshake (SHA-1 + base64) and  */
/* send each encoded access unit as one binary frame. Replaces guppy-webrtc.      */
/* ----------------------------------------------------------------------------- */
typedef struct {
  uint32_t h[5];
  uint64_t len;
  uint8_t buf[64];
  int n;
} SHA1;
static void sha1_init(SHA1* s) {
  s->h[0] = 0x67452301;
  s->h[1] = 0xEFCDAB89;
  s->h[2] = 0x98BADCFE;
  s->h[3] = 0x10325476;
  s->h[4] = 0xC3D2E1F0;
  s->len = 0;
  s->n = 0;
}
static void sha1_block(SHA1* s, const uint8_t* p) {
  uint32_t w[80], a, b, c, d, e, f, k, t;
  for (int i = 0; i < 16; i++) w[i] = (p[i * 4] << 24) | (p[i * 4 + 1] << 16) | (p[i * 4 + 2] << 8) | p[i * 4 + 3];
  for (int i = 16; i < 80; i++) { uint32_t v = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16]; w[i] = (v << 1) | (v >> 31); }
  a = s->h[0]; b = s->h[1]; c = s->h[2]; d = s->h[3]; e = s->h[4];
  for (int i = 0; i < 80; i++) {
    if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999; }
    else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
    else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
    else { f = b ^ c ^ d; k = 0xCA62C1D6; }
    t = ((a << 5) | (a >> 27)) + f + e + k + w[i];
    e = d; d = c; c = (b << 30) | (b >> 2); b = a; a = t;
  }
  s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e;
}
static void sha1_update(SHA1* s, const uint8_t* p, size_t n) {
  s->len += n;
  while (n) {
    int t = 64 - s->n;
    if (t > (int)n) t = (int)n;
    memcpy(s->buf + s->n, p, t);
    s->n += t; p += t; n -= t;
    if (s->n == 64) { sha1_block(s, s->buf); s->n = 0; }
  }
}
static void sha1_final(SHA1* s, uint8_t out[20]) {
  uint64_t bits = s->len * 8;
  uint8_t pad = 0x80, z = 0;
  sha1_update(s, &pad, 1);
  while (s->n != 56) sha1_update(s, &z, 1);
  uint8_t lb[8];
  for (int i = 0; i < 8; i++) lb[i] = (uint8_t)(bits >> (56 - i * 8));
  sha1_update(s, lb, 8);
  for (int i = 0; i < 5; i++) {
    out[i * 4] = s->h[i] >> 24; out[i * 4 + 1] = s->h[i] >> 16;
    out[i * 4 + 2] = s->h[i] >> 8; out[i * 4 + 3] = s->h[i];
  }
}
static void b64(const uint8_t* in, int n, char* out) {
  static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  int o = 0;
  for (int i = 0; i < n; i += 3) {
    int v = in[i] << 16 | (i + 1 < n ? in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
    out[o++] = t[(v >> 18) & 63];
    out[o++] = t[(v >> 12) & 63];
    out[o++] = (i + 1 < n) ? t[(v >> 6) & 63] : '=';
    out[o++] = (i + 2 < n) ? t[v & 63] : '=';
  }
  out[o] = 0;
}

static pthread_mutex_t g_h264_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_h264_cond = PTHREAD_COND_INITIALIZER;
static uint8_t* g_h264_buf = NULL;
static size_t g_h264_len = 0, g_h264_cap = 0;
static unsigned g_h264_seq = 0;

static void h264_publish(const void* f, size_t len) {
  pthread_mutex_lock(&g_h264_lock);
  if (len > g_h264_cap) {
    free(g_h264_buf);
    g_h264_buf = malloc(len);
    g_h264_cap = g_h264_buf ? len : 0;
  }
  if (g_h264_buf) { memcpy(g_h264_buf, f, len); g_h264_len = len; g_h264_seq++; }
  pthread_cond_broadcast(&g_h264_cond);
  pthread_mutex_unlock(&g_h264_lock);
}

static int ws_send(int fd, const uint8_t* p, size_t len) {
  uint8_t hdr[10];
  int hn;
  hdr[0] = 0x82; /* FIN + binary opcode */
  if (len < 126) { hdr[1] = (uint8_t)len; hn = 2; }
  else if (len < 65536) { hdr[1] = 126; hdr[2] = len >> 8; hdr[3] = len & 0xff; hn = 4; }
  else { hdr[1] = 127; for (int i = 0; i < 8; i++) hdr[2 + i] = (uint8_t)((uint64_t)len >> (56 - i * 8)); hn = 10; }
  if (write_all(fd, hdr, hn)) return -1;
  return write_all(fd, p, len);
}

static void* ws_client(void* arg) {
  int fd = (int)(intptr_t)arg;
  set_sock_timeout(fd, 5);
  char req[2048];
  int n = read(fd, req, sizeof(req) - 1);
  if (n <= 0) { close(fd); return NULL; }
  req[n] = 0;
  char* k = strcasestr(req, "Sec-WebSocket-Key:");
  if (!k) { close(fd); return NULL; }
  k += 18;
  while (*k == ' ') k++;
  char key[128];
  int i = 0;
  while (*k && *k != '\r' && *k != '\n' && i < 100) key[i++] = *k++;
  key[i] = 0;
  char cat[200];
  snprintf(cat, sizeof(cat), "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", key);
  SHA1 s;
  sha1_init(&s);
  sha1_update(&s, (const uint8_t*)cat, strlen(cat));
  uint8_t dig[20];
  sha1_final(&s, dig);
  char acc[40];
  b64(dig, 20, acc);
  char resp[256];
  int rn = snprintf(resp, sizeof(resp),
                    "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                    "Sec-WebSocket-Accept: %s\r\n\r\n", acc);
  if (write_all(fd, resp, rn)) { close(fd); return NULL; }
  if (__sync_add_and_fetch(&g_ws_clients, 1) > MAX_CLIENTS_PER_STREAM) {
    __sync_sub_and_fetch(&g_ws_clients, 1);
    close(fd);
    return NULL;
  }
  unsigned last = 0;
  while (g_run) {
    pthread_mutex_lock(&g_h264_lock);
    while (g_h264_seq == last && g_run) pthread_cond_wait(&g_h264_cond, &g_h264_lock);
    size_t len = g_h264_len;
    uint8_t* tmp = len ? malloc(len) : NULL;
    if (tmp) memcpy(tmp, g_h264_buf, len);
    last = g_h264_seq;
    pthread_mutex_unlock(&g_h264_lock);
    if (!tmp) break;
    int bad = ws_send(fd, tmp, len);
    free(tmp);
    if (bad) break;
  }
  __sync_sub_and_fetch(&g_ws_clients, 1);
  close(fd);
  return NULL;
}

static void* ws_server(void* arg) {
  int port = (int)(intptr_t)arg;
  int s = socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  struct sockaddr_in a = {0};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = INADDR_ANY;
  a.sin_port = htons(port);
  if (bind(s, (struct sockaddr*)&a, sizeof(a)) < 0 || listen(s, 4) < 0) {
    logmsg("ws: bind :%d failed: %s", port, strerror(errno));
    close(s);
    return NULL;
  }
  logmsg("ws: H.264 WebSocket serving on :%d (jmuxer)", port);
  while (g_run) {
    int c = accept(s, NULL, NULL);
    if (c < 0) continue;
    pthread_t t;
    if (pthread_create(&t, NULL, ws_client, (void*)(intptr_t)c) == 0) pthread_detach(t);
    else close(c);
  }
  close(s);
  return NULL;
}

/* ----------------------------------------------------------------------------- */
/* UVC capture (single-planar MMAP) with format auto-select                      */
/* ----------------------------------------------------------------------------- */
typedef enum { SRC_H264, SRC_MJPEG, SRC_YUYV } SrcKind;

typedef struct {
  int fd;
  struct {
    void* start;
    size_t length;
  } buf[MAX_BUFS];
  int nbufs;
  int w, h;
  SrcKind kind;
  uint32_t fourcc;
  char devpath[64];
  int reqfps;
} Capture;

/* Does the camera advertise this fourcc? (read-only ENUM_FMT) */
static int cap_has_format(int fd, uint32_t fourcc) {
  for (int i = 0;; i++) {
    struct v4l2_fmtdesc d = {0};
    d.index = i;
    d.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_ENUM_FMT, &d) == -1) break;
    if (d.pixelformat == fourcc) return 1;
  }
  return 0;
}

/* Open the camera and pick the best source format.
 * pref: SRC_H264>SRC_MJPEG>SRC_YUYV unless `force` pins one. */
static int cap_open(Capture* c, const char* dev, int w, int h, int fps, int force /*-1=auto*/) {
  memset(c, 0, sizeof(*c));
  c->fd = -1;
  snprintf(c->devpath, sizeof(c->devpath), "%s", dev);
  c->w = w;
  c->h = h;
  c->reqfps = fps;
  int fd = open(dev, O_RDWR | O_NONBLOCK);
  if (fd < 0) {
    logmsg("capture: open %s failed: %s", dev, strerror(errno));
    return -1;
  }

  /* choose format */
  struct {
    SrcKind k;
    uint32_t fcc;
  } order[3] = {{SRC_H264, V4L2_PIX_FMT_H264}, {SRC_MJPEG, V4L2_PIX_FMT_MJPEG}, {SRC_YUYV, V4L2_PIX_FMT_YUYV}};
  int chosen = -1;
  if (force >= 0) {
    for (int i = 0; i < 3; i++)
      if ((int)order[i].k == force && cap_has_format(fd, order[i].fcc)) chosen = i;
    if (chosen < 0) logmsg("capture: forced format not offered, falling back to auto");
  }
  if (chosen < 0)
    for (int i = 0; i < 3; i++)
      if (cap_has_format(fd, order[i].fcc)) { chosen = i; break; }
  if (chosen < 0) {
    logmsg("capture: camera offers none of H264/MJPEG/YUYV");
    close(fd);
    return -1;
  }
  c->kind = order[chosen].k;
  c->fourcc = order[chosen].fcc;

  struct v4l2_format fmt = {0};
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = w;
  fmt.fmt.pix.height = h;
  fmt.fmt.pix.pixelformat = c->fourcc;
  fmt.fmt.pix.field = V4L2_FIELD_NONE;
  if (xioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
    logmsg("capture: S_FMT failed: %s", strerror(errno));
    close(fd);
    return -1;
  }
  c->w = fmt.fmt.pix.width;
  c->h = fmt.fmt.pix.height;
  c->fourcc = fmt.fmt.pix.pixelformat;
  logmsg("capture: %s -> %dx%d %.4s (%s)", dev, c->w, c->h, (char*)&c->fourcc,
         c->kind == SRC_H264 ? "passthrough" : c->kind == SRC_MJPEG ? "HW-decode" : "YUYV->NV12");

  struct v4l2_streamparm parm = {0};
  parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  parm.parm.capture.timeperframe.numerator = 1;
  parm.parm.capture.timeperframe.denominator = fps;
  xioctl(fd, VIDIOC_S_PARM, &parm);

  struct v4l2_requestbuffers req = {0};
  req.count = 4;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;
  if (xioctl(fd, VIDIOC_REQBUFS, &req) == -1) {
    logmsg("capture: REQBUFS failed: %s", strerror(errno));
    close(fd);
    return -1;
  }
  c->nbufs = req.count;
  for (int i = 0; i < c->nbufs; i++) {
    struct v4l2_buffer b = {0};
    b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    b.memory = V4L2_MEMORY_MMAP;
    b.index = i;
    if (xioctl(fd, VIDIOC_QUERYBUF, &b) == -1) { close(fd); return -1; }
    c->buf[i].length = b.length;
    c->buf[i].start = mmap(NULL, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, b.m.offset);
    if (c->buf[i].start == MAP_FAILED) { close(fd); return -1; }
    if (xioctl(fd, VIDIOC_QBUF, &b) == -1) { close(fd); return -1; }
  }
  int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (xioctl(fd, VIDIOC_STREAMON, &type) == -1) {
    logmsg("capture: STREAMON failed: %s", strerror(errno));
    close(fd);
    return -1;
  }
  c->fd = fd;
  return 0;
}

static int cap_get(Capture* c, void** data, size_t* len, int timeout_ms) {
  struct pollfd pfd = {.fd = c->fd, .events = POLLIN};
  int pr = poll(&pfd, 1, timeout_ms);
  if (pr <= 0) return -1;
  struct v4l2_buffer b = {0};
  b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  b.memory = V4L2_MEMORY_MMAP;
  if (xioctl(c->fd, VIDIOC_DQBUF, &b) == -1) return -1;
  *data = c->buf[b.index].start;
  *len = b.bytesused;
  return b.index;
}

static void cap_release(Capture* c, int idx) {
  struct v4l2_buffer b = {0};
  b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  b.memory = V4L2_MEMORY_MMAP;
  b.index = idx;
  xioctl(c->fd, VIDIOC_QBUF, &b);
}

static void cap_close(Capture* c) {
  if (c->fd < 0) return;
  int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  xioctl(c->fd, VIDIOC_STREAMOFF, &type);
  for (int i = 0; i < c->nbufs; i++)
    if (c->buf[i].start && c->buf[i].start != MAP_FAILED) munmap(c->buf[i].start, c->buf[i].length);
  close(c->fd);
  c->fd = -1;
}

/* ----------------------------------------------------------------------------- */
/* Helix V4L2 M2M context (multiplanar) - JPEG-decode or H.264-encode             */
/* OUTPUT queue = source side, CAPTURE queue = coded/dest side.                   */
/* The OUTPUT (input) queue can use MMAP (we memcpy in) or DMABUF (zero-copy).    */
/* ----------------------------------------------------------------------------- */
typedef struct {
  int fd;
  struct {
    void* start[VIDEO_MAX_PLANES];
    size_t length[VIDEO_MAX_PLANES];
    int nplanes;
    int dmafd; /* exported dmabuf fd for this CAPTURE buffer (decoder), else -1 */
  } out[MAX_BUFS], cap[MAX_BUFS];
  int n_out, n_cap;
  int out_planes, cap_planes;
  int out_memory; /* V4L2_MEMORY_MMAP or V4L2_MEMORY_DMABUF */
  int w, h;
  uint8_t* gather; /* contiguous NV12 scratch when CAPTURE is multi-plane */
} M2M;

static int m2m_open(M2M* m, const char* dev, int w, int h, uint32_t src_fourcc, uint32_t dst_fourcc) {
  memset(m, 0, sizeof(*m));
  m->w = w;
  m->h = h;
  m->out_memory = V4L2_MEMORY_MMAP;
  for (int i = 0; i < MAX_BUFS; i++) m->out[i].dmafd = m->cap[i].dmafd = -1;
  m->fd = open(dev, O_RDWR);
  if (m->fd < 0) {
    logmsg("m2m: open %s failed: %s", dev, strerror(errno));
    return -1;
  }
  uint32_t comp_sz = (uint32_t)w * h; /* generous for compressed planes */

  struct v4l2_format cfmt = {0};
  cfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  cfmt.fmt.pix_mp.width = w;
  cfmt.fmt.pix_mp.height = h;
  cfmt.fmt.pix_mp.pixelformat = dst_fourcc;
  cfmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
  cfmt.fmt.pix_mp.num_planes = 1;
  if (dst_fourcc == V4L2_PIX_FMT_H264 || dst_fourcc == V4L2_PIX_FMT_JPEG)
    cfmt.fmt.pix_mp.plane_fmt[0].sizeimage = comp_sz;
  if (xioctl(m->fd, VIDIOC_S_FMT, &cfmt) == -1) {
    logmsg("m2m: S_FMT(CAPTURE %.4s) failed: %s", (char*)&dst_fourcc, strerror(errno));
    return -1;
  }
  m->cap_planes = cfmt.fmt.pix_mp.num_planes;

  struct v4l2_format ofmt = {0};
  ofmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  ofmt.fmt.pix_mp.width = w;
  ofmt.fmt.pix_mp.height = h;
  ofmt.fmt.pix_mp.pixelformat = src_fourcc;
  ofmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
  ofmt.fmt.pix_mp.num_planes = 1;
  if (src_fourcc == V4L2_PIX_FMT_H264 || src_fourcc == V4L2_PIX_FMT_JPEG)
    ofmt.fmt.pix_mp.plane_fmt[0].sizeimage = comp_sz;
  if (xioctl(m->fd, VIDIOC_S_FMT, &ofmt) == -1) {
    logmsg("m2m: S_FMT(OUTPUT %.4s) failed: %s", (char*)&src_fourcc, strerror(errno));
    return -1;
  }
  m->out_planes = ofmt.fmt.pix_mp.num_planes;
  if (m->cap_planes >= 2) { /* multi-plane raw output (decoder NV12): need a gather buffer */
    m->gather = malloc((size_t)w * h * 3 / 2 + 64);
    if (!m->gather) return -1;
  }
  logmsg("m2m %s: OUT %.4s %ux%u/%dpl bpl=%u sz=%u | CAP %.4s %ux%u/%dpl bpl=%u sz=%u", dev,
         (char*)&ofmt.fmt.pix_mp.pixelformat, ofmt.fmt.pix_mp.width, ofmt.fmt.pix_mp.height,
         m->out_planes, ofmt.fmt.pix_mp.plane_fmt[0].bytesperline,
         ofmt.fmt.pix_mp.plane_fmt[0].sizeimage, (char*)&cfmt.fmt.pix_mp.pixelformat,
         cfmt.fmt.pix_mp.width, cfmt.fmt.pix_mp.height, m->cap_planes,
         cfmt.fmt.pix_mp.plane_fmt[0].bytesperline, cfmt.fmt.pix_mp.plane_fmt[0].sizeimage);
  return 0;
}

/* Request + mmap buffers for one queue. type = OUTPUT_MPLANE or CAPTURE_MPLANE. */
static int m2m_reqbuf_mmap(M2M* m, int type, int count) {
  struct v4l2_requestbuffers req = {0};
  req.count = count;
  req.type = type;
  req.memory = V4L2_MEMORY_MMAP;
  if (xioctl(m->fd, VIDIOC_REQBUFS, &req) == -1) {
    logmsg("m2m: REQBUFS(type %d) failed: %s", type, strerror(errno));
    return -1;
  }
  int* nb = (type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) ? &m->n_out : &m->n_cap;
  int* np = (type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) ? &m->out_planes : &m->cap_planes;
  *nb = req.count;
  for (int i = 0; i < (int)req.count; i++) {
    struct v4l2_plane planes[VIDEO_MAX_PLANES] = {0};
    struct v4l2_buffer b = {0};
    b.type = type;
    b.memory = V4L2_MEMORY_MMAP;
    b.index = i;
    b.length = VIDEO_MAX_PLANES;
    b.m.planes = planes;
    if (xioctl(m->fd, VIDIOC_QUERYBUF, &b) == -1) return -1;
    *np = b.length;
    for (int p = 0; p < (int)b.length; p++) {
      void* addr = mmap(NULL, planes[p].length, PROT_READ | PROT_WRITE, MAP_SHARED, m->fd,
                        planes[p].m.mem_offset);
      if (addr == MAP_FAILED) { logmsg("m2m: mmap failed: %s", strerror(errno)); return -1; }
      if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
        m->out[i].start[p] = addr;
        m->out[i].length[p] = planes[p].length;
        m->out[i].nplanes = b.length;
      } else {
        m->cap[i].start[p] = addr;
        m->cap[i].length[p] = planes[p].length;
        m->cap[i].nplanes = b.length;
      }
    }
  }
  return 0;
}

/* Export this M2M's CAPTURE buffers as dmabuf fds (decoder side, for zero-copy). */
static int m2m_export_capture(M2M* m) {
  for (int i = 0; i < m->n_cap; i++) {
    struct v4l2_exportbuffer e = {0};
    e.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    e.index = i;
    e.plane = 0;
    e.flags = O_RDONLY;
    if (xioctl(m->fd, VIDIOC_EXPBUF, &e) == -1) {
      logmsg("m2m: EXPBUF failed: %s (zero-copy off)", strerror(errno));
      return -1;
    }
    m->cap[i].dmafd = e.fd;
  }
  return 0;
}

/* Try to set the OUTPUT (input) queue to import dmabuf. Returns 0 if the driver
 * accepts DMABUF on OUTPUT, -1 otherwise (caller then uses MMAP path). */
static int m2m_reqbuf_dmabuf_out(M2M* m, int count) {
  struct v4l2_requestbuffers req = {0};
  req.count = count;
  req.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  req.memory = V4L2_MEMORY_DMABUF;
  if (xioctl(m->fd, VIDIOC_REQBUFS, &req) == -1) return -1;
  m->n_out = req.count;
  m->out_memory = V4L2_MEMORY_DMABUF;
  return 0;
}

/* Finish bring-up after the OUTPUT queue's buffers are already requested (MMAP or
 * DMABUF): allocate+queue the CAPTURE buffers and STREAMON both queues. */
static int m2m_start_after_reqbuf(M2M* m) {
  /* CAPTURE buffers always MMAP (we read coded data out of them). */
  if (m2m_reqbuf_mmap(m, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, 2) < 0) return -1;
  for (int i = 0; i < m->n_cap; i++) {
    struct v4l2_plane planes[VIDEO_MAX_PLANES] = {0};
    struct v4l2_buffer b = {0};
    b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    b.memory = V4L2_MEMORY_MMAP;
    b.index = i;
    b.length = m->cap_planes;
    b.m.planes = planes;
    if (xioctl(m->fd, VIDIOC_QBUF, &b) == -1) return -1;
  }
  int t = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  if (xioctl(m->fd, VIDIOC_STREAMON, &t) == -1) return -1;
  t = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  if (xioctl(m->fd, VIDIOC_STREAMON, &t) == -1) return -1;
  return 0;
}

/* Full bring-up with MMAP OUTPUT buffers (the decoder, and the encoder fallback). */
static int m2m_start(M2M* m) {
  if (m2m_reqbuf_mmap(m, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, 2) < 0) return -1;
  return m2m_start_after_reqbuf(m);
}

/* Synchronous one-in/one-out. For MMAP OUTPUT, `in` is copied into the buffer.
 * For DMABUF OUTPUT, `dmafd` is the imported buffer fd (in/in_len ignored).
 * Returns coded data via out/out_len; keyframe set for H.264 IDR. */
static int m2m_process(M2M* m, const void* in, size_t in_len, int dmafd, void** out, size_t* out_len,
                       int* keyframe, int* cap_index) {
  struct v4l2_plane oplanes[VIDEO_MAX_PLANES] = {0};
  struct v4l2_buffer ob = {0};
  ob.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  ob.memory = m->out_memory;
  ob.index = 0;
  ob.length = m->out_planes;
  ob.m.planes = oplanes;
  if (m->out_memory == V4L2_MEMORY_DMABUF) {
    oplanes[0].m.fd = dmafd;
    oplanes[0].bytesused = (uint32_t)m->w * m->h * 3 / 2;
    oplanes[0].length = oplanes[0].bytesused;
  } else if (m->out_planes >= 2) {
    /* multi-plane NV12 input (encoder): split contiguous `in` (Y then UV). */
    size_t ysz = (size_t)m->w * m->h, uvsz = ysz / 2;
    if (ysz > m->out[0].length[0]) ysz = m->out[0].length[0];
    if (uvsz > m->out[0].length[1]) uvsz = m->out[0].length[1];
    memcpy(m->out[0].start[0], in, ysz);
    memcpy(m->out[0].start[1], (const uint8_t*)in + (size_t)m->w * m->h, uvsz);
    oplanes[0].bytesused = ysz;
    oplanes[0].length = m->out[0].length[0];
    oplanes[1].bytesused = uvsz;
    oplanes[1].length = m->out[0].length[1];
  } else {
    size_t cap0 = m->out[0].length[0];
    size_t n = in_len < cap0 ? in_len : cap0;
    memcpy(m->out[0].start[0], in, n);
    oplanes[0].bytesused = n;
    oplanes[0].length = m->out[0].length[0];
  }
  if (xioctl(m->fd, VIDIOC_QBUF, &ob) == -1) { logmsg("m2m: QBUF(OUTPUT) %s", strerror(errno)); return -1; }

  /* Wait (bounded) for the coded frame to be ready (POLLIN on a CAPTURE-side M2M
   * fd), rather than blocking forever in DQBUF - a stalled, uninterruptible V4L2
   * wait is how the driver wedges. POLLIN fires only when the encode/decode of
   * this frame is done, at which point both queues are dequeue-able. */
  struct pollfd pfd = {.fd = m->fd, .events = POLLIN};
  int pr = poll(&pfd, 1, 3000);
  if (pr <= 0) { logmsg("m2m: codec poll timeout/err (%d) - dropping frame", pr); return -1; }

  struct v4l2_plane o2[VIDEO_MAX_PLANES] = {0};
  struct v4l2_buffer odq = {0};
  odq.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  odq.memory = m->out_memory;
  odq.length = m->out_planes;
  odq.m.planes = o2;
  if (xioctl(m->fd, VIDIOC_DQBUF, &odq) == -1) { logmsg("m2m: DQBUF(OUTPUT) %s", strerror(errno)); return -1; }

  struct v4l2_plane cp[VIDEO_MAX_PLANES] = {0};
  struct v4l2_buffer cb = {0};
  cb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  cb.memory = V4L2_MEMORY_MMAP;
  cb.length = m->cap_planes;
  cb.m.planes = cp;
  if (xioctl(m->fd, VIDIOC_DQBUF, &cb) == -1) { logmsg("m2m: DQBUF(CAPTURE) %s", strerror(errno)); return -1; }
  if (m->cap_planes >= 2) {
    /* multi-plane NV12 output (decoder): gather Y + UV into one contiguous buffer.
     * Raw planes report bytesused=0 on this driver, so use the known sizes. */
    size_t ysz = (size_t)m->w * m->h, uvsz = ysz / 2;
    memcpy(m->gather, (const void*)m->cap[cb.index].start[0], ysz);
    memcpy(m->gather + ysz, (const void*)m->cap[cb.index].start[1], uvsz);
    *out = m->gather;
    *out_len = ysz + uvsz;
  } else {
    *out = m->cap[cb.index].start[0];
    *out_len = cp[0].bytesused;
  }
  if (keyframe) *keyframe = (cb.flags & V4L2_BUF_FLAG_KEYFRAME) ? 1 : 0;
  if (cap_index) *cap_index = cb.index;

  struct v4l2_plane rqp[VIDEO_MAX_PLANES] = {0};
  struct v4l2_buffer rq = cb;
  rq.m.planes = rqp;
  rqp[0].length = m->cap[cb.index].length[0];
  xioctl(m->fd, VIDIOC_QBUF, &rq);
  return 0;
}

/* Clean drain on shutdown (best-effort). */
static void m2m_drain(M2M* m) {
  struct v4l2_encoder_cmd cmd = {0};
  cmd.cmd = V4L2_ENC_CMD_STOP;
  xioctl(m->fd, VIDIOC_ENCODER_CMD, &cmd); /* ignore errors; decoder ctx will just no-op */
}

static void m2m_close(M2M* m) {
  if (m->fd < 0) return;
  int t = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  xioctl(m->fd, VIDIOC_STREAMOFF, &t);
  t = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  xioctl(m->fd, VIDIOC_STREAMOFF, &t);
  for (int i = 0; i < m->n_out; i++)
    for (int p = 0; p < m->out[i].nplanes; p++)
      if (m->out[i].start[p]) munmap(m->out[i].start[p], m->out[i].length[p]);
  for (int i = 0; i < m->n_cap; i++) {
    for (int p = 0; p < m->cap[i].nplanes; p++)
      if (m->cap[i].start[p]) munmap(m->cap[i].start[p], m->cap[i].length[p]);
    if (m->cap[i].dmafd >= 0) close(m->cap[i].dmafd);
  }
  free(m->gather);
  close(m->fd);
  m->fd = -1;
}

/* ----------------------------------------------------------------------------- */
/* simulcast streams                                                             */
/* ----------------------------------------------------------------------------- */
typedef struct {
  int w, h;         /* encode resolution */
  int bitrate, gop, rc;
  uint32_t profile;
  const char* outpath;
  FILE* fp;
  M2M enc;
  uint8_t* scalebuf; /* NV12 scratch if this stream is downscaled, else NULL */
  int cur_bitrate;
  long frames, keyframes;
  uint64_t bytes;
} Stream;

/* ----------------------------------------------------------------------------- */
/* adaptive bitrate (AIMD) over a unix control socket                            */
/* ----------------------------------------------------------------------------- */
/* Control commands (newline/space text, sent to the unix dgram socket):
 *   bitrate <bps>     - set primary target bitrate directly
 *   loss <percent>    - report packet loss; AIMD adjusts bitrate
 *   idr               - force a keyframe now
 *   snapshot <path>   - (handled by caller) capture current frame as JPEG
 *   stats             - log current stats
 */
static int ctrl_open(const char* path) {
  int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0);
  if (fd < 0) return -1;
  struct sockaddr_un a = {0};
  a.sun_family = AF_UNIX;
  snprintf(a.sun_path, sizeof(a.sun_path), "%s", path);
  unlink(path);
  if (bind(fd, (struct sockaddr*)&a, sizeof(a)) < 0) {
    logmsg("ctrl: bind %s failed: %s (control disabled)", path, strerror(errno));
    close(fd);
    return -1;
  }
  return fd;
}

/* AIMD: high loss -> multiplicative decrease, low loss -> additive increase. */
static int aimd(int cur, double loss_pct) {
  int next = cur;
  if (loss_pct > 5.0) next = (int)(cur * 0.7);
  else if (loss_pct > 2.0) next = (int)(cur * 0.85);
  else if (loss_pct < 0.5) next = cur + 200000; /* +200 kbit/s probe */
  if (next > BITRATE_MAX) next = BITRATE_MAX;
  if (next < BITRATE_MIN) next = BITRATE_MIN;
  return next;
}

/* ----------------------------------------------------------------------------- */
/* signals                                                                       */
/* ----------------------------------------------------------------------------- */
static void on_term(int s) { (void)s; g_run = 0; }
static void on_usr1(int s) { (void)s; g_force_idr = 1; }

/* ----------------------------------------------------------------------------- */
/* main                                                                          */
/* ----------------------------------------------------------------------------- */
static void usage(const char* p) {
  fprintf(stderr,
          "guppycam - open V4L2 camera daemon for the Creality KE (Ingenic X2000)\n"
          "usage: %s [options]\n"
          "  --cam <dev>        UVC device              (default /dev/video4)\n"
          "  --codec <dev>      Helix M2M device        (default /dev/video1)\n"
          "  --input <fmt>      auto|h264|mjpeg|yuyv    (default auto)\n"
          "  -w,--width <n>     capture width           (default 1280)\n"
          "  -h,--height <n>    capture height          (default 720)\n"
          "  -f,--fps <n>       fps                     (default 15)\n"
          "  -b,--bitrate <n>   primary H.264 bitrate   (default 2000000)\n"
          "  -g,--gop <n>       keyframe interval       (default = fps)\n"
          "     --rc <mode>     cbr|vbr|cq              (default cbr)\n"
          "     --profile <p>   baseline|main|high      (default main)\n"
          "  -o,--out <file>    primary H.264 output    (default stdout)\n"
          "     --stream WxH@bps[:file]  add a simulcast stream (repeatable)\n"
          "     --zerocopy <m>  auto|on|off (dmabuf decode->encode, default auto)\n"
          "     --control <sock> unix control socket    (default off)\n"
          "     --memfd        stream0 H.264 -> a cam_app-layout memfd (feed guppy-webrtc)\n"
          "     --mjpeg <port> serve MJPEG (native passthrough @ master res) on <port>\n"
          "     --mjpeg-low <port> serve a downscaled 640x360 HW-JPEG MJPEG on <port>\n"
          "     --ws <port>    serve raw H.264 over WebSocket on <port> (Mainsail jmuxer)\n"
          "     --frames <n>    stop after n frames     (default 0=forever)\n"
          "     --snapshot <f>  one MJPEG/JPEG frame to f and exit\n"
          "  SIGUSR1 = force keyframe.\n",
          p);
}

static uint32_t parse_profile(const char* s) {
  if (!strcmp(s, "baseline")) return GC_PROFILE_BASELINE;
  if (!strcmp(s, "high")) return GC_PROFILE_HIGH;
  return GC_PROFILE_MAIN;
}
static int parse_rc(const char* s) {
  if (!strcmp(s, "vbr")) return GC_RC_VBR;
  if (!strcmp(s, "cq")) return GC_RC_CQ;
  return GC_RC_CBR;
}
static int parse_input(const char* s) {
  if (!strcmp(s, "h264")) return SRC_H264;
  if (!strcmp(s, "mjpeg")) return SRC_MJPEG;
  if (!strcmp(s, "yuyv")) return SRC_YUYV;
  return -1; /* auto */
}

static void enc_apply_controls(M2M* enc, Stream* s) {
  set_ctrl(enc->fd, V4L2_CID_MPEG_VIDEO_BITRATE_MODE, s->rc);
  set_ctrl(enc->fd, V4L2_CID_MPEG_VIDEO_BITRATE, s->bitrate);
  set_ctrl(enc->fd, V4L2_CID_MPEG_VIDEO_GOP_SIZE, s->gop);
  set_ctrl(enc->fd, V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE, 1);
  set_ctrl(enc->fd, V4L2_CID_MPEG_VIDEO_H264_PROFILE, s->profile);
  set_ctrl(enc->fd, V4L2_CID_MPEG_VIDEO_HEADER_MODE, 1);
  s->cur_bitrate = s->bitrate;
}

int main(int argc, char** argv) {
  const char* cam_dev = "/dev/video4";
  const char* codec_dev = "/dev/video1";
  int w = 1280, h = 720, fps = 15;
  int bitrate = 2000000, gop = -1, rc = GC_RC_CBR;
  uint32_t profile = GC_PROFILE_MAIN;
  const char* outpath = NULL;
  const char* snappath = NULL;
  const char* ctrlpath = NULL;
  int force_input = -1;
  int zerocopy = 1; /* auto */
  int use_memfd = 0;
  int mjpeg_port = 0;
  int mjpeg_low_port = 0;
  int ws_port = 0;
  long max_frames = 0;
  MemfdSink sink = {0};

  Stream streams[MAX_STREAMS];
  memset(streams, 0, sizeof(streams));
  int nstreams = 0;

  for (int i = 1; i < argc; i++) {
    const char* a = argv[i];
#define NEXT() (i + 1 < argc ? argv[++i] : "")
    if (!strcmp(a, "--cam")) cam_dev = NEXT();
    else if (!strcmp(a, "--codec")) codec_dev = NEXT();
    else if (!strcmp(a, "--input")) force_input = parse_input(NEXT());
    else if (!strcmp(a, "-w") || !strcmp(a, "--width")) w = atoi(NEXT());
    else if (!strcmp(a, "-h") || !strcmp(a, "--height")) h = atoi(NEXT());
    else if (!strcmp(a, "-f") || !strcmp(a, "--fps")) fps = atoi(NEXT());
    else if (!strcmp(a, "-b") || !strcmp(a, "--bitrate")) bitrate = atoi(NEXT());
    else if (!strcmp(a, "-g") || !strcmp(a, "--gop")) gop = atoi(NEXT());
    else if (!strcmp(a, "--rc")) rc = parse_rc(NEXT());
    else if (!strcmp(a, "--profile")) profile = parse_profile(NEXT());
    else if (!strcmp(a, "-o") || !strcmp(a, "--out")) outpath = NEXT();
    else if (!strcmp(a, "--zerocopy")) { const char* m = NEXT(); zerocopy = !strcmp(m, "off") ? 0 : !strcmp(m, "on") ? 2 : 1; }
    else if (!strcmp(a, "--control")) ctrlpath = NEXT();
    else if (!strcmp(a, "--memfd")) use_memfd = 1;
    else if (!strcmp(a, "--mjpeg")) mjpeg_port = atoi(NEXT());
    else if (!strcmp(a, "--mjpeg-low")) mjpeg_low_port = atoi(NEXT());
    else if (!strcmp(a, "--ws")) ws_port = atoi(NEXT());
    else if (!strcmp(a, "--frames")) max_frames = atol(NEXT());
    else if (!strcmp(a, "--snapshot")) snappath = NEXT();
    else if (!strcmp(a, "--stream")) {
      if (nstreams >= MAX_STREAMS - 1) { logmsg("too many --stream"); return 2; }
      /* parse WxH@bps[:file] */
      const char* spec = NEXT();
      Stream* s = &streams[1 + nstreams];
      char fbuf[128] = {0};
      int sw, sh, sb;
      if (sscanf(spec, "%dx%d@%d", &sw, &sh, &sb) == 3) {
        s->w = sw; s->h = sh; s->bitrate = sb; s->gop = (gop < 0 ? fps : gop);
        s->rc = rc; s->profile = profile;
        const char* colon = strchr(spec, ':');
        if (colon) { snprintf(fbuf, sizeof(fbuf), "%s", colon + 1); s->outpath = strdup(fbuf); }
        nstreams++;
      } else logmsg("bad --stream spec '%s' (want WxH@bps[:file])", spec);
    }
    else if (!strcmp(a, "--help")) { usage(argv[0]); return 0; }
    else { logmsg("unknown arg: %s", a); usage(argv[0]); return 2; }
#undef NEXT
  }
  if (gop < 0) gop = fps;
  if (bitrate > BITRATE_MAX) bitrate = BITRATE_MAX;

  struct sigaction sa = {0};
  sa.sa_handler = on_term;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);
  sa.sa_handler = on_usr1;
  sigaction(SIGUSR1, &sa, NULL);
  signal(SIGPIPE, SIG_IGN);

  /* ---- snapshot mode ---- */
  if (snappath) {
    Capture cap;
    if (cap_open(&cap, cam_dev, w, h, fps, SRC_MJPEG) < 0) return 1;
    void* data; size_t len; int idx = -1;
    for (int k = 0; k < 4 && g_run; k++) {
      if (idx >= 0) cap_release(&cap, idx);
      idx = cap_get(&cap, &data, &len, 2000);
      if (idx < 0) { logmsg("snapshot: timeout"); cap_close(&cap); return 1; }
    }
    FILE* f = fopen(snappath, "wb");
    if (!f) { cap_close(&cap); return 1; }
    fwrite(data, 1, len, f);
    fclose(f);
    logmsg("snapshot: wrote %zu bytes to %s", len, snappath);
    cap_close(&cap);
    return 0;
  }

  /* ---- open camera (auto-format) ---- */
  Capture cap;
  if (cap_open(&cap, cam_dev, w, h, fps, force_input) < 0) return 1;
  w = cap.w;
  h = cap.h;

  /* primary stream (stream 0) = full capture resolution */
  Stream* s0 = &streams[0];
  s0->w = w; s0->h = h; s0->bitrate = bitrate; s0->gop = gop; s0->rc = rc;
  s0->profile = profile; s0->outpath = outpath;
  int total_streams = 1 + nstreams;

  /* H.264 sink: stream0 either to a memfd (live feed for guppy-webrtc) or a file/stdout */
  if (use_memfd) {
    if (memfd_sink_open(&sink, "main_memfd") < 0) { logmsg("memfd sink open failed: %s", strerror(errno)); return 1; }
    logmsg("guppycam: stream0 H.264 -> memfd (fd %d, name main_memfd)", sink.fd);
  }
  for (int i = 0; i < total_streams; i++) {
    Stream* s = &streams[i];
    if (i == 0 && (use_memfd || ws_port)) { s->fp = NULL; continue; }  /* stream0 -> memfd/ws */
    s->fp = s->outpath ? fopen(s->outpath, "wb") : (i == 0 ? stdout : NULL);
    if (!s->fp) { logmsg("stream %d: cannot open output '%s'", i, s->outpath ? s->outpath : "(none)"); return 1; }
  }

  /* ---- H.264 passthrough fast-path (camera already emits H.264) ---- */
  if (cap.kind == SRC_H264) {
    logmsg("guppycam: H.264 passthrough (no transcode). simulcast/control disabled in this mode.");
    long frames = 0;
    while (g_run) {
      void* d; size_t l;
      int idx = cap_get(&cap, &d, &l, 1000);
      if (idx < 0) continue;
      if (ws_port > 0) h264_publish(d, l);
      if (use_memfd) memfd_sink_write(&sink, d, l);
      if (s0->fp) fwrite(d, 1, l, s0->fp);
      cap_release(&cap, idx);
      if (max_frames && ++frames >= max_frames) break;
    }
    if (s0->fp != stdout) fclose(s0->fp);
    cap_close(&cap);
    return 0;
  }

  /* ---- decoder (MJPEG path only); YUYV is converted on CPU ---- */
  M2M dec;
  int have_dec = 0;
  uint8_t* nv12_cpu = NULL;  /* YUYV->NV12 scratch */
  uint8_t* mjpeg_fix = NULL; /* Huffman-table-spliced MJPEG scratch */
  size_t mjpeg_fix_cap = 0;
  if (cap.kind == SRC_MJPEG) {
    if (m2m_open(&dec, codec_dev, w, h, V4L2_PIX_FMT_JPEG, V4L2_PIX_FMT_NV12) < 0) return 1;
    if (m2m_start(&dec) < 0) { logmsg("decoder start failed"); return 1; }
    have_dec = 1;
    mjpeg_fix_cap = (size_t)w * h * 2 + sizeof(STD_DHT) + 1024;
    mjpeg_fix = malloc(mjpeg_fix_cap);
    if (!mjpeg_fix) return 1;
  } else { /* YUYV */
    nv12_cpu = malloc((size_t)w * h * 3 / 2);
    if (!nv12_cpu) return 1;
  }

  /* ---- encoders (one per stream) ---- */
  for (int i = 0; i < total_streams; i++) {
    Stream* s = &streams[i];
    if (m2m_open(&s->enc, codec_dev, s->w, s->h, V4L2_PIX_FMT_NV12, V4L2_PIX_FMT_H264) < 0) return 1;
    enc_apply_controls(&s->enc, s);
    /* zero-copy dmabuf decode->encode: OPT-IN ONLY (--zerocopy on). On the KE's
     * 4.4 helix driver, REQBUFS(DMABUF) succeeds but QBUF rejects the imported fd
     * with EFAULT, so "auto" must NOT use it - the memcpy path is the reliable one.
     * Kept behind an explicit flag for hardware that genuinely supports import. */
    if (i == 0 && have_dec && zerocopy == 2 && s->w == w && s->h == h) {
      if (m2m_export_capture(&dec) == 0 && m2m_reqbuf_dmabuf_out(&s->enc, 2) == 0) {
        logmsg("stream0: zero-copy dmabuf decode->encode ENABLED (experimental)");
      } else {
        logmsg("stream0: zero-copy requested but unsupported; using copy");
        s->enc.out_memory = V4L2_MEMORY_MMAP;
        if (m2m_reqbuf_mmap(&s->enc, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, 2) < 0) return 1;
      }
    } else {
      if (m2m_reqbuf_mmap(&s->enc, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, 2) < 0) return 1;
    }
    /* stream-on (capture queue + output queue) */
    if (m2m_start_after_reqbuf(&s->enc) < 0) { logmsg("encoder %d start failed", i); return 1; }
    if (s->w != w || s->h != h) {
      s->scalebuf = malloc((size_t)s->w * s->h * 3 / 2);
      if (!s->scalebuf) return 1;
    }
  }

  /* low (remote) stream: downscale master NV12 to 640x360 and HW-JPEG-encode it */
  M2M jpeg_low = {0};
  int have_jlow = 0;
  uint8_t* low_scale = NULL;
  if (mjpeg_low_port > 0 && cap.kind != SRC_H264) {
    if (m2m_open(&jpeg_low, codec_dev, 640, 360, V4L2_PIX_FMT_NV12, V4L2_PIX_FMT_JPEG) < 0) return 1;
    set_ctrl(jpeg_low.fd, V4L2_CID_JPEG_COMPRESSION_QUALITY, 1);
    if (m2m_start(&jpeg_low) < 0) { logmsg("jpeg-low encoder start failed"); return 1; }
    low_scale = malloc((size_t)640 * 360 * 3 / 2);
    if (!low_scale) return 1;
    have_jlow = 1;
  }

  int ctrlfd = ctrlpath ? ctrl_open(ctrlpath) : -1;
  if (mjpeg_port > 0) {
    g_mout_mid.port = mjpeg_port;
    pthread_t t;
    if (pthread_create(&t, NULL, mjpeg_server, &g_mout_mid) == 0) pthread_detach(t);
  }
  if (mjpeg_low_port > 0) {
    g_mout_low.port = mjpeg_low_port;
    pthread_t t;
    if (pthread_create(&t, NULL, mjpeg_server, &g_mout_low) == 0) pthread_detach(t);
  }
  if (ws_port > 0) {
    pthread_t t;
    if (pthread_create(&t, NULL, ws_server, (void*)(intptr_t)ws_port) == 0) pthread_detach(t);
  }

  logmsg("guppycam: %dx%d@%dfps, %d stream(s), src=%s. SIGUSR1=IDR%s%s", w, h, fps, total_streams,
         cap.kind == SRC_MJPEG ? "MJPEG" : "YUYV", ctrlfd >= 0 ? ", ctrl=on" : "",
         streams[0].enc.out_memory == V4L2_MEMORY_DMABUF ? ", zero-copy" : "");

  uint64_t t0 = now_ms();
  long loops = 0;
  while (g_run) {
    /* control socket */
    if (ctrlfd >= 0) {
      char cmd[128];
      ssize_t n;
      while ((n = recv(ctrlfd, cmd, sizeof(cmd) - 1, 0)) > 0) {
        cmd[n] = 0;
        if (!strncmp(cmd, "bitrate", 7)) {
          int b = atoi(cmd + 7);
          if (b >= BITRATE_MIN && b <= BITRATE_MAX) {
            set_ctrl(streams[0].enc.fd, V4L2_CID_MPEG_VIDEO_BITRATE, b);
            streams[0].cur_bitrate = b;
            logmsg("ctrl: bitrate -> %d", b);
          }
        } else if (!strncmp(cmd, "loss", 4)) {
          double loss = atof(cmd + 4);
          int b = aimd(streams[0].cur_bitrate, loss);
          if (b != streams[0].cur_bitrate) {
            set_ctrl(streams[0].enc.fd, V4L2_CID_MPEG_VIDEO_BITRATE, b);
            logmsg("ctrl: loss %.1f%% -> bitrate %d (was %d)", loss, b, streams[0].cur_bitrate);
            streams[0].cur_bitrate = b;
          }
        } else if (!strncmp(cmd, "idr", 3)) {
          g_force_idr = 1;
        } else if (!strncmp(cmd, "stats", 5)) {
          logmsg("stats: stream0 %ld frames, %ld kf, %d bps", streams[0].frames,
                 streams[0].keyframes, streams[0].cur_bitrate);
        }
      }
    }

    /* capture one frame */
    void* raw; size_t raw_len;
    int idx = cap_get(&cap, &raw, &raw_len, 1000);
    if (idx < 0) {
      if (!g_run) break;
      logmsg("capture stall - attempting reconnect");
      cap_close(&cap);
      while (g_run && cap_open(&cap, cam_dev, w, h, fps, force_input) < 0) {
        struct timespec ts = {1, 0};
        nanosleep(&ts, NULL);
      }
      continue;
    }

    if (g_force_idr) {
      for (int i = 0; i < total_streams; i++)
        set_ctrl(streams[i].enc.fd, V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME, 1);
      g_force_idr = 0;
    }

    /* feed the mid MJPEG server the native camera frame (it already has DHT) */
    if (mjpeg_port > 0 && cap.kind == SRC_MJPEG) mjpeg_publish(&g_mout_mid, raw, raw_len);

    /* get full-res NV12 */
    void* nv12 = NULL; size_t nv12_len = 0; int nv12_dmafd = -1, dec_capidx = 0;
    if (cap.kind == SRC_MJPEG) {
      /* insert standard Huffman tables if the camera's MJPEG lacks them */
      size_t fl = mjpeg_ensure_huffman(raw, raw_len, mjpeg_fix, mjpeg_fix_cap);
      const void* jin = fl ? (const void*)mjpeg_fix : raw;
      size_t jlen = fl ? fl : raw_len;
      if (m2m_process(&dec, jin, jlen, -1, &nv12, &nv12_len, NULL, &dec_capidx) != 0) {
        cap_release(&cap, idx);
        continue;
      }
      nv12_dmafd = dec.cap[dec_capidx].dmafd; /* fd of the buffer actually produced */
      static int _dbg = 0;
      if (!_dbg) {
        _dbg = 1;
        logmsg("decode: first NV12 = %zu bytes (expect %u), idx=%d", nv12_len,
               (unsigned)((size_t)w * h * 3 / 2), dec_capidx);
      }
    } else {
      yuyv_to_nv12(raw, nv12_cpu, w, h);
      nv12 = nv12_cpu;
      nv12_len = (size_t)w * h * 3 / 2;
    }
    cap_release(&cap, idx);

    /* low/remote MJPEG: lazy - only downscale+JPEG-encode when a viewer is connected */
    if (have_jlow && g_mout_low.clients > 0) {
      nv12_downscale(nv12, w, h, low_scale, 640, 360);
      void* jl;
      size_t jll;
      if (m2m_process(&jpeg_low, low_scale, (size_t)640 * 360 * 3 / 2, -1, &jl, &jll, NULL, NULL) == 0)
        mjpeg_publish(&g_mout_low, jl, jll);
    }

    /* encode for every stream */
    for (int i = 0; i < total_streams; i++) {
      Stream* s = &streams[i];
      void* h264; size_t h264_len; int key = 0;
      const void* enc_in = nv12;
      size_t enc_in_len = nv12_len;
      if (s->scalebuf) {
        nv12_downscale(nv12, w, h, s->scalebuf, s->w, s->h);
        enc_in = s->scalebuf;
        enc_in_len = (size_t)s->w * s->h * 3 / 2;
      }
      int dmafd = (i == 0 && s->enc.out_memory == V4L2_MEMORY_DMABUF) ? nv12_dmafd : -1;
      if (m2m_process(&s->enc, enc_in, enc_in_len, dmafd, &h264, &h264_len, &key, NULL) == 0) {
        if (i == 0 && ws_port > 0) h264_publish(h264, h264_len);
        if (i == 0 && use_memfd) memfd_sink_write(&sink, h264, h264_len);
        if (s->fp) { fwrite(h264, 1, h264_len, s->fp); fflush(s->fp); }
        s->frames++; s->bytes += h264_len; if (key) s->keyframes++;
      }
    }

    if (max_frames && streams[0].frames >= max_frames) break;
    if ((++loops % (fps * 5)) == 0) {
      double secs = (now_ms() - t0) / 1000.0;
      logmsg("  %ld frames, %ld kf, %.0f kbit/s", streams[0].frames, streams[0].keyframes,
             secs > 0 ? streams[0].bytes * 8.0 / secs / 1000.0 : 0);
    }
  }

  /* ---- shutdown ---- */
  for (int i = 0; i < total_streams; i++) {
    m2m_drain(&streams[i].enc);
    m2m_close(&streams[i].enc);
    if (streams[i].fp && streams[i].fp != stdout) fclose(streams[i].fp);
    free(streams[i].scalebuf);
  }
  if (have_dec) m2m_close(&dec);
  if (have_jlow) m2m_close(&jpeg_low);
  free(low_scale);
  free(nv12_cpu);
  free(mjpeg_fix);
  if (ctrlfd >= 0) { close(ctrlfd); unlink(ctrlpath); }
  cap_close(&cap);
  logmsg("guppycam: stopped (stream0 %ld frames, %ld keyframes).", streams[0].frames,
         streams[0].keyframes);
  return 0;
}
