/* l2g_stamp_analyze: offline readout of the W-L2g resident stage camera (gpu_resident_chain_test --stamps-raw FILE).
 * Build: cc -O2 -o build/l2g_stamp_analyze tools/l2g_stamp_analyze.c      Run: build/l2g_stamp_analyze FILE [warps]
 *
 * File: blocks of { u32 magic "STMP", N, grid, rep } then N x grid records of 64 u32 (see omega_blackwell_codegen_resident.h,
 * spec.stamps). Real chain shapes alternate (k 2048, n 4096) / (k 4096, n 2048) as in tests/gpu_resident_chain_test.c.
 *
 * Per stage it rebuilds the bytes-done curve: each warp's rows finish at its first-row stamp, then evenly up to its last-row
 * stamp (the camera keeps first and last only: INFERRED spacing for the middle rows). From the curve:
 *   t10 / t50 / t90 / t100   time (from the stage's earliest CTA start) at which that share of the stage's weight bytes is done
 *   mid_GBps                 rate between 10 % and 90 % (the steady streaming rate)
 *   ramp_us = t10 - 0.1 * B / mid    time lost before the steady rate (start-up)
 *   tail_us = t100 - t90 - 0.1 * B / mid   time lost after it (tail: few warps still working)
 *   steady_us = 0.8 * B / mid, and steady_excess_us = steady part at mid vs at 217 GB/s
 * Also: per-SM spread of CTA done times (is one SM slow?), and warps with 1 vs 2+ rows.
 * Averages over every block with the same grid (all reps, all stages of each shape). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define R 64u
#define MAXW 65536u
typedef struct { double t; double b; } Ev;
static int cmp_ev(const void *a, const void *b) { double x = ((const Ev *)a)->t, y = ((const Ev *)b)->t; return x < y ? -1 : x > y; }

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s stamps.bin [warps]\n", argv[0]); return 2; }
    const uint32_t W = argc > 2 ? (uint32_t)atoi(argv[2]) : 8u;
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    static Ev ev[MAXW * 4];
    double acc[2][10]; uint32_t cnt[2]; memset(acc, 0, sizeof acc); memset(cnt, 0, sizeof cnt);
    double smspread = 0; uint32_t nsm = 0, gridseen = 0;
    uint32_t hdr[4];
    while (fread(hdr, 4, 4, f) == 4) {
        if (hdr[0] != 0x504d5453u) { fprintf(stderr, "bad block magic\n"); return 1; }
        const uint32_t N = hdr[1], grid = hdr[2], stride = grid * W;
        gridseen = grid;
        uint32_t *rec = malloc((size_t)N * grid * R * 4);
        if (!rec || fread(rec, 4, (size_t)N * grid * R, f) != (size_t)N * grid * R) { fprintf(stderr, "short block\n"); return 1; }
        for (uint32_t s = 0; s < N; s++) {
            const uint32_t k = s % 2 == 0 ? 2048u : 4096u, n = s % 2 == 0 ? 4096u : 2048u;
            const uint32_t *q0 = rec + (size_t)s * grid * R;
            uint32_t t0 = q0[0];
            for (uint32_t b = 1; b < grid; b++) if ((int32_t)(q0[b * R] - t0) < 0) t0 = q0[b * R];
            size_t ne = 0; double B = 0;
            for (uint32_t b = 0; b < grid; b++) {
                const uint32_t *q = q0 + (size_t)b * R;
                for (uint32_t w = 0; w < W; w++) {
                    const uint32_t g0 = b * W + w, rw = g0 < n ? (n - g0 + stride - 1) / stride : 0;
                    if (!rw) continue;
                    const double tf = (int32_t)(q[8 + w * 2] - t0), tl = rw > 1 ? (double)(int32_t)(q[9 + w * 2] - t0) : tf;
                    for (uint32_t i = 0; i < rw; i++) { ev[ne].t = rw > 1 ? tf + (tl - tf) * i / (rw - 1) : tf; ev[ne].b = k; ne++; B += k; }
                }
            }
            qsort(ev, ne, sizeof *ev, cmp_ev);
            double cum = 0, t10 = 0, t50 = 0, t90 = 0, t100 = ev[ne - 1].t; int g10 = 0, g50 = 0, g90 = 0;
            for (size_t i = 0; i < ne; i++) { cum += ev[i].b;
                if (!g10 && cum >= 0.1 * B) { t10 = ev[i].t; g10 = 1; }
                if (!g50 && cum >= 0.5 * B) { t50 = ev[i].t; g50 = 1; }
                if (!g90 && cum >= 0.9 * B) { t90 = ev[i].t; g90 = 1; } }
            const double mid = 0.8 * B / (t90 - t10); /* bytes per ns = GB/s */
            const double ramp = t10 - 0.1 * B / mid, tail = t100 - t90 - 0.1 * B / mid, steady = 0.8 * B / mid, ideal = B / 217.0;
            const int sh = s % 2;
            double v[10] = { t10, t50, t90, t100, mid, ramp, tail, steady, ideal, (double)(int32_t)(q0[1] - t0) };
            for (int j = 0; j < 10; j++) acc[sh][j] += v[j];
            cnt[sh]++;
            /* per-SM: mean done per SM, spread of the SM means */
            double sum[256] = { 0 }; uint32_t c[256] = { 0 };
            for (uint32_t b = 0; b < grid; b++) { const uint32_t *q = q0 + (size_t)b * R; uint32_t sm = q[4] & 0xff; sum[sm] += (int32_t)(q[1] - t0); c[sm]++; }
            double lo = 1e18, hi = -1e18;
            for (int i = 0; i < 256; i++) if (c[i]) { double m = sum[i] / c[i]; if (m < lo) lo = m; if (m > hi) hi = m; }
            smspread += hi - lo; nsm++;
        }
        free(rec);
    }
    fclose(f);
    printf("grid=%u warps=%u (averages over all reps and stages of each shape; times in us from the stage's earliest CTA start)\n", gridseen, W);
    for (int sh = 0; sh < 2; sh++) {
        if (!cnt[sh]) continue;
        double *a = acc[sh]; const double c = cnt[sh];
        printf("SHAPE %s stages=%u t10=%.2f t50=%.2f t90=%.2f t100=%.2f mid_GBps=%.1f ramp_us=%.2f steady_us=%.2f tail_us=%.2f bytes_only_217_us=%.2f\n",
               sh == 0 ? "k2048xn4096" : "k4096xn2048", cnt[sh], a[0] / c / 1e3, a[1] / c / 1e3, a[2] / c / 1e3, a[3] / c / 1e3, a[4] / c,
               a[5] / c / 1e3, a[7] / c / 1e3, a[6] / c / 1e3, a[8] / c / 1e3);
    }
    printf("SM spread (max - min of per-SM mean CTA done) mean_us=%.2f over %u stages\n", nsm ? smspread / nsm / 1e3 : 0.0, nsm);
    return 0;
}
