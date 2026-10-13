/* fp4_sens: cheap output-space scoring of an OFP4v1 store against the FP8 checkpoint on calibration activations (OWN-30B cut L3b). No Python.
 *   fp4_sens <checkpoint_dir> <store> <calib_dir> [--tokens all|train|val] [--nval V] [--rot gu,dn] [--layers N] [--out sens.tsv]
 * For every routed (token, expert) pair of the chosen tokens it runs the FULL expert (gate|up, SiLU, down) with FP8 weights and with the store's weights
 * on the same input and accumulates the weighted error energy  wt^2 * ||y_q - y_ref||^2  and the reference energy  wt^2 * ||y_ref||^2.
 * Prints per-layer relative error and writes one line per expert: layer expert pairs err_energy ref_energy (energies per token of the set).
 * This is the sensitivity ranking used for mixed precision, and a fast proxy for the end-to-end study. */
#include "fp4_st.h"
#include "fp4_quant.h"
#include <omp.h>

enum { E = 128, GU_N = 1536, GU_K = 2048, DN_N = 2048, DN_K = 768, HDR = 4096, TOPK = 8, FF = 768, D = 2048, REC = D + TOPK + TOPK + TOPK * FF };
#define GU_W ((size_t)GU_N * GU_K / 2)
#define GU_S ((size_t)GU_N * GU_K / 16)
#define DN_W ((size_t)DN_N * DN_K / 2)
#define DN_S ((size_t)DN_N * DN_K / 16)
#define LAYER_STRIDE ((size_t)E * (GU_W + GU_S + 4 + DN_W + DN_S + 4))

static void fp8_dequant(const StTensor *w, const StTensor *s, float *dst, size_t dst_stride) {
    uint32_t rows = w->shape[0], cols = w->shape[1], sb = (cols + 127) / 128; const uint16_t *sc = (const uint16_t *)s->data;
    for (uint32_t r = 0; r < rows; r++) for (uint32_t c = 0; c < cols; c++) dst[(size_t)r * dst_stride + c] = st_e4m3(w->data[(size_t)r * cols + c]) * st_bf16(sc[(r / 128) * sb + c / 128]);
}
static void matvec(const float *W, int n, int k, const float *x, float *y) {
    for (int r = 0; r < n; r++) { const float *w = W + (size_t)r * k; float a = 0; for (int c = 0; c < k; c++) a += w[c] * x[c]; y[r] = a; }
}
static void expert_fwd(const float *gu, const float *dn, const float *x, const float *xrot, float *y, float *tmp /* GU_N + FF */, int rot_dn) {
    float *g = tmp; matvec(gu, GU_N, GU_K, xrot ? xrot : x, g);
    float hh[FF]; for (int i = 0; i < FF; i++) hh[i] = g[i] / (1.0f + expf(-g[i])) * g[FF + i];
    if (rot_dn > 1) fq_wht(hh, FF, rot_dn);
    matvec(dn, DN_N, DN_K, hh, y);
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: fp4_sens <ckpt> <store> <calib_dir> [options]\n"); return 2; }
    const char *which = "all", *outp = NULL; int nval = 8, rot_gu = 0, rot_dn = 0, layers = 48;
    for (int i = 4; i < argc; i++) {
        if (!strcmp(argv[i], "--tokens") && i + 1 < argc) which = argv[++i]; else if (!strcmp(argv[i], "--nval") && i + 1 < argc) nval = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--rot") && i + 1 < argc) sscanf(argv[++i], "%d,%d", &rot_gu, &rot_dn); else if (!strcmp(argv[i], "--layers") && i + 1 < argc) layers = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) outp = argv[++i];
    }
    StIndex ix = { 0 }; if (st_open_dir(&ix, argv[1])) return 1;
    int fd = open(argv[2], O_RDONLY); struct stat sb; fstat(fd, &sb); uint8_t *store = mmap(NULL, (size_t)sb.st_size, PROT_READ, MAP_SHARED, fd, 0); close(fd);
    if (store == MAP_FAILED || memcmp(store, "AFP4v1", 6)) { fprintf(stderr, "bad store\n"); return 1; }
    char pth[600]; snprintf(pth, sizeof pth, "%s/meta.txt", argv[3]); FILE *mf = fopen(pth, "r"); if (!mf) { perror(pth); return 1; }
    int seqlen[4096], nseq = 0, sq, sd, tk; while (fscanf(mf, "seq %d seed %d tokens %d\n", &sq, &sd, &tk) == 3 && nseq < 4096) seqlen[nseq++] = tk; fclose(mf);
    long ntok = 0, nvt = 0; for (int i = 0; i < nseq; i++) { ntok += seqlen[i]; if (i >= nseq - nval) nvt += seqlen[i]; }
    long t_lo = 0, t_hi = ntok; if (!strcmp(which, "val")) t_lo = ntok - nvt; else if (!strcmp(which, "train")) t_hi = ntok - nvt;
    fprintf(stderr, "scoring tokens [%ld, %ld) of %ld\n", t_lo, t_hi, ntok);
    FILE *out = outp ? fopen(outp, "w") : NULL; double tn = 0, td = 0;
    for (int Ly = 0; Ly < layers; Ly++) {
        snprintf(pth, sizeof pth, "%s/act_%02d.bin", argv[3], Ly); fd = open(pth, O_RDONLY); if (fd < 0 || fstat(fd, &sb)) { perror(pth); return 1; }
        const float *act = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0); close(fd);
        const uint8_t *p = store + HDR + (size_t)Ly * LAYER_STRIDE; const uint8_t *gw = p, *gs = gw + (size_t)E * GU_W; const float *gt = (const float *)(gs + (size_t)E * GU_S);
        const uint8_t *dw = (const uint8_t *)(gt + E), *ds = dw + (size_t)E * DN_W; const float *dt = (const float *)(ds + (size_t)E * DN_S);
        double en[E] = { 0 }, ed[E] = { 0 }; long np[E] = { 0 };
        #pragma omp parallel for schedule(dynamic, 1)
        for (int e = 0; e < E; e++) {
            long cnt = 0; for (long t = t_lo; t < t_hi; t++) { const int *ids = (const int *)(act + t * REC + D); for (int j = 0; j < TOPK; j++) if (ids[j] == e) cnt++; }
            np[e] = cnt; if (!cnt) continue;
            float *gref = malloc((size_t)GU_N * GU_K * 4), *gq = malloc((size_t)GU_N * GU_K * 4), *dref = malloc((size_t)DN_N * DN_K * 4), *dq = malloc((size_t)DN_N * DN_K * 4);
            char nm[160]; const StTensor *w, *s; static const char *pn[3] = { "gate_proj", "up_proj", "down_proj" };
            for (int proj = 0; proj < 3; proj++) {
                snprintf(nm, sizeof nm, "model.layers.%d.mlp.experts.%d.%s.weight", Ly, e, pn[proj]); w = st_get(&ix, nm);
                snprintf(nm, sizeof nm, "model.layers.%d.mlp.experts.%d.%s.weight_scale_inv", Ly, e, pn[proj]); s = st_get(&ix, nm);
                if (proj < 2) fp8_dequant(w, s, gref + (size_t)proj * 768 * GU_K, GU_K); else fp8_dequant(w, s, dref, DN_K);
            }
            omega_fp4_dequantize(gw + (size_t)e * GU_W, gs + (size_t)e * GU_S, gt[e], GU_N, GU_K, gq); omega_fp4_dequantize(dw + (size_t)e * DN_W, ds + (size_t)e * DN_S, dt[e], DN_N, DN_K, dq);
            float tmp[GU_N + FF], yr[DN_N], yq[DN_N], xr[D];
            for (long t = t_lo; t < t_hi; t++) { const int *ids = (const int *)(act + t * REC + D); const float *x = act + t * REC;
                for (int j = 0; j < TOPK; j++) if (ids[j] == e) {
                    float wv = act[t * REC + D + TOPK + j]; wv *= wv;
                    expert_fwd(gref, dref, x, NULL, yr, tmp, 0);
                    const float *xin = x; if (rot_gu > 1) { memcpy(xr, x, sizeof xr); fq_wht(xr, D, rot_gu); xin = xr; }
                    expert_fwd(gq, dq, xin, NULL, yq, tmp, rot_dn);
                    double a = 0, b = 0; for (int i = 0; i < DN_N; i++) { double df = (double)yq[i] - yr[i]; a += df * df; b += (double)yr[i] * yr[i]; }
                    en[e] += wv * a; ed[e] += wv * b; } }
            free(gref); free(gq); free(dref); free(dq);
        }
        double ln = 0, ld = 0; for (int e = 0; e < E; e++) { ln += en[e]; ld += ed[e]; if (out) fprintf(out, "%d\t%d\t%ld\t%.6g\t%.6g\n", Ly, e, np[e], en[e], ed[e]); }
        printf("layer %2d output-space rel error %.4f\n", Ly, ld > 0 ? sqrt(ln / ld) : 0.0); fflush(stdout); tn += ln; td += ld; munmap((void *)act, sb.st_size);
    }
    printf("ALL: weighted output-space rel error %.4f\n", sqrt(tn / td));
    if (out) fclose(out); return 0;
}
