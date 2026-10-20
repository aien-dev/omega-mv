/* Hosted test: src/omega_gpu_encoding.h equals the physics originals (L202, cut A).
 * omega_gpu_mthd vs nvrm_mthd (physics nvrm/nvrm.h): every subchannel 0..7, every dword method 0..0xfffc, every
 * count 0..8191 (the full legal space, 2^30 headers), plus 2^24 pseudo-random out-of-range arguments.
 * omega_gpu_gp_entry vs nvrm_gp_entry (physics nvrm/nvrm.c): every nwords 0..2^22 at 16 edge addresses, plus
 * 2^24 pseudo-random (va, nwords) pairs. Any difference prints the first one and fails. */
#include "omega_gpu_encoding.h"
#include "nvrm.h"

#include <stdio.h>

/* Called through volatile pointers so the compiler cannot prove the two inline bodies equal and drop the compare. */
#ifdef OMEGA_ENC_MUTANT /* negative control: one header and one entry wrong, the test must fail */
static uint32_t mut_mthd(uint32_t s, uint32_t m, uint32_t c) { return omega_gpu_mthd(s, m, c) ^ (s == 5 && m == 0x3a8 && c == 4097); }
static uint64_t mut_gp(uint64_t va, uint32_t n) { return omega_gpu_gp_entry(va, n) ^ (uint64_t)(n == 1234567); }
static uint32_t (*volatile f_om)(uint32_t, uint32_t, uint32_t) = mut_mthd;
static uint64_t (*volatile g_om)(uint64_t, uint32_t) = mut_gp;
#else
static uint32_t (*volatile f_om)(uint32_t, uint32_t, uint32_t) = omega_gpu_mthd;
static uint64_t (*volatile g_om)(uint64_t, uint32_t) = omega_gpu_gp_entry;
#endif
static uint32_t (*volatile f_nv)(uint32_t, uint32_t, uint32_t) = nvrm_mthd;
static uint64_t (*volatile g_nv)(uint64_t, uint32_t) = nvrm_gp_entry;

static uint64_t g_x = 0x9e3779b97f4a7c15ull;
static uint64_t rnd(void) { g_x ^= g_x << 13; g_x ^= g_x >> 7; g_x ^= g_x << 17; return g_x; }

int main(void) {
    unsigned long long checked = 0, bad = 0;
    for (uint32_t subc = 0; subc < 8; subc++)
        for (uint32_t m = 0; m <= 0xfffcu; m += 4)
            for (uint32_t c = 0; c < 8192; c++) {
                checked++;
                if (f_om(subc, m, c) != f_nv(subc, m, c) && bad++ == 0)
                    fprintf(stderr, "FAIL mthd(%u, 0x%x, %u)\n", subc, m, c);
            }
    for (int i = 0; i < (1 << 24); i++) {
        uint32_t s = (uint32_t)rnd(), m = (uint32_t)rnd(), c = (uint32_t)rnd();
        checked++;
        if (f_om(s, m, c) != f_nv(s, m, c) && bad++ == 0)
            fprintf(stderr, "FAIL mthd(0x%x, 0x%x, 0x%x)\n", s, m, c);
    }
    printf("mthd: %llu headers compared, %llu differ\n", checked, bad);

    unsigned long long gchecked = 0, gbad = 0;
    const uint64_t edges[16] = { 0, 1, 2, 3, 4, 0xfff, 0x1000, 0x7fffffffffull, 0x8000000000ull, 0xffffffffffull,
                                 0x10000000000ull, 0x1ffffffffffull, 0x20000000000ull, 0x3ffffffffffull,
                                 0xfffffffffffffffcull, 0xffffffffffffffffull };
    for (int e = 0; e < 16; e++)
        for (uint32_t n = 0; n <= (1u << 22); n++) {
            gchecked++;
            if (g_om(edges[e], n) != g_nv(edges[e], n) && gbad++ == 0)
                fprintf(stderr, "FAIL gp_entry(0x%llx, %u)\n", (unsigned long long)edges[e], n);
        }
    for (int i = 0; i < (1 << 24); i++) {
        uint64_t va = rnd();
        uint32_t n = (uint32_t)rnd();
        gchecked++;
        if (g_om(va, n) != g_nv(va, n) && gbad++ == 0)
            fprintf(stderr, "FAIL gp_entry(0x%llx, %u)\n", (unsigned long long)va, n);
    }
    printf("gp_entry: %llu entries compared, %llu differ\n", gchecked, gbad);
    if (bad || gbad) { printf("test-gpu-encoding: FAIL\n"); return 1; }
    printf("test-gpu-encoding: PASS\n");
    return 0;
}
