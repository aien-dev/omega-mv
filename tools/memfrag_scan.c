/* memfrag_scan.c -- which pages keep 2 MiB blocks from forming (omega#528).
 *
 * Read-only. Walks /proc/kpageflags one pageblock (512 x 4 KiB = 2 MiB) at a
 * time and sorts every page into a class. A pageblock can only become a free
 * 2 MiB block (by freeing or by compaction) when every page in it is free or
 * movable; one page of any other class pins the whole block. Optionally tags
 * the physical pages behind one process's driver mappings (default: lines of
 * /proc/<pid>/maps naming "nvidiactl", which on GB10 are the GPU's system
 * pages mapped to the CPU) so they can be counted as their own class.
 *
 *   memfrag_scan [--kpageflags F] [--pid P]... [--maps F --pagemap F]... [--match S]
 *
 * Root (or CAP_SYS_ADMIN) is needed for the real /proc/kpageflags and for
 * physical frame numbers in /proc/<pid>/pagemap. Output is key=value lines.
 *
 * Page classes, from the kernel's page flag bits
 * (include/uapi/linux/kernel-page-flags.h):
 *   free     BUDDY head, plus the flag-less pages after it up to the largest
 *            aligned power-of-two block that starts at the head (the kernel
 *            marks only the head page of a free block)
 *   gpu      tagged from the process's driver mappings
 *   slab     SLAB
 *   pgtable  PGTABLE
 *   reserved RESERVED (bit 32): set aside at boot, never freed
 *   movable  LRU or ANON (page cache and user memory the kernel can migrate)
 *   other    anything else that is present: driver, vmalloc, kernel stacks,
 *            pages without flags that no free head covers
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MF_BLOCK_PAGES 512u
#define MF_MAX_ORDER   13u

#define KPF_SLAB          7
#define KPF_BUDDY        10
#define KPF_LRU           5
#define KPF_ANON         12
#define KPF_COMPOUND_HEAD 15
#define KPF_COMPOUND_TAIL 16
#define KPF_NOPAGE       20
#define KPF_PGTABLE      26
#define KPF_RESERVED     32 /* kernel-hacking bit: memblock reserved at boot */
#define KPF_BIT(b) (1ull << (b))

enum mf_class { MF_HOLE, MF_FREE, MF_GPU, MF_SLAB, MF_PGTABLE, MF_RESERVED, MF_OTHER, MF_MOVABLE, MF_NCLASS };
static const char *const mf_class_name[MF_NCLASS] = {
    "hole", "free", "gpu", "slab", "pgtable", "reserved", "other", "movable"
};

typedef struct {
    uint8_t *bits;
    uint64_t nbits;
} mf_bitmap;

static int mf_bit_get(const mf_bitmap *b, uint64_t i) {
    return b && i < b->nbits && (b->bits[i >> 3] >> (i & 7)) & 1;
}

static int mf_bit_set(mf_bitmap *b, uint64_t i) {
    if (i >= b->nbits) {
        uint64_t n = b->nbits ? b->nbits : 1u << 20;
        while (n <= i) n <<= 1;
        uint8_t *p = realloc(b->bits, n >> 3);
        if (!p) return -1;
        memset(p + (b->nbits >> 3), 0, (n - b->nbits) >> 3);
        b->bits = p;
        b->nbits = n;
    }
    b->bits[i >> 3] |= (uint8_t)(1u << (i & 7));
    return 0;
}

typedef struct {
    uint64_t pages[MF_NCLASS];        /* pages per class */
    uint64_t blocks;                  /* pageblocks with any present page */
    uint64_t blocks_free;             /* every present page free */
    uint64_t blocks_compactable;      /* free or movable only: compaction can empty it */
    uint64_t blocks_with[MF_NCLASS];  /* blocks holding >= 1 page of the class */
    uint64_t blocks_gpu_only_pin;     /* gpu pages are the only pinning pages */
    uint64_t blocks_gpu_full;         /* all 512 pages gpu */
    uint64_t gpu_compound;            /* gpu pages that are part of a compound page */
    uint64_t pin_combo[16];           /* blocks by which pinning classes they hold:
                                         bit 0 gpu, 1 slab, 2 pgtable, 3 reserved+other */
    uint64_t other_flag[32];          /* distinct flag words seen on "other" pages */
    uint64_t other_n[32];             /* ... and how many pages carried each */
    uint64_t other_spill;             /* "other" pages past the 32 distinct words */
} mf_counts;

/* Free run state carried across pages: pages below free_end that have no
 * flags belong to the free block whose head was seen last. */
typedef struct {
    uint64_t free_end;
} mf_walk;

static unsigned mf_aligned_order(uint64_t pfn) {
    unsigned o = 0;
    while (o < MF_MAX_ORDER && !(pfn & ((2ull << o) - 1))) o++;
    return o;
}

static enum mf_class mf_classify(mf_walk *w, uint64_t pfn, uint64_t flags, int is_gpu) {
    if (flags & KPF_BIT(KPF_NOPAGE)) return MF_HOLE;
#ifdef MEMFRAG_MUTANT_NO_GPU_TAG
    is_gpu = 0; /* mutant: the driver mappings are not counted */
#endif
    if (is_gpu) {
        w->free_end = 0;
        return MF_GPU;
    }
    if (flags & KPF_BIT(KPF_BUDDY)) {
        w->free_end = pfn + (1ull << mf_aligned_order(pfn));
        return MF_FREE;
    }
#ifndef MEMFRAG_MUTANT_NO_FREE_TAIL /* mutant: only the head page of a free block counts */
    if (flags == 0 && pfn < w->free_end) return MF_FREE;
#endif
    w->free_end = 0;
    if (flags & KPF_BIT(KPF_SLAB)) return MF_SLAB;
    if (flags & KPF_BIT(KPF_PGTABLE)) return MF_PGTABLE;
    if (flags & KPF_BIT(KPF_RESERVED)) return MF_RESERVED;
    if (flags & (KPF_BIT(KPF_LRU) | KPF_BIT(KPF_ANON))) return MF_MOVABLE;
    return MF_OTHER;
}

static void mf_note_other(mf_counts *c, uint64_t flags) {
    for (unsigned j = 0; j < 32; j++) {
        if (c->other_n[j] && c->other_flag[j] == flags) {
            c->other_n[j]++;
            return;
        }
        if (!c->other_n[j]) {
            c->other_flag[j] = flags;
            c->other_n[j] = 1;
            return;
        }
    }
    c->other_spill++;
}

/* One pageblock: flags[n] for pfn base..base+n-1 (n <= MF_BLOCK_PAGES). */
static void mf_block(mf_counts *c, mf_walk *w, uint64_t base, const uint64_t *flags,
                     unsigned n, const mf_bitmap *gpu) {
    uint64_t per[MF_NCLASS] = {0};
    for (unsigned i = 0; i < n; i++) {
        int g = mf_bit_get(gpu, base + i);
        enum mf_class k = mf_classify(w, base + i, flags[i], g);
        per[k]++;
        if (k == MF_GPU && flags[i] & (KPF_BIT(KPF_COMPOUND_HEAD) | KPF_BIT(KPF_COMPOUND_TAIL)))
            c->gpu_compound++;
        if (k == MF_OTHER) mf_note_other(c, flags[i]);
    }
    uint64_t present = n - per[MF_HOLE];
    for (int k = 0; k < MF_NCLASS; k++) {
        c->pages[k] += per[k];
        if (per[k]) c->blocks_with[k]++;
    }
    if (!present) return;
    c->blocks++;
    uint64_t pinning = per[MF_GPU] + per[MF_SLAB] + per[MF_PGTABLE] + per[MF_RESERVED] + per[MF_OTHER];
    if (per[MF_FREE] == present) c->blocks_free++;
    if (!pinning) c->blocks_compactable++;
    c->pin_combo[(per[MF_GPU] > 0) | (per[MF_SLAB] > 0) << 1 | (per[MF_PGTABLE] > 0) << 2 |
                 (per[MF_RESERVED] + per[MF_OTHER] > 0) << 3]++;
    if (per[MF_GPU] && per[MF_GPU] == pinning) c->blocks_gpu_only_pin++;
    if (per[MF_GPU] == MF_BLOCK_PAGES) c->blocks_gpu_full++;
}

/* Walk a kpageflags-format stream (one little-endian u64 per pfn from 0). */
static int mf_scan_fd(int fd, const mf_bitmap *gpu, mf_counts *c) {
    uint64_t buf[MF_BLOCK_PAGES];
    mf_walk w = {0};
    uint64_t base = 0;
    for (;;) {
        size_t got = 0;
        while (got < sizeof buf) {
            ssize_t r = read(fd, (char *)buf + got, sizeof buf - got);
            if (r < 0) {
                if (errno == EINTR) continue;
                return -1;
            }
            if (r == 0) break;
            got += (size_t)r;
        }
        unsigned n = (unsigned)(got / sizeof buf[0]);
        if (n) mf_block(c, &w, base, buf, n, gpu);
        if (got < sizeof buf) return 0;
        base += MF_BLOCK_PAGES;
    }
}

/* Tag the pfns behind every mapping in `maps` whose line contains `match`.
 * Returns the number of present pages tagged, -1 on error. Pagemap entry:
 * bit 63 present, bits 0..54 pfn (zero without CAP_SYS_ADMIN). */
static long mf_tag_mappings(const char *maps, const char *pagemap, const char *match,
                            mf_bitmap *gpu, uint64_t *zero_pfn) {
    FILE *m = fopen(maps, "r");
    if (!m) return -1;
    int pm = open(pagemap, O_RDONLY);
    if (pm < 0) {
        fclose(m);
        return -1;
    }
    char line[4096];
    long tagged = 0;
    *zero_pfn = 0;
    while (fgets(line, sizeof line, m)) {
        if (!strstr(line, match)) continue;
        unsigned long lo, hi;
        if (sscanf(line, "%lx-%lx", &lo, &hi) != 2 || hi <= lo) continue;
        for (uint64_t va = lo; va < hi; va += 4096 * 512ull) {
            uint64_t ent[512];
            uint64_t want = (hi - va) / 4096;
            if (want > 512) want = 512;
            ssize_t r = pread(pm, ent, want * 8, (off_t)(va / 4096 * 8));
            if (r != (ssize_t)(want * 8)) {
                tagged = -1;
                goto out;
            }
            for (uint64_t i = 0; i < want; i++) {
                if (!(ent[i] >> 63)) continue;
                uint64_t pfn = ent[i] & ((1ull << 55) - 1);
                if (!pfn) {
                    (*zero_pfn)++;
                    continue;
                }
                if (mf_bit_set(gpu, pfn)) {
                    tagged = -1;
                    goto out;
                }
                tagged++;
            }
        }
    }
out:
    close(pm);
    fclose(m);
    return tagged;
}

/* Lengths of physically contiguous runs among the tagged pfns, as a log2
 * histogram: hist[k] counts runs of length in [2^k, 2^(k+1)). */
static void mf_run_hist(const mf_bitmap *gpu, uint64_t hist[MF_MAX_ORDER + 2]) {
    memset(hist, 0, sizeof(uint64_t) * (MF_MAX_ORDER + 2));
    uint64_t run = 0;
    for (uint64_t i = 0; i <= gpu->nbits; i++) {
        if (i < gpu->nbits && mf_bit_get(gpu, i)) {
            run++;
            continue;
        }
        if (!run) continue;
        unsigned k = 0;
        while (k < MF_MAX_ORDER + 1 && (run >> (k + 1))) k++;
        hist[k]++;
        run = 0;
    }
}

static void mf_print(FILE *o, const mf_counts *c, long tagged, uint64_t zero_pfn,
                     const mf_bitmap *gpu) {
    for (int k = 1; k < MF_NCLASS; k++)
        fprintf(o, "pages_%s=%llu\n", mf_class_name[k], (unsigned long long)c->pages[k]);
    fprintf(o, "blocks=%llu\n", (unsigned long long)c->blocks);
    fprintf(o, "blocks_free=%llu\n", (unsigned long long)c->blocks_free);
    fprintf(o, "blocks_compactable=%llu\n", (unsigned long long)c->blocks_compactable);
    fprintf(o, "blocks_pinned=%llu\n", (unsigned long long)(c->blocks - c->blocks_compactable));
    for (int k = MF_GPU; k < MF_NCLASS; k++)
        fprintf(o, "blocks_with_%s=%llu\n", mf_class_name[k], (unsigned long long)c->blocks_with[k]);
    fprintf(o, "blocks_gpu_only_pin=%llu\n", (unsigned long long)c->blocks_gpu_only_pin);
    fprintf(o, "blocks_gpu_full=%llu\n", (unsigned long long)c->blocks_gpu_full);
    for (unsigned b = 1; b < 16; b++)
        fprintf(o, "pinned_by_%s%s%s%s=%llu\n", b & 1 ? "G" : "-", b & 2 ? "S" : "-", b & 4 ? "P" : "-",
                b & 8 ? "O" : "-", (unsigned long long)c->pin_combo[b]);
    for (unsigned j = 0; j < 32 && c->other_n[j]; j++)
        if (c->other_n[j] * 100 >= c->pages[MF_OTHER])
            fprintf(o, "other_flags_0x%llx=%llu\n", (unsigned long long)c->other_flag[j],
                    (unsigned long long)c->other_n[j]);
    fprintf(o, "other_spill=%llu\n", (unsigned long long)c->other_spill);
    if (tagged < 0) return;
    fprintf(o, "gpu_tagged=%ld\n", tagged);
    fprintf(o, "gpu_pfn_hidden=%llu\n", (unsigned long long)zero_pfn);
    fprintf(o, "gpu_compound=%llu\n", (unsigned long long)c->gpu_compound);
    uint64_t hist[MF_MAX_ORDER + 2];
    mf_run_hist(gpu, hist);
    for (unsigned k = 0; k < MF_MAX_ORDER + 2; k++)
        if (hist[k]) fprintf(o, "gpu_runs_order%u=%llu\n", k, (unsigned long long)hist[k]);
}

#ifndef MEMFRAG_SCAN_NO_MAIN
int main(int argc, char **argv) {
    const char *kpf = "/proc/kpageflags", *match = "nvidiactl";
    char *maps[16], *pagemap[16];
    int nsrc = 0, nmaps = 0, npm = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--kpageflags") && i + 1 < argc) kpf = argv[++i];
        else if (!strcmp(argv[i], "--match") && i + 1 < argc) match = argv[++i];
        else if (!strcmp(argv[i], "--maps") && i + 1 < argc && nmaps < 16) maps[nmaps++] = strdup(argv[++i]);
        else if (!strcmp(argv[i], "--pagemap") && i + 1 < argc && npm < 16) pagemap[npm++] = strdup(argv[++i]);
        else if (!strcmp(argv[i], "--pid") && i + 1 < argc && nmaps < 16 && npm < 16) {
            long pid = strtol(argv[++i], NULL, 10);
            char b[64];
            snprintf(b, sizeof b, "/proc/%ld/maps", pid);
            maps[nmaps++] = strdup(b);
            snprintf(b, sizeof b, "/proc/%ld/pagemap", pid);
            pagemap[npm++] = strdup(b);
        } else {
            fprintf(stderr, "usage: %s [--kpageflags F] [--pid P]... [--maps F --pagemap F]... [--match S]\n",
                    argv[0]);
            return 2;
        }
    }
    if (nmaps != npm) {
        fprintf(stderr, "memfrag_scan: --maps and --pagemap go together\n");
        return 2;
    }
    nsrc = nmaps;
    mf_bitmap gpu = {0};
    long tagged = nsrc ? 0 : -1;
    uint64_t zero_pfn = 0;
    /* Tag first, then scan: the two reads are not atomic, so pages that move
     * or are freed in between are counted under their new class. */
    for (int s = 0; s < nsrc; s++) {
        uint64_t z = 0;
        long t = mf_tag_mappings(maps[s], pagemap[s], match, &gpu, &z);
        if (t < 0) {
            fprintf(stderr, "memfrag_scan: cannot read %s / %s: %s\n", maps[s], pagemap[s], strerror(errno));
            return 1;
        }
        tagged += t;
        zero_pfn += z;
    }
    if (nsrc && tagged == 0 && zero_pfn)
        fprintf(stderr, "memfrag_scan: pagemap hides frame numbers (needs CAP_SYS_ADMIN)\n");
    int fd = open(kpf, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "memfrag_scan: open %s: %s\n", kpf, strerror(errno));
        return 1;
    }
    mf_counts c = {0};
    if (mf_scan_fd(fd, &gpu, &c)) {
        fprintf(stderr, "memfrag_scan: read %s: %s\n", kpf, strerror(errno));
        close(fd);
        return 1;
    }
    close(fd);
    mf_print(stdout, &c, tagged, zero_pfn, &gpu);
    free(gpu.bits);
    for (int s = 0; s < nsrc; s++) {
        free(maps[s]);
        free(pagemap[s]);
    }
    return 0;
}
#endif
