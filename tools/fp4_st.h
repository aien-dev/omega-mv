/* fp4_st.h: minimal safetensors reader for the FP4 converter and study (OWN-30B cut L3). Header-only, no allocations beyond the index.
 * Maps every shard of a checkpoint directory read-only and looks tensors up by name. dtypes: BF16, F8_E4M3, F32 (others stored as-is). */
#ifndef FP4_ST_H
#define FP4_ST_H
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct { char name[120]; char dtype[12]; uint32_t shape[3]; int ndim; const uint8_t *data; uint64_t bytes; } StTensor;
typedef struct { StTensor *t; size_t n, cap; } StIndex;

static int st_cmp(const void *a, const void *b) { return strcmp(((const StTensor *)a)->name, ((const StTensor *)b)->name); }

static int st_add_shard(StIndex *ix, const char *path) {
    int fd = open(path, O_RDONLY); if (fd < 0) { perror(path); return -1; }
    struct stat sb; fstat(fd, &sb);
    uint8_t *m = mmap(NULL, (size_t)sb.st_size, PROT_READ, MAP_SHARED, fd, 0); close(fd);
    if (m == MAP_FAILED) { perror("mmap"); return -1; }
    uint64_t hl; memcpy(&hl, m, 8);
    const char *h = (const char *)m + 8, *end = h + hl; const uint8_t *base = m + 8 + hl;
    const char *p = h;
    while (p < end) {
        const char *q = memchr(p, '"', (size_t)(end - p)); if (!q) break;
        const char *q2 = memchr(q + 1, '"', (size_t)(end - q - 1)); if (!q2) break;
        size_t nl = (size_t)(q2 - q - 1);
        const char *colon = q2 + 1; if (*colon != ':' || colon[1] != '{') { p = q2 + 1; continue; }
        const char *close = strchr(colon, '}'); if (!close) break;
        if (nl < sizeof(ix->t[0].name) && !(nl == 12 && memcmp(q + 1, "__metadata__", 12) == 0)) {
            if (ix->n == ix->cap) { ix->cap = ix->cap ? ix->cap * 2 : 4096; ix->t = realloc(ix->t, ix->cap * sizeof *ix->t); }
            StTensor *t = &ix->t[ix->n++]; memset(t, 0, sizeof *t);
            memcpy(t->name, q + 1, nl);
            const char *d = strstr(colon, "\"dtype\":\""); d += 9; size_t dl = (size_t)(strchr(d, '"') - d); memcpy(t->dtype, d, dl < 11 ? dl : 11);
            const char *s = strstr(colon, "\"shape\":["); s += 9; t->ndim = 0;
            while (*s != ']') { if (*s >= '0' && *s <= '9') { t->shape[t->ndim++] = (uint32_t)strtoul(s, (char **)&s, 10); } else s++; }
            const char *o = strstr(colon, "\"data_offsets\":["); o += 16; uint64_t a = strtoull(o, (char **)&o, 10); o++; uint64_t b = strtoull(o, (char **)&o, 10);
            t->data = base + a; t->bytes = b - a;
        }
        p = close + 1;
    }
    return 0;
}
static int st_open_dir(StIndex *ix, const char *dir) {
    char path[512];
    for (int i = 1; i <= 4; i++) { snprintf(path, sizeof path, "%s/model-%05d-of-00004.safetensors", dir, i); if (st_add_shard(ix, path)) return -1; }
    qsort(ix->t, ix->n, sizeof *ix->t, st_cmp);
    return 0;
}
static const StTensor *st_get(const StIndex *ix, const char *name) {
    StTensor key; memset(&key, 0, sizeof key); snprintf(key.name, sizeof key.name, "%s", name);
    return bsearch(&key, ix->t, ix->n, sizeof *ix->t, st_cmp);
}
static inline float st_bf16(uint16_t v) { uint32_t u = (uint32_t)v << 16; float f; memcpy(&f, &u, 4); return f; }
/* float8_e4m3fn -> float (bias 7, 0x7F/0xFF NaN, no inf) */
static inline float st_e4m3(uint8_t b) {
    int s = b >> 7, e = (b >> 3) & 15, m = b & 7; float v;
    if (e == 0) v = (float)m * (1.0f / 512.0f); else v = (1.0f + (float)m / 8.0f) * (e >= 7 ? (float)(1u << (e - 7)) : 1.0f / (float)(1u << (7 - e)));
    return s ? -v : v;
}
#endif
