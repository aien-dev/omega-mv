/* Host memory fragmentation scanner (tools/memfrag_scan.c, omega#528). Host only: synthetic
 * kpageflags/maps/pagemap fixtures, no /proc, no root, no device.
 *
 * The central check is the omega#528 claim in miniature: the same 512 GPU pages pin 16 pageblocks
 * when the driver hands them out as 64 KiB pieces spread over the blocks (what the live 27B server
 * showed), and one pageblock when they come as one 2 MiB piece. Negative control: no GPU pages
 * and only movable memory pins nothing. Mutants NO_GPU_TAG and NO_FREE_TAIL must fail. */
#define MEMFRAG_SCAN_NO_MAIN
#include "../tools/memfrag_scan.c"

#include <sys/stat.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define NB 16u                                   /* pageblocks per fixture */
#define NP (NB * MF_BLOCK_PAGES)
#define F_LRU  KPF_BIT(KPF_LRU)
#define F_MMAP KPF_BIT(11)
#define F_HEAD (KPF_BIT(KPF_COMPOUND_HEAD) | F_MMAP)
#define F_TAIL (KPF_BIT(KPF_COMPOUND_TAIL) | F_MMAP)

static uint64_t flags[NP];
static mf_bitmap gpu;

static void reset(uint64_t fill) {
    for (unsigned i = 0; i < NP; i++) flags[i] = fill;
    free(gpu.bits);
    gpu.bits = NULL;
    gpu.nbits = 0;
}

/* One driver chunk of 2^order pages at pfn: compound head + tails, mapped, tagged. */
static void gpu_chunk(uint64_t pfn, unsigned order) {
    for (uint64_t i = 0; i < (1ull << order); i++) {
        flags[pfn + i] = i ? F_TAIL : F_HEAD;
        if (mf_bit_set(&gpu, pfn + i)) { printf("FAIL: bitmap alloc\n"); exit(1); }
    }
}

static mf_counts scan(void) {
    mf_counts c = {0};
    mf_walk w = {0};
    for (unsigned b = 0; b < NB; b++) mf_block(&c, &w, (uint64_t)b * MF_BLOCK_PAGES, flags + b * MF_BLOCK_PAGES, MF_BLOCK_PAGES, &gpu);
    return c;
}

static void write_file(const char *path, const void *p, size_t n) {
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(p, 1, n, f) != n || fclose(f)) { printf("FAIL: write %s\n", path); exit(1); }
}

int main(void) {
    char dir[] = "/tmp/memfrag-test-XXXXXX";
    if (!mkdtemp(dir)) { printf("FAIL: mkdtemp\n"); return 1; }

    /* 1. scattered 64 KiB pieces: 32 order-4 chunks, two per block, rest of each block movable */
    reset(F_LRU);
    for (unsigned b = 0; b < NB; b++) {
        gpu_chunk((uint64_t)b * MF_BLOCK_PAGES + 64, 4);
        gpu_chunk((uint64_t)b * MF_BLOCK_PAGES + 320, 4);
    }
    mf_counts s = scan();
    CHECK(s.pages[MF_GPU] == 512, "scattered: 512 gpu pages (got %llu)", (unsigned long long)s.pages[MF_GPU]);
    CHECK(s.blocks_with[MF_GPU] == NB, "scattered: gpu in all 16 blocks (got %llu)", (unsigned long long)s.blocks_with[MF_GPU]);
    CHECK(s.blocks_compactable == 0, "scattered: no block compactable (got %llu)", (unsigned long long)s.blocks_compactable);
    CHECK(s.blocks_gpu_only_pin == NB && s.pin_combo[1] == NB, "scattered: gpu is the only pinner of 16 blocks (got %llu)", (unsigned long long)s.blocks_gpu_only_pin);
    CHECK(s.gpu_compound == 512, "scattered: every gpu page compound (got %llu)", (unsigned long long)s.gpu_compound);
    uint64_t hist[MF_MAX_ORDER + 2];
    mf_run_hist(&gpu, hist);
    CHECK(hist[4] == 32 && hist[9] == 0, "scattered: 32 runs of 16 pages (order4=%llu order9=%llu)", (unsigned long long)hist[4], (unsigned long long)hist[9]);
    char *out = NULL;
    size_t outn = 0;
    FILE *mo = open_memstream(&out, &outn);
    mf_print(mo, &s, 512, 0, &gpu);
    fclose(mo);
    CHECK(strstr(out, "\nblocks_pinned=16\n") && strstr(out, "\npinned_by_G---=16\n") && strstr(out, "\ngpu_runs_order4=32\n"),
          "scattered: report lines (got:\n%s)", out);
    free(out);

    /* 2. the same 512 pages as one 2 MiB piece: one block pinned, fifteen compactable */
    reset(F_LRU);
    gpu_chunk(0, 9);
    mf_counts p = scan();
    CHECK(p.pages[MF_GPU] == 512, "packed: 512 gpu pages (got %llu)", (unsigned long long)p.pages[MF_GPU]);
    CHECK(p.blocks_with[MF_GPU] == 1 && p.blocks_gpu_full == 1, "packed: one full gpu block (with=%llu full=%llu)", (unsigned long long)p.blocks_with[MF_GPU], (unsigned long long)p.blocks_gpu_full);
    CHECK(p.blocks_compactable == NB - 1, "packed: 15 blocks compactable (got %llu)", (unsigned long long)p.blocks_compactable);
    mf_run_hist(&gpu, hist);
    CHECK(hist[9] == 1 && hist[4] == 0, "packed: one run of 512 pages (order9=%llu)", (unsigned long long)hist[9]);
    CHECK(s.blocks - s.blocks_compactable == NB * (p.blocks - p.blocks_compactable), "scattered pins 16x the blocks of packed");

    /* 3. negative control: movable only, no gpu tags: nothing pinned, every gpu count zero */
    reset(F_LRU);
    mf_counts n = scan();
    CHECK(n.blocks == NB && n.blocks_compactable == NB, "control: all 16 blocks compactable (got %llu)", (unsigned long long)n.blocks_compactable);
    CHECK(n.pages[MF_GPU] == 0 && n.blocks_with[MF_GPU] == 0 && n.gpu_compound == 0, "control: no gpu pages");
    for (unsigned b = 1; b < 16; b++) CHECK(n.pin_combo[b] == 0, "control: pin combo %u empty", b);

    /* 4. classes: one slab page, one page-table page, one reserved page, holes */
    reset(F_LRU);
    flags[1 * MF_BLOCK_PAGES + 7] = KPF_BIT(KPF_SLAB);
    flags[2 * MF_BLOCK_PAGES + 9] = KPF_BIT(KPF_PGTABLE);
    flags[3 * MF_BLOCK_PAGES + 3] = KPF_BIT(KPF_RESERVED);
    for (unsigned i = 0; i < MF_BLOCK_PAGES; i++) flags[15 * MF_BLOCK_PAGES + i] = KPF_BIT(KPF_NOPAGE);
    mf_counts k = scan();
    CHECK(k.blocks == NB - 1, "classes: the all-hole block is not a block (got %llu)", (unsigned long long)k.blocks);
    CHECK(k.pin_combo[2] == 1 && k.pin_combo[4] == 1 && k.pin_combo[8] == 1, "classes: one block each pinned by slab, pgtable, reserved");
    CHECK(k.blocks_compactable == NB - 4, "classes: 12 compactable (got %llu)", (unsigned long long)k.blocks_compactable);

    /* 5. free blocks: the kernel flags only the head of a free block */
    reset(F_LRU);
    flags[0] = KPF_BIT(KPF_BUDDY);                                 /* block 0: one free 2 MiB block */
    for (unsigned i = 1; i < MF_BLOCK_PAGES; i++) flags[i] = 0;
    flags[MF_BLOCK_PAGES] = KPF_BIT(KPF_BUDDY);                    /* block 1: free 64 KiB at its start */
    for (unsigned i = 1; i < 16; i++) flags[MF_BLOCK_PAGES + i] = 0;
    flags[MF_BLOCK_PAGES + 16] = KPF_BIT(KPF_SLAB);
    flags[MF_BLOCK_PAGES + 17] = 0;                                /* flag-less, no free head: other */
    mf_counts f = scan();
    CHECK(f.blocks_free == 1, "free: one whole free block (got %llu)", (unsigned long long)f.blocks_free);
    CHECK(f.pages[MF_FREE] == 512 + 16, "free: 528 free pages (got %llu)", (unsigned long long)f.pages[MF_FREE]);
    CHECK(f.pages[MF_OTHER] == 1, "free: one unexplained page (got %llu)", (unsigned long long)f.pages[MF_OTHER]);
    CHECK(f.blocks_compactable == NB - 1, "free: free block counts as compactable (got %llu)", (unsigned long long)f.blocks_compactable);

    /* 6. end to end through files: kpageflags stream with a short last block, maps + pagemap tagging */
    reset(F_LRU);
    for (unsigned i = 0; i < 64; i++) flags[MF_BLOCK_PAGES + i] = i ? F_TAIL : F_HEAD;  /* the driver pages */
    char kpf[64], maps[64], pm[64];
    snprintf(kpf, sizeof kpf, "%s/kpageflags", dir);
    write_file(kpf, flags, (NP - 100) * sizeof flags[0]);
    snprintf(maps, sizeof maps, "%s/maps", dir);
    const char *m = "10000000-10040000 rw-s 00000000 00:06 7 /dev/nvidiactl\n"
                    "20000000-20001000 rw-p 00000000 00:00 0 [heap]\n";
    write_file(maps, m, strlen(m));
    snprintf(pm, sizeof pm, "%s/pagemap", dir);
    static uint64_t ent[0x20001];
    for (unsigned i = 0; i < 64; i++) ent[0x10000 + i] = 1ull << 63 | (MF_BLOCK_PAGES + i);
    ent[0x20000] = 1ull << 63 | 5;                                 /* heap page: must not be tagged */
    write_file(pm, ent, sizeof ent);
    reset(F_LRU);
    uint64_t hidden = 0;
    long t = mf_tag_mappings(maps, pm, "nvidiactl", &gpu, &hidden);
    CHECK(t == 64 && hidden == 0, "files: 64 pages tagged (got %ld)", t);
    CHECK(!mf_bit_get(&gpu, 5), "files: the heap page is not tagged");
    int fd = open(kpf, O_RDONLY);
    mf_counts e = {0};
    CHECK(fd >= 0 && mf_scan_fd(fd, &gpu, &e) == 0, "files: scan reads the stream");
    if (fd >= 0) close(fd);
    CHECK(e.blocks == NB, "files: short last block still counted (got %llu)", (unsigned long long)e.blocks);
    CHECK(e.pages[MF_GPU] == 64 && e.blocks_with[MF_GPU] == 1, "files: 64 gpu pages in one block (got %llu)", (unsigned long long)e.pages[MF_GPU]);
    ent[0x10000] = 1ull << 63;                                     /* unprivileged pagemap: pfn 0 */
    write_file(pm, ent, sizeof ent);
    reset(F_LRU);
    t = mf_tag_mappings(maps, pm, "nvidiactl", &gpu, &hidden);
    CHECK(t == 63 && hidden == 1, "files: a hidden pfn is reported, not tagged (tagged %ld hidden %llu)", t, (unsigned long long)hidden);

    unlink(kpf);
    unlink(maps);
    unlink(pm);
    rmdir(dir);
    free(gpu.bits);
    if (fails) { printf("MEMFRAG_SCAN FAIL (%d)\n", fails); return 1; }
    printf("MEMFRAG_SCAN PASS\n");
    return 0;
}
