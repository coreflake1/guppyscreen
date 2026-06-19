/* guppycam - an open, controllable camera daemon for the Creality KE (Ingenic X2000).
 *
 * A from-scratch, fully-documented replacement for Creality's closed `cam_app`.
 * It drives the exact same hardware path cam_app uses, but every stage is open
 * V4L2 and every encoder parameter is under our control:
 *
 *     USB UVC camera (/dev/video4, MJPEG)
 *        │  V4L2 capture (MMAP)
 *        ▼
 *     Helix HW JPEG decoder (/dev/video1, V4L2 M2M)   MJPEG ──► NV12
 *        │
 *        ▼
 *     Helix HW H.264 encoder (/dev/video1, V4L2 M2M)  NV12 ──► H.264 Annex-B
 *        │                                            (bitrate / GOP / profile /
 *        ▼                                             rate-control / force-IDR)
 *     output: raw .h264 to stdout|file, and/or MJPEG snapshot passthrough
 *
 * Zero proprietary SDK (no libimp), no cloud, no AI middleware. The whole thing
 * is a single small static binary.
 *
 * Hardware facts verified on-device (Ender-3 V3 KE, kernel 4.4.94):
 *   - video4 "CCX2F3298" uvcvideo: MJPG + YUYV, up to 1920x1080@30
 *   - video1 "helix-venc" V4L2 M2M multiplanar:
 *       OUTPUT (raw in):  NV12, NV21, YU12, plus JPEG/H264 (decode mode)
 *       CAPTURE (coded):  H264, JPEG, plus NV12 (decode mode)
 *       controls: gop_size, bitrate(<=4Mbps), bitrate_mode(VBR/CBR/CQ),
 *                 frame_rc_enable, header_mode, h264 i/p QP, i_frame_period,
 *                 h264_level, h264_profile, jpeg compression_quality
 *
 * Build: see build.sh (cross-compiles a static mipsel binary in the guppydev image).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <linux/videodev2.h>

/* ----- V4L2 control IDs (define explicitly so we don't depend on header age) ----- */
#ifndef V4L2_CID_MPEG_VIDEO_BITRATE
#define V4L2_CID_MPEG_VIDEO_BITRATE            (V4L2_CID_MPEG_BASE + 207)
#endif
#ifndef V4L2_CID_MPEG_VIDEO_BITRATE_MODE
#define V4L2_CID_MPEG_VIDEO_BITRATE_MODE       (V4L2_CID_MPEG_BASE + 206)
#endif
#ifndef V4L2_CID_MPEG_VIDEO_GOP_SIZE
#define V4L2_CID_MPEG_VIDEO_GOP_SIZE           (V4L2_CID_MPEG_BASE + 202)
#endif
#ifndef V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME
#define V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME    (V4L2_CID_MPEG_BASE + 211)
#endif
#ifndef V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE
#define V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE    (V4L2_CID_MPEG_BASE + 200)
#endif
#ifndef V4L2_CID_MPEG_VIDEO_HEADER_MODE
#define V4L2_CID_MPEG_VIDEO_HEADER_MODE        (V4L2_CID_MPEG_BASE + 210)
#endif
#ifndef V4L2_CID_MPEG_VIDEO_H264_PROFILE
#define V4L2_CID_MPEG_VIDEO_H264_PROFILE       (V4L2_CID_MPEG_BASE + 363)
#endif
#ifndef V4L2_CID_MPEG_VIDEO_H264_LEVEL
#define V4L2_CID_MPEG_VIDEO_H264_LEVEL         (V4L2_CID_MPEG_BASE + 358)
#endif
#ifndef V4L2_CID_JPEG_COMPRESSION_QUALITY
#define V4L2_CID_JPEG_COMPRESSION_QUALITY      (V4L2_CID_JPEG_CLASS_BASE + 3)
#endif
/* Helix-reported menu order for H264 profile (from v4l2-ctl -L on-device):
 * 0 Baseline, 1 Constrained Baseline, 2 Main(default), 3 Extended, 4 High. */
#define GC_PROFILE_BASELINE 0
#define GC_PROFILE_MAIN     2
#define GC_PROFILE_HIGH     4
/* bitrate_mode menu: 0 VBR, 1 CBR(default), 2 CQ */
#define GC_RC_VBR 0
#define GC_RC_CBR 1
#define GC_RC_CQ  2

#define MAX_BUFS 6

/* ----------------------------------------------------------------------------- */
/* logging + ioctl helpers                                                       */
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

/* ioctl with EINTR retry. */
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

/* ----------------------------------------------------------------------------- */
/* UVC capture (single-planar MMAP)                                              */
/* ----------------------------------------------------------------------------- */
typedef struct {
  int fd;
  struct {
    void* start;
    size_t length;
  } buf[MAX_BUFS];
  int nbufs;
  int w, h;
  uint32_t fourcc;
} Capture;

static int cap_open(Capture* c, const char* dev, int w, int h, int fps, uint32_t fourcc) {
  memset(c, 0, sizeof(*c));
  c->w = w;
  c->h = h;
  c->fourcc = fourcc;
  c->fd = open(dev, O_RDWR | O_NONBLOCK);
  if (c->fd < 0) {
    logmsg("capture: open %s failed: %s", dev, strerror(errno));
    return -1;
  }

  struct v4l2_format fmt = {0};
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = w;
  fmt.fmt.pix.height = h;
  fmt.fmt.pix.pixelformat = fourcc;
  fmt.fmt.pix.field = V4L2_FIELD_NONE;
  if (xioctl(c->fd, VIDIOC_S_FMT, &fmt) == -1) {
    logmsg("capture: S_FMT failed: %s", strerror(errno));
    return -1;
  }
  c->w = fmt.fmt.pix.width;   /* driver may adjust */
  c->h = fmt.fmt.pix.height;
  logmsg("capture: %dx%d %.4s (req %dx%d)", c->w, c->h, (char*)&fmt.fmt.pix.pixelformat, w, h);

  struct v4l2_streamparm parm = {0};
  parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  parm.parm.capture.timeperframe.numerator = 1;
  parm.parm.capture.timeperframe.denominator = fps;
  xioctl(c->fd, VIDIOC_S_PARM, &parm);  /* best-effort */

  struct v4l2_requestbuffers req = {0};
  req.count = 4;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;
  if (xioctl(c->fd, VIDIOC_REQBUFS, &req) == -1) {
    logmsg("capture: REQBUFS failed: %s", strerror(errno));
    return -1;
  }
  c->nbufs = req.count;
  for (int i = 0; i < c->nbufs; i++) {
    struct v4l2_buffer b = {0};
    b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    b.memory = V4L2_MEMORY_MMAP;
    b.index = i;
    if (xioctl(c->fd, VIDIOC_QUERYBUF, &b) == -1) return -1;
    c->buf[i].length = b.length;
    c->buf[i].start = mmap(NULL, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, c->fd, b.m.offset);
    if (c->buf[i].start == MAP_FAILED) {
      logmsg("capture: mmap failed: %s", strerror(errno));
      return -1;
    }
    if (xioctl(c->fd, VIDIOC_QBUF, &b) == -1) return -1;
  }
  int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (xioctl(c->fd, VIDIOC_STREAMON, &type) == -1) {
    logmsg("capture: STREAMON failed: %s", strerror(errno));
    return -1;
  }
  return 0;
}

/* Block (poll) for the next captured frame. Returns the buffer index (>=0) and
 * fills data/len; caller MUST call cap_release(idx) when done with the data. */
static int cap_get(Capture* c, void** data, size_t* len, int timeout_ms) {
  struct pollfd pfd = {.fd = c->fd, .events = POLLIN};
  int pr = poll(&pfd, 1, timeout_ms);
  if (pr <= 0) return -1;  /* timeout or error */
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
/* Helix V4L2 M2M context (multiplanar) - used for both JPEG-decode and H.264-encode */
/* ----------------------------------------------------------------------------- */
typedef struct {
  int fd;
  /* OUTPUT queue = raw/source side, CAPTURE queue = coded/dest side */
  struct {
    void* start[VIDEO_MAX_PLANES];
    size_t length[VIDEO_MAX_PLANES];
    int nplanes;
  } out[MAX_BUFS], cap[MAX_BUFS];
  int n_out, n_cap;
  int out_planes, cap_planes;
  int w, h;
} M2M;

static int m2m_reqbuf_mmap(M2M* m, int type, int count,
                           void* dst /* the out[] or cap[] array */, int* nbufs, int* nplanes) {
  struct v4l2_requestbuffers req = {0};
  req.count = count;
  req.type = type;
  req.memory = V4L2_MEMORY_MMAP;
  if (xioctl(m->fd, VIDIOC_REQBUFS, &req) == -1) {
    logmsg("m2m: REQBUFS(type %d) failed: %s", type, strerror(errno));
    return -1;
  }
  *nbufs = req.count;
  for (int i = 0; i < (int)req.count; i++) {
    struct v4l2_plane planes[VIDEO_MAX_PLANES] = {0};
    struct v4l2_buffer b = {0};
    b.type = type;
    b.memory = V4L2_MEMORY_MMAP;
    b.index = i;
    b.length = VIDEO_MAX_PLANES;
    b.m.planes = planes;
    if (xioctl(m->fd, VIDIOC_QUERYBUF, &b) == -1) return -1;
    *nplanes = b.length;
    /* dst is M2M.out or M2M.cap; index into it generically */
    for (int p = 0; p < (int)b.length; p++) {
      void* addr = mmap(NULL, planes[p].length, PROT_READ | PROT_WRITE, MAP_SHARED, m->fd,
                        planes[p].m.mem_offset);
      if (addr == MAP_FAILED) {
        logmsg("m2m: mmap plane failed: %s", strerror(errno));
        return -1;
      }
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
  (void)dst;
  return 0;
}

/* Open + configure a Helix M2M context.
 *   src_fourcc : OUTPUT-queue pixel format (what we feed in)
 *   dst_fourcc : CAPTURE-queue pixel format (what comes out)
 * For decode: src=JPEG, dst=NV12.  For encode: src=NV12, dst=H264. */
static int m2m_open(M2M* m, const char* dev, int w, int h, uint32_t src_fourcc, uint32_t dst_fourcc) {
  memset(m, 0, sizeof(*m));
  m->w = w;
  m->h = h;
  m->fd = open(dev, O_RDWR);
  if (m->fd < 0) {
    logmsg("m2m: open %s failed: %s", dev, strerror(errno));
    return -1;
  }

  /* Generous byte budget for compressed planes so the driver doesn't allocate a
   * buffer too small to hold a frame (would silently truncate). Raw NV12 is sized
   * by the driver from w*h. */
  uint32_t compressed_sz = (uint32_t)w * h;  /* >> any JPEG/H.264 frame at this res */

  /* Set CAPTURE (coded/dest) format first - the stateful-encoder convention. */
  struct v4l2_format cfmt = {0};
  cfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  cfmt.fmt.pix_mp.width = w;
  cfmt.fmt.pix_mp.height = h;
  cfmt.fmt.pix_mp.pixelformat = dst_fourcc;
  cfmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
  cfmt.fmt.pix_mp.num_planes = 1;
  if (dst_fourcc == V4L2_PIX_FMT_H264 || dst_fourcc == V4L2_PIX_FMT_JPEG)
    cfmt.fmt.pix_mp.plane_fmt[0].sizeimage = compressed_sz;
  if (xioctl(m->fd, VIDIOC_S_FMT, &cfmt) == -1) {
    logmsg("m2m: S_FMT(CAPTURE %.4s) failed: %s", (char*)&dst_fourcc, strerror(errno));
    return -1;
  }
  m->cap_planes = cfmt.fmt.pix_mp.num_planes;

  /* Set OUTPUT (raw/source) format. */
  struct v4l2_format ofmt = {0};
  ofmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  ofmt.fmt.pix_mp.width = w;
  ofmt.fmt.pix_mp.height = h;
  ofmt.fmt.pix_mp.pixelformat = src_fourcc;
  ofmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
  ofmt.fmt.pix_mp.num_planes = 1;
  if (src_fourcc == V4L2_PIX_FMT_H264 || src_fourcc == V4L2_PIX_FMT_JPEG)
    ofmt.fmt.pix_mp.plane_fmt[0].sizeimage = compressed_sz;
  if (xioctl(m->fd, VIDIOC_S_FMT, &ofmt) == -1) {
    logmsg("m2m: S_FMT(OUTPUT %.4s) failed: %s", (char*)&src_fourcc, strerror(errno));
    return -1;
  }
  m->out_planes = ofmt.fmt.pix_mp.num_planes;
  return 0;
}

static int m2m_start(M2M* m) {
  void* dummy = NULL;
  if (m2m_reqbuf_mmap(m, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, 2, dummy, &m->n_out, &m->out_planes) < 0)
    return -1;
  if (m2m_reqbuf_mmap(m, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, 2, dummy, &m->n_cap, &m->cap_planes) < 0)
    return -1;
  /* Queue all CAPTURE buffers up front so coded output always has somewhere to land. */
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

/* Synchronous one-in/one-out transcode of a single frame.
 * Copies `in`(in_len) into an OUTPUT buffer, queues it, then dequeues the coded
 * CAPTURE buffer; sets out/out_len. keyframe is set for H.264 IDR frames.
 * The returned out pointer is into a CAPTURE mmap buffer, valid until next call. */
static int m2m_process(M2M* m, const void* in, size_t in_len, void** out, size_t* out_len,
                       int* keyframe) {
  /* 1. fill + queue an OUTPUT buffer (index 0; we run synchronously) */
  struct v4l2_plane oplanes[VIDEO_MAX_PLANES] = {0};
  struct v4l2_buffer ob = {0};
  ob.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  ob.memory = V4L2_MEMORY_MMAP;
  ob.index = 0;
  ob.length = m->out_planes;
  ob.m.planes = oplanes;
  size_t cap0 = m->out[0].length[0];
  size_t n = in_len < cap0 ? in_len : cap0;
  memcpy(m->out[0].start[0], in, n);
  oplanes[0].bytesused = n;
  oplanes[0].length = m->out[0].length[0];
  if (xioctl(m->fd, VIDIOC_QBUF, &ob) == -1) {
    logmsg("m2m: QBUF(OUTPUT) failed: %s", strerror(errno));
    return -1;
  }

  /* 2. dequeue the OUTPUT buffer back (input consumed) */
  struct v4l2_plane oplanes2[VIDEO_MAX_PLANES] = {0};
  struct v4l2_buffer odq = {0};
  odq.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  odq.memory = V4L2_MEMORY_MMAP;
  odq.length = m->out_planes;
  odq.m.planes = oplanes2;
  if (xioctl(m->fd, VIDIOC_DQBUF, &odq) == -1) {
    logmsg("m2m: DQBUF(OUTPUT) failed: %s", strerror(errno));
    return -1;
  }

  /* 3. dequeue the coded CAPTURE buffer */
  struct v4l2_plane cplanes[VIDEO_MAX_PLANES] = {0};
  struct v4l2_buffer cb = {0};
  cb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  cb.memory = V4L2_MEMORY_MMAP;
  cb.length = m->cap_planes;
  cb.m.planes = cplanes;
  if (xioctl(m->fd, VIDIOC_DQBUF, &cb) == -1) {
    logmsg("m2m: DQBUF(CAPTURE) failed: %s", strerror(errno));
    return -1;
  }
  *out = m->cap[cb.index].start[0];
  *out_len = cplanes[0].bytesused;
  if (keyframe) *keyframe = (cb.flags & V4L2_BUF_FLAG_KEYFRAME) ? 1 : 0;

  /* 4. re-queue that CAPTURE buffer for the next frame */
  struct v4l2_plane rqp[VIDEO_MAX_PLANES] = {0};
  struct v4l2_buffer rq = cb;
  rq.m.planes = rqp;
  rqp[0].length = m->cap[cb.index].length[0];
  xioctl(m->fd, VIDIOC_QBUF, &rq);
  return 0;
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
  for (int i = 0; i < m->n_cap; i++)
    for (int p = 0; p < m->cap[i].nplanes; p++)
      if (m->cap[i].start[p]) munmap(m->cap[i].start[p], m->cap[i].length[p]);
  close(m->fd);
  m->fd = -1;
}

/* ----------------------------------------------------------------------------- */
/* signals                                                                       */
/* ----------------------------------------------------------------------------- */
static void on_term(int s) {
  (void)s;
  g_run = 0;
}
static void on_usr1(int s) {
  (void)s;
  g_force_idr = 1; /* request a keyframe on the next encoded frame */
}

/* ----------------------------------------------------------------------------- */
/* main                                                                          */
/* ----------------------------------------------------------------------------- */
static void usage(const char* p) {
  fprintf(stderr,
          "guppycam - open V4L2 camera daemon for the Creality KE (Ingenic X2000)\n"
          "usage: %s [options]\n"
          "  --cam <dev>        UVC capture device      (default /dev/video4)\n"
          "  --codec <dev>      Helix M2M device        (default /dev/video1)\n"
          "  -w, --width <n>    capture width           (default 1280)\n"
          "  -h, --height <n>   capture height          (default 720)\n"
          "  -f, --fps <n>      capture/encode fps       (default 15)\n"
          "  -b, --bitrate <n>  H.264 bitrate bits/s     (default 2000000, max 4000000)\n"
          "  -g, --gop <n>      keyframe interval frames (default = fps -> 1s)\n"
          "      --rc <mode>    cbr|vbr|cq               (default cbr)\n"
          "      --profile <p>  baseline|main|high       (default main)\n"
          "  -o, --out <file>   write H.264 to file      (default stdout)\n"
          "      --frames <n>   stop after n frames      (default 0 = run forever)\n"
          "      --snapshot <f> capture ONE MJPEG frame to f and exit (no encode)\n"
          "  SIGUSR1 forces an IDR/keyframe on the next frame.\n",
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

int main(int argc, char** argv) {
  const char* cam_dev = "/dev/video4";
  const char* codec_dev = "/dev/video1";
  int w = 1280, h = 720, fps = 15;
  int bitrate = 2000000, gop = -1, rc = GC_RC_CBR;
  uint32_t profile = GC_PROFILE_MAIN;
  const char* outpath = NULL;
  const char* snappath = NULL;
  long max_frames = 0;

  static const struct {
    const char* l;
    const char* s;
    int has_arg;
  } opts[] = {{"--cam", NULL, 1},      {"--codec", NULL, 1},  {"--width", "-w", 1},
              {"--height", "-h", 1},   {"--fps", "-f", 1},    {"--bitrate", "-b", 1},
              {"--gop", "-g", 1},      {"--rc", NULL, 1},     {"--profile", NULL, 1},
              {"--out", "-o", 1},      {"--frames", NULL, 1}, {"--snapshot", NULL, 1}};
  (void)opts;
  for (int i = 1; i < argc; i++) {
    const char* a = argv[i];
#define NEXT() (i + 1 < argc ? argv[++i] : "")
    if (!strcmp(a, "--cam")) cam_dev = NEXT();
    else if (!strcmp(a, "--codec")) codec_dev = NEXT();
    else if (!strcmp(a, "-w") || !strcmp(a, "--width")) w = atoi(NEXT());
    else if (!strcmp(a, "-h") || !strcmp(a, "--height")) h = atoi(NEXT());
    else if (!strcmp(a, "-f") || !strcmp(a, "--fps")) fps = atoi(NEXT());
    else if (!strcmp(a, "-b") || !strcmp(a, "--bitrate")) bitrate = atoi(NEXT());
    else if (!strcmp(a, "-g") || !strcmp(a, "--gop")) gop = atoi(NEXT());
    else if (!strcmp(a, "--rc")) rc = parse_rc(NEXT());
    else if (!strcmp(a, "--profile")) profile = parse_profile(NEXT());
    else if (!strcmp(a, "-o") || !strcmp(a, "--out")) outpath = NEXT();
    else if (!strcmp(a, "--frames")) max_frames = atol(NEXT());
    else if (!strcmp(a, "--snapshot")) snappath = NEXT();
    else if (!strcmp(a, "--help")) { usage(argv[0]); return 0; }
    else { logmsg("unknown arg: %s", a); usage(argv[0]); return 2; }
#undef NEXT
  }
  if (gop < 0) gop = fps;        /* default: one keyframe per second */
  if (bitrate > 4000000) bitrate = 4000000;

  struct sigaction sa = {0};
  sa.sa_handler = on_term;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);
  sa.sa_handler = on_usr1;
  sigaction(SIGUSR1, &sa, NULL);
  signal(SIGPIPE, SIG_IGN);

  /* ---- snapshot mode: grab one native MJPEG frame, write it, done. ---- */
  if (snappath) {
    Capture cap;
    if (cap_open(&cap, cam_dev, w, h, fps, V4L2_PIX_FMT_MJPEG) < 0) return 1;
    void* data;
    size_t len;
    int idx = -1;
    /* skip the first couple frames (AE/AWB settle), then grab one */
    for (int k = 0; k < 4 && g_run; k++) {
      if (idx >= 0) cap_release(&cap, idx);
      idx = cap_get(&cap, &data, &len, 2000);
      if (idx < 0) { logmsg("snapshot: capture timeout"); cap_close(&cap); return 1; }
    }
    FILE* f = fopen(snappath, "wb");
    if (!f) { logmsg("snapshot: cannot open %s", snappath); cap_close(&cap); return 1; }
    fwrite(data, 1, len, f);
    fclose(f);
    logmsg("snapshot: wrote %zu bytes (MJPEG passthrough) to %s", len, snappath);
    cap_close(&cap);
    return 0;
  }

  /* ---- streaming pipeline: UVC MJPEG -> Helix decode -> NV12 -> Helix encode -> H.264 ---- */
  logmsg("guppycam: %dx%d@%dfps  bitrate=%d gop=%d rc=%d profile=%d", w, h, fps, bitrate, gop, rc,
         profile);

  Capture cap;
  if (cap_open(&cap, cam_dev, w, h, fps, V4L2_PIX_FMT_MJPEG) < 0) return 1;
  w = cap.w;
  h = cap.h;

  M2M dec, enc;
  if (m2m_open(&dec, codec_dev, w, h, V4L2_PIX_FMT_JPEG, V4L2_PIX_FMT_NV12) < 0) return 1;
  if (m2m_start(&dec) < 0) { logmsg("decoder start failed"); return 1; }

  if (m2m_open(&enc, codec_dev, w, h, V4L2_PIX_FMT_NV12, V4L2_PIX_FMT_H264) < 0) return 1;
  /* encoder controls - the whole point: full, documented control of the stream */
  set_ctrl(enc.fd, V4L2_CID_MPEG_VIDEO_BITRATE_MODE, rc);
  set_ctrl(enc.fd, V4L2_CID_MPEG_VIDEO_BITRATE, bitrate);
  set_ctrl(enc.fd, V4L2_CID_MPEG_VIDEO_GOP_SIZE, gop);
  set_ctrl(enc.fd, V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE, 1);
  set_ctrl(enc.fd, V4L2_CID_MPEG_VIDEO_H264_PROFILE, profile);
  /* header_mode=1 -> SPS/PPS prepended to each IDR (best for streaming/late joiners) */
  set_ctrl(enc.fd, V4L2_CID_MPEG_VIDEO_HEADER_MODE, 1);
  if (m2m_start(&enc) < 0) { logmsg("encoder start failed"); return 1; }

  FILE* out = outpath ? fopen(outpath, "wb") : stdout;
  if (!out) { logmsg("cannot open output %s", outpath); return 1; }

  logmsg("guppycam: streaming. SIGUSR1=force-IDR, SIGTERM=stop.");
  long frames = 0, kf = 0;
  uint64_t bytes = 0;
  while (g_run) {
    void *mjpeg, *nv12, *h264;
    size_t mjpeg_len, nv12_len, h264_len;
    int idx = cap_get(&cap, &mjpeg, &mjpeg_len, 1000);
    if (idx < 0) {
      if (g_run) logmsg("capture stall");
      continue;
    }
    if (g_force_idr) {
      set_ctrl(enc.fd, V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME, 1);
      g_force_idr = 0;
    }
    if (m2m_process(&dec, mjpeg, mjpeg_len, &nv12, &nv12_len, NULL) == 0) {
      int key = 0;
      if (m2m_process(&enc, nv12, nv12_len, &h264, &h264_len, &key) == 0) {
        fwrite(h264, 1, h264_len, out);
        bytes += h264_len;
        frames++;
        if (key) kf++;
      }
    }
    cap_release(&cap, idx);
    if (max_frames && frames >= max_frames) break;
    if ((frames % (fps * 5)) == 0 && frames)
      logmsg("  %ld frames, %ld keyframes, %.1f kbit/s avg", frames, kf,
             (frames ? (bytes * 8.0) / (frames / (double)fps) / 1000.0 : 0));
  }

  if (out != stdout) fclose(out);
  m2m_close(&enc);
  m2m_close(&dec);
  cap_close(&cap);
  logmsg("guppycam: stopped after %ld frames (%ld keyframes, %llu bytes).", frames, kf,
         (unsigned long long)bytes);
  return 0;
}
