/*
 * wpt_headbound: can the greedy token be found EXACTLY without reading the whole lm_head? (L274,
 * docs/speed/WORK_PER_TOKEN.md section "lm_head"). CPU only, from real hidden states.
 *
 *   wpt_headbound <outside.safetensors> <ref.logits> <max-positions> <stride>
 *
 * The greedy token is argmax_i w_i . h over V = 248320 rows w_i (bf16, 5120 wide). A row can be skipped, with the
 * answer unchanged, when an upper bound UB_i on w_i . h is below a lower bound LB on the maximum. This tool measures how
 * many rows survive, and so how many lm_head bytes a bound-based search must still read, for three sound bounds:
 *   cs     UB_i = |w_i| |h|                                   (Cauchy-Schwarz on the whole row; reads 4 B per row)
 *   adapt  pick the m 16-wide column blocks with the largest |h_b|; read those blocks of every row (one 32 B sector per
 *          row per block when the head is stored block-major), p_i = sum_{b in S} w_ib . h_b, and
 *          UB_i = p_i + sqrt(|w_i|^2 - |w_iS|^2) |h_rest|
 *   adapt20  same, the rest bounded per 256-wide super-block: sum_B sqrt(|w_iB|^2 - |w_iB,S|^2) |h_B,rest|
 *          (20 extra floats per row stored)
 * LB = the largest exact logit among the 16 rows with the largest p_i (those rows are read whole). Every UB carries a
 * float rounding margin of 2 * 5120 * 2^-24 * |w_i| |h| so the bound also holds for the chip's fp32 sum.
 * Surviving rows are read whole. Bytes = bound reads + survivors' remaining blocks + the 16 candidate rows.
 *
 * h is not stored by the reference run, only the f32 logits L = W h. h is recovered by least squares on 8192 rows
 * (normal equations in f64, Cholesky), then checked: all 248320 logits recomputed from h against the stored ones
 * (max |diff| printed; the argmax must match). Threads: OpenMP.
 */
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define D 5120
#define NB (D / 16)   /* 320 blocks of 16 */
#define NSB (D / 256) /* 20 super-blocks */
#define NLS 8192
#define KCAND 16

static const uint16_t *W;
static uint32_t V;

static inline float bf(uint16_t v) { uint32_t u = (uint32_t)v << 16; float f; memcpy(&f, &u, 4); return f; }

static const void *map_file(const char *p, size_t *len) {
    int fd = open(p, O_RDONLY);
    if (fd < 0) { perror(p); exit(1); }
    struct stat st;
    fstat(fd, &st);
    void *m = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (m == MAP_FAILED) { perror("mmap"); exit(1); }
    *len = (size_t)st.st_size;
    return m;
}

static int cmp_desc(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x < y) - (x > y);
}

int main(int argc, char **argv) {
    if (argc != 5) { fprintf(stderr, "usage: wpt_headbound <outside.safetensors> <ref.logits> <max-pos> <stride>\n"); return 2; }
    size_t flen, llen;
    const uint8_t *f = map_file(argv[1], &flen);
    uint64_t hn;
    memcpy(&hn, f, 8);
    const char *hdr = (const char *)f + 8;
    const char *k = strstr(hdr, "\"lm_head.weight\"");
    if (!k || k > hdr + hn) { fprintf(stderr, "no lm_head.weight\n"); return 1; }
    unsigned long long a, b, rows, cols;
    if (sscanf(strstr(k, "\"shape\":[") + 9, "%llu,%llu", &rows, &cols) != 2 ||
        sscanf(strstr(k, "\"data_offsets\":[") + 16, "%llu,%llu", &a, &b) != 2 || cols != D) {
        fprintf(stderr, "lm_head header parse failed\n"); return 1;
    }
    V = (uint32_t)rows;
    W = (const uint16_t *)(f + 8 + hn + a);
    const float *L = map_file(argv[2], &llen);
    size_t npos = llen / ((size_t)V * 4);
    int maxp = atoi(argv[3]), stride = atoi(argv[4]);
    fprintf(stderr, "V %u, %zu positions in %s\n", V, npos, argv[2]);

    /* Row norms: total, per 16-block, per 256-super-block (f64). */
    double *n2 = malloc(sizeof(double) * V);
    float *bn2 = malloc(sizeof(float) * (size_t)V * NB);
#pragma omp parallel for schedule(static)
    for (uint32_t i = 0; i < V; i++) {
        const uint16_t *w = W + (size_t)i * D;
        double t = 0;
        for (int bl = 0; bl < NB; bl++) {
            double s = 0;
            for (int j = 0; j < 16; j++) { double x = bf(w[bl * 16 + j]); s += x * x; }
            bn2[(size_t)i * NB + bl] = (float)s;
            t += s;
        }
        n2[i] = t;
    }

    /* Least squares rows: a fixed stride through the vocabulary. */
    uint32_t *ls = malloc(sizeof(uint32_t) * NLS);
    for (int r = 0; r < NLS; r++) ls[r] = (uint32_t)(((uint64_t)r * V) / NLS + 7) % V;
    double *At = malloc(sizeof(double) * (size_t)D * NLS); /* A transposed: column q of the chosen rows, contiguous */
    for (int r = 0; r < NLS; r++) { const uint16_t *w = W + (size_t)ls[r] * D; for (int q = 0; q < D; q++) At[(size_t)q * NLS + r] = bf(w[q]); }
    double *G = calloc((size_t)D * D, sizeof(double));
#pragma omp parallel for schedule(dynamic, 16)
    for (int p = 0; p < D; p++)
        for (int q = 0; q <= p; q++) {
            const double *x = At + (size_t)p * NLS, *z = At + (size_t)q * NLS;
            double s = 0;
            for (int r = 0; r < NLS; r++) s += x[r] * z[r];
            G[(size_t)p * D + q] = s;
        }
    free(At);
    /* Cholesky G = C C^T, lower, in place. */
    for (int j = 0; j < D; j++) {
        double s = G[(size_t)j * D + j];
        for (int t = 0; t < j; t++) s -= G[(size_t)j * D + t] * G[(size_t)j * D + t];
        if (s <= 0) { fprintf(stderr, "Cholesky failed at %d\n", j); return 1; }
        double c = sqrt(s);
        G[(size_t)j * D + j] = c;
#pragma omp parallel for schedule(static)
        for (int i = j + 1; i < D; i++) {
            double u = G[(size_t)i * D + j];
            for (int t = 0; t < j; t++) u -= G[(size_t)i * D + t] * G[(size_t)j * D + t];
            G[(size_t)i * D + j] = u / c;
        }
    }
    fprintf(stderr, "normal equations factored\n");

    const int MS[] = {8, 16, 32, 64, 128};
    const int NM = 5;
    double head_bytes = (double)V * D * 2;
    double sum_cs = 0, sum_ad[5] = {0}, sum_ad20[5] = {0}, worst_ad[5] = {0}, worst_ad20[5] = {0};
    int done = 0, bad = 0;
    printf("pos\terr_max\tnorm_h\ttop1\tgap12\tcs_surv\tconc16\tconc64\tconc256");
    for (int m = 0; m < NM; m++) printf("\tad%d_surv\tad%d_frac\tad20_%d_frac", MS[m], MS[m], MS[m]);
    printf("\n");
    double *h = malloc(sizeof(double) * D), *y = malloc(sizeof(double) * D);
    double *logit = malloc(sizeof(double) * V), *p = malloc(sizeof(double) * V);
    float *pb = malloc(sizeof(float) * (size_t)V * NB);
    for (size_t pos = 0; pos < npos && done < maxp; pos += (size_t)stride, done++) {
        const float *l = L + pos * V;
        /* b = A^T l_S, solve C C^T h = b */
        for (int q = 0; q < D; q++) y[q] = 0;
        for (int r = 0; r < NLS; r++) { const uint16_t *w = W + (size_t)ls[r] * D; double lv = l[ls[r]]; for (int q = 0; q < D; q++) y[q] += bf(w[q]) * lv; }
        for (int i = 0; i < D; i++) { double s = y[i]; for (int t = 0; t < i; t++) s -= G[(size_t)i * D + t] * y[t]; y[i] = s / G[(size_t)i * D + i]; }
        for (int i = D - 1; i >= 0; i--) { double s = y[i]; for (int t = i + 1; t < D; t++) s -= G[(size_t)t * D + i] * h[t]; h[i] = s / G[(size_t)i * D + i]; }
        double hh = 0;
        for (int q = 0; q < D; q++) hh += h[q] * h[q];
        double nh = sqrt(hh);
        /* Exact logits from h, and every 16-block partial. */
        double err = 0;
        uint32_t am_ref = 0, am_h = 0;
#pragma omp parallel for schedule(static) reduction(max : err)
        for (uint32_t i = 0; i < V; i++) {
            const uint16_t *w = W + (size_t)i * D;
            double t = 0;
            for (int bl = 0; bl < NB; bl++) {
                double s = 0;
                for (int j = 0; j < 16; j++) s += (double)bf(w[bl * 16 + j]) * h[bl * 16 + j];
                pb[(size_t)i * NB + bl] = (float)s;
                t += s;
            }
            logit[i] = t;
            double e = fabs(t - (double)l[i]);
            if (e > err) err = e;
        }
        for (uint32_t i = 1; i < V; i++) { if (l[i] > l[am_ref]) am_ref = i; if (logit[i] > logit[am_h]) am_h = i; }
        if (am_ref != am_h) bad++;
        double top1 = logit[am_h], top2 = -1e300;
        for (uint32_t i = 0; i < V; i++) if (i != am_h && logit[i] > top2) top2 = logit[i];
        const double marg = 2.0 * D * ldexp(1.0, -24) * nh;
        uint32_t cs = 0;
        for (uint32_t i = 0; i < V; i++) if (sqrt(n2[i]) * nh * (1 + 0) + marg * sqrt(n2[i]) >= top1) cs++;
        /* Block energies of h, sorted. */
        double hb[NB], hbs[NB];
        int order[NB];
        for (int bl = 0; bl < NB; bl++) { double s = 0; for (int j = 0; j < 16; j++) s += h[bl * 16 + j] * h[bl * 16 + j]; hb[bl] = s; hbs[bl] = s; order[bl] = bl; }
        for (int x = 0; x < NB; x++) for (int z = x + 1; z < NB; z++) if (hb[order[z]] > hb[order[x]]) { int t = order[x]; order[x] = order[z]; order[z] = t; }
        qsort(hbs, NB, sizeof(double), cmp_desc);
        /* coordinate concentration */
        double *hc = malloc(sizeof(double) * D);
        for (int q = 0; q < D; q++) hc[q] = h[q] * h[q];
        qsort(hc, D, sizeof(double), cmp_desc);
        double c16 = 0, c64 = 0, c256 = 0;
        for (int q = 0; q < 256; q++) { if (q < 16) c16 += hc[q]; if (q < 64) c64 += hc[q]; c256 += hc[q]; }
        free(hc);
        sum_cs += cs;
        printf("%zu\t%.3g\t%.4g\t%.4g\t%.4g\t%u\t%.4f\t%.4f\t%.4f", pos, err, nh, top1, top1 - top2, cs, c16 / hh, c64 / hh, c256 / hh);
        for (int mi = 0; mi < NM; mi++) {
            int m = MS[mi];
            char in[NB] = {0};
            double rest = hh;
            double restB[NSB] = {0};
            for (int x = 0; x < m; x++) { in[order[x]] = 1; rest -= hb[order[x]]; }
            for (int bl = 0; bl < NB; bl++) if (!in[bl]) restB[bl / 16] += hb[bl];
            if (rest < 0) rest = 0;
            double nrest = sqrt(rest);
            /* partials and bounds */
#pragma omp parallel for schedule(static)
            for (uint32_t i = 0; i < V; i++) {
                double s = 0;
                for (int x = 0; x < m; x++) s += pb[(size_t)i * NB + order[x]];
                p[i] = s;
            }
            /* LB: exact max over the KCAND largest partials */
            uint32_t cand[KCAND];
            double cv[KCAND];
            for (int c = 0; c < KCAND; c++) { cand[c] = 0; cv[c] = -1e300; }
            for (uint32_t i = 0; i < V; i++) {
                if (p[i] <= cv[KCAND - 1]) continue;
                int c = KCAND - 1;
                while (c > 0 && cv[c - 1] < p[i]) { cv[c] = cv[c - 1]; cand[c] = cand[c - 1]; c--; }
                cv[c] = p[i]; cand[c] = i;
            }
            double LB = -1e300;
            for (int c = 0; c < KCAND; c++) if (logit[cand[c]] > LB) LB = logit[cand[c]];
            LB -= marg * sqrt(n2[am_h]);
            uint64_t surv = 0, surv20 = 0;
            int lost = 0;
#pragma omp parallel for schedule(static) reduction(+ : surv, surv20, lost)
            for (uint32_t i = 0; i < V; i++) {
                const float *bn = bn2 + (size_t)i * NB;
                double ws = 0, wsB[NSB] = {0}, wB[NSB] = {0};
                for (int bl = 0; bl < NB; bl++) { wB[bl / 16] += bn[bl]; }
                for (int x = 0; x < m; x++) { ws += bn[order[x]]; wsB[order[x] / 16] += bn[order[x]]; }
                double wr = n2[i] - ws;
                if (wr < 0) wr = 0;
                double mg = marg * sqrt(n2[i]);
                double ub = p[i] + sqrt(wr) * nrest + mg;
                double ub20 = p[i] + mg;
                for (int B = 0; B < NSB; B++) { double r = wB[B] - wsB[B]; if (r < 0) r = 0; ub20 += sqrt(r) * sqrt(restB[B]); }
                if (ub >= LB) surv++;
                if (ub20 >= LB) surv20++;
                if (i == am_h && (ub < LB || ub20 < LB)) lost++;
            }
            if (lost) bad++;
            double rowb = (double)D * 2, blkb = 32.0;
            double by = (double)m * blkb * V + (double)surv * (NB - m) * blkb + KCAND * rowb + 4.0 * V;
            double by20 = (double)m * blkb * V + (double)surv20 * (NB - m) * blkb + KCAND * rowb + 4.0 * V * (1 + NSB);
            double fr = by / head_bytes, fr20 = by20 / head_bytes;
            sum_ad[mi] += fr; sum_ad20[mi] += fr20;
            if (fr > worst_ad[mi]) worst_ad[mi] = fr;
            if (fr20 > worst_ad20[mi]) worst_ad20[mi] = fr20;
            printf("\t%llu\t%.4f\t%.4f", (unsigned long long)surv, fr, fr20);
        }
        printf("\n");
        fflush(stdout);
    }
    printf("SUMMARY positions %d bad %d mean_cs_surv %.0f of %u", done, bad, sum_cs / done, V);
    for (int mi = 0; mi < NM; mi++)
        printf(" | m%d mean_frac %.4f worst %.4f ad20 mean %.4f worst %.4f", MS[mi], sum_ad[mi] / done, worst_ad[mi],
               sum_ad20[mi] / done, worst_ad20[mi]);
    printf("\n");
    return bad ? 1 : 0;
}
