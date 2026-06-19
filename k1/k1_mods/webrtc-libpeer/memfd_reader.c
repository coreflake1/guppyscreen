#include "memfd_reader.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

struct MemfdReader {
  volatile unsigned char* buf;
  size_t sz;
  unsigned char* a; /* stability double-read scratch */
  unsigned char* b; /* returned frame (stable across one call) */
  uint32_t last_off;
  unsigned char sps[64];
  int sps_len;
  unsigned char pps[64];
  int pps_len;
};

/* Find the last NAL of `type` (7=SPS, 8=PPS) in an Annex-B region. */
static const unsigned char* find_last_nal(const unsigned char* b, size_t len,
                                          int type, size_t* ol) {
  const unsigned char* best = 0;
  size_t bl = 0;
  for (size_t i = 0; i + 5 < len; i++) {
    if (b[i] == 0 && b[i + 1] == 0 && b[i + 2] == 0 && b[i + 3] == 1 &&
        (b[i + 4] & 0x1f) == type) {
      size_t j = i + 4;
      while (j + 4 < len &&
             !(b[j] == 0 && b[j + 1] == 0 && b[j + 2] == 0 && b[j + 3] == 1))
        j++;
      best = b + i;
      bl = j - i;
    }
  }
  *ol = bl;
  return best;
}

MemfdReader* mr_open(const char* path) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) return NULL;
  struct stat st;
  size_t sz = 0;
  if (fstat(fd, &st) == 0) sz = st.st_size;
  if (!sz) sz = 8u << 20;
  volatile unsigned char* buf = mmap(0, sz, PROT_READ, MAP_SHARED, fd, 0);
  close(fd); /* mapping survives the fd close */
  if (buf == MAP_FAILED) return NULL;

  MemfdReader* mr = calloc(1, sizeof(*mr));
  if (!mr) {
    munmap((void*)buf, sz);
    return NULL;
  }
  mr->buf = buf;
  mr->sz = sz;
  mr->a = malloc(sz);
  mr->b = malloc(sz);
  mr->last_off = 0xffffffffu;
  if (!mr->a || !mr->b) {
    mr_close(mr);
    return NULL;
  }

  /* Prime SPS/PPS from whatever is currently in the buffer. */
  size_t sl = 0, pl = 0;
  const unsigned char* sps = find_last_nal((const unsigned char*)buf, sz, 7, &sl);
  const unsigned char* pps = find_last_nal((const unsigned char*)buf, sz, 8, &pl);
  if (sps && sl && sl <= sizeof(mr->sps)) {
    memcpy(mr->sps, sps, sl);
    mr->sps_len = (int)sl;
  }
  if (pps && pl && pl <= sizeof(mr->pps)) {
    memcpy(mr->pps, pps, pl);
    mr->pps_len = (int)pl;
  }
  return mr;
}

int mr_next_frame(MemfdReader* mr, uint8_t** out, int* size) {
  volatile unsigned char* buf = mr->buf;
  size_t sz = mr->sz;

  uint32_t off = *(volatile uint32_t*)(buf + 16);
  if (off == mr->last_off || (size_t)off + 4 >= sz) return 0;

  uint32_t fsz = *(volatile uint32_t*)(buf + off);
  if (fsz == 0 || fsz >= sz || (size_t)off + 4 + fsz > sz) return 0;

  volatile unsigned char* p = buf + off + 4;
  if (!(p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 1)) return 0;

  /* Stability double-read: reject a frame caught mid-write. */
  memcpy(mr->a, (const void*)p, fsz);
  usleep(1500);
  if (*(volatile uint32_t*)(buf + 16) != off) return 0;     /* buffer flipped */
  if (*(volatile uint32_t*)(buf + off) != fsz) return 0;    /* size changed */
  memcpy(mr->b, (const void*)p, fsz);
  if (memcmp(mr->a, mr->b, fsz) != 0) return 0;             /* still writing */

  /* Refresh SPS/PPS if this frame carries them. */
  size_t sl = 0, pl = 0;
  const unsigned char* sps = find_last_nal(mr->b, fsz, 7, &sl);
  const unsigned char* pps = find_last_nal(mr->b, fsz, 8, &pl);
  if (sps && sl && sl <= sizeof(mr->sps)) {
    memcpy(mr->sps, sps, sl);
    mr->sps_len = (int)sl;
  }
  if (pps && pl && pl <= sizeof(mr->pps)) {
    memcpy(mr->pps, pps, pl);
    mr->pps_len = (int)pl;
  }

  mr->last_off = off;
  *out = mr->b;
  *size = (int)fsz;
  return 1;
}

const uint8_t* mr_sps(MemfdReader* mr, int* size) {
  *size = mr->sps_len;
  return mr->sps_len ? mr->sps : NULL;
}

const uint8_t* mr_pps(MemfdReader* mr, int* size) {
  *size = mr->pps_len;
  return mr->pps_len ? mr->pps : NULL;
}

void mr_close(MemfdReader* mr) {
  if (!mr) return;
  if (mr->buf && mr->buf != MAP_FAILED) munmap((void*)mr->buf, mr->sz);
  free(mr->a);
  free(mr->b);
  free(mr);
}
