/* fp4_gptq: activation-aware (GPTQ-style) conversion of the FP8 Qwen3-30B-A3B experts to the OFP4v1 store (OWN-30B cut L3b). No Python.
 *   fp4_gptq <checkpoint_dir> <calib_dir> <out_store> [--layers N] [--nval V] [--search lo,hi] [--damp 0.01] [--n0 256] [--n1 256] [--minval 8] [--rot gu,dn]
 * <calib_dir> comes from `fp4_study --calib` (sequences sampled from the FP8 model, never the golden prompts). Per layer file act_LL.bin holds, per
 * token, xn[2048] f32 (expert input), ids[8] i32, wt[8] f32, hh[8][768] f32 (down-projection input of each chosen expert), see fp4_study.c.
 * The last V sequences are held out: they are never used to fit anything, only to score RTN against GPTQ on the OUTPUT error ||(W-Wq) x|| / ||W x||,
 * and each expert matrix keeps the GPTQ result only if it beats plain rounding on that held-out data (otherwise it keeps the RTN codes).
 * Hessian per expert matrix: H = (1-a) Hexp + a Hlayer (gate|up: layer-pooled prior, a = n0/(n_e+n0)); down: Hexp + ridge (a = n1/(n_e+n1)).
 * The store layout is exactly the converter's, so fp4_study and the chip kernel read it unchanged. */
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

/* ---- dense helpers on row-major k x k matrices (lower triangle used) ---- */
static int chol_lower(float *A, int k) { /* in place, A = L L^T, returns 0 ok */
    for (int i = 0; i < k; i++) {
        float *ai = A + (size_t)i * k;
        for (int j = 0; j <= i; j++) {
            float *aj = A + (size_t)j * k; float s = ai[j]; for (int m = 0; m < j; m++) s -= ai[m] * aj[m];
            if (i == j) { if (!(s > 0.0f)) return -1; ai[i] = sqrtf(s); } else ai[j] = s / aj[j];
        }
    }
    return 0;
}
static void tri_inv_lower(const float *L, float *Li, int k, float *tmp) {
    for (int i = 0; i < k; i++) {
        const float *li = L + (size_t)i * k; float *oi = Li + (size_t)i * k;
        for (int j = 0; j <= i; j++) tmp[j] = 0.0f;
        for (int m = 0; m < i; m++) { float a = li[m]; const float *om = Li + (size_t)m * k; for (int j = 0; j <= m; j++) tmp[j] += a * om[j]; }
        float inv = 1.0f / li[i]; for (int j = 0; j < i; j++) oi[j] = -tmp[j] * inv; oi[i] = inv;
        for (int j = i + 1; j < k; j++) oi[j] = 0.0f;
    }
}
static void gram_lower(const float *Li, float *H, int k) { /* H = Li^T Li, lower triangle */
    for (size_t i = 0; i < (size_t)k * k; i++) H[i] = 0.0f;
    for (int m = 0; m < k; m++) { const float *r = Li + (size_t)m * k; for (int a = 0; a <= m; a++) { float ra = r[a]; float *h = H + (size_t)a * k; for (int b = 0; b <= a; b++) h[b] += ra * r[b]; } }
}

/* H[a][b] += sum_t ws[t] * xs[t][a] * xs[t][b] for rows a in [a0,a1), b <= a. Tokens are taken 16 at a time so each row of H is touched once per 16 tokens. */
static void accum_gram(float *H, int k, int a0, int a1, int nt, const float **xs, const float *ws) {
    for (int t0 = 0; t0 < nt; t0 += 16) {
        int tb = nt - t0 < 16 ? nt - t0 : 16; const float *xp[16];
        for (int t = 0; t < tb; t++) xp[t] = xs[t0 + t];
        for (int a = a0; a < a1; a++) {
            float xa[16]; for (int t = 0; t < tb; t++) xa[t] = ws[t0 + t] * xp[t][a];
            float *h = H + (size_t)a * k;
            if (tb == 16) { for (int b = 0; b <= a; b++) { float s = h[b]; for (int t = 0; t < 16; t++) s += xa[t] * xp[t][b]; h[b] = s; } }
            else for (int b = 0; b <= a; b++) { float s = h[b]; for (int t = 0; t < tb; t++) s += xa[t] * xp[t][b]; h[b] = s; }
        }
    }
}

/* GPTQ with FP4 block-16 grid. W is n x k (modified, ends as the dequantised result divided by nothing: it holds the quantised values),
 * H is k x k (destroyed). Returns 0 on success. Writes OFP4v1 codes and scales. */
static int gptq_fp4(float *W, int n, int k, float *H, float damp, double tsd, int lo, int hi, uint8_t *w4, uint8_t *scales, float *work /* 3*k*k + k */) {
    float *L = work, *Li = work + (size_t)k * k, *Hi = work + 2 * (size_t)k * k, *tmp = work + 3 * (size_t)k * k;
    double mean = 0; int dead = 0;
    for (int i = 0; i < k; i++) { float d = H[(size_t)i * k + i]; if (d <= 0.0f) { dead++; H[(size_t)i * k + i] = 1.0f; for (int r = 0; r < n; r++) W[(size_t)r * k + i] = W[(size_t)r * k + i]; } else mean += d; }
    mean /= (k - dead > 0 ? k - dead : 1);
    for (int i = 0; i < k; i++) H[(size_t)i * k + i] += (float)(damp * mean);
    memcpy(L, H, (size_t)k * k * 4);
    if (chol_lower(L, k)) return -1;
    tri_inv_lower(L, Li, k, tmp); gram_lower(Li, Hi, k);
    if (chol_lower(Hi, k)) return -2; /* Hi = Lh with Hinv = Lh Lh^T; U = Lh^T */
    for (int i = 0; i < k; i++) for (int j = 0; j < k; j++) L[(size_t)i * k + j] = j >= i ? Hi[(size_t)j * k + i] : 0.0f; /* L now holds U (upper) */
    const float *U = L; enum { B = 128 };
    float *Err = malloc((size_t)n * B * 4); uint8_t *gs = malloc(n); double *gsv = malloc(n * sizeof(double)); uint8_t *codes = malloc((size_t)n * k);
    if (!Err || !gs || !gsv || !codes) return -3;
    for (int j1 = 0; j1 < k; j1 += B) {
        int j2 = j1 + B < k ? j1 + B : k;
        for (int i = j1; i < j2; i++) {
            if (i % 16 == 0) { /* choose the scale of this 16-column group from the current (already compensated) weights */
                for (int r = 0; r < n; r++) {
                    const float *blk = W + (size_t)r * k + i; double ba = 0; for (int t = 0; t < 16; t++) { double a = fabs((double)blk[t]); if (a > ba) ba = a; }
                    uint8_t bs = 0; double bv = 0;
                    if (ba > 0) { int s0 = omega_fp4_e4m3_round((float)(ba / (6.0 * tsd))); double best = INFINITY; uint8_t cc[16];
                        for (int d = lo; d <= hi; d++) { int s = s0 + d; if (s < 1 || s > 0x7E) continue; double sv = (double)omega_fp4_e4m3_unsigned((uint8_t)s) * tsd; double sse = fq_block_sse(blk, sv, cc); if (sse < best) { best = sse; bs = (uint8_t)s; bv = sv; } } }
                    gs[r] = bs; gsv[r] = bv; scales[(size_t)r * (k / 16) + i / 16] = bs;
                }
            }
            float dii = U[(size_t)i * k + i]; const float *ui = U + (size_t)i * k;
            for (int r = 0; r < n; r++) {
                float *wr = W + (size_t)r * k; double sv = gsv[r]; uint8_t c = sv > 0.0 ? omega_fp4_nearest_code((float)((double)wr[i] / sv)) : 0;
                codes[(size_t)r * k + i] = c; float q = (float)((double)omega_fp4_value(c) * sv); float err = (wr[i] - q) / dii; Err[(size_t)r * B + (i - j1)] = err; wr[i] = q;
                for (int j = i + 1; j < j2; j++) wr[j] -= err * ui[j];
            }
        }
        if (j2 < k) for (int r = 0; r < n; r++) { float *wr = W + (size_t)r * k; const float *er = Err + (size_t)r * B;
            for (int t = 0; t < j2 - j1; t++) { float e = er[t]; const float *ut = U + (size_t)(j1 + t) * k; for (int j = j2; j < k; j++) wr[j] -= e * ut[j]; } }
    }
    for (int r = 0; r < n; r++) for (int c = 0; c < k; c += 2) w4[(size_t)r * (k / 2) + c / 2] = (uint8_t)(codes[(size_t)r * k + c] | (codes[(size_t)r * k + c + 1] << 4));
    free(Err); free(gs); free(gsv); free(codes); return 0;
}

static void dequant_store(const uint8_t *w4, const uint8_t *sc, float ts, int n, int k, float *w) { omega_fp4_dequantize(w4, sc, ts, n, k, w); }

/* output error accumulators on held-out inputs */
static void out_err(const float *Wref, const float *Wq, int n, int k, const float *x, double *num, double *den) {
    for (int r = 0; r < n; r++) { const float *a = Wref + (size_t)r * k, *b = Wq + (size_t)r * k; float ya = 0, yb = 0; for (int c = 0; c < k; c++) { ya += a[c] * x[c]; yb += b[c] * x[c]; } *num += (double)(ya - yb) * (ya - yb); *den += (double)ya * ya; }
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: fp4_gptq <ckpt> <calib_dir> <out_store> [options]\n"); return 2; }
    int layers = 48, nval = 8, lo = -8, hi = 2, minval = 8, rot_gu = 0, rot_dn = 0, first = 0; float damp = 0.01f, n0 = 256, n1 = 256;
    for (int i = 4; i < argc; i++) {
        if (!strcmp(argv[i], "--layers") && i + 1 < argc) layers = atoi(argv[++i]); else if (!strcmp(argv[i], "--first") && i + 1 < argc) first = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--nval") && i + 1 < argc) nval = atoi(argv[++i]); else if (!strcmp(argv[i], "--search") && i + 1 < argc) sscanf(argv[++i], "%d,%d", &lo, &hi);
        else if (!strcmp(argv[i], "--damp") && i + 1 < argc) damp = (float)atof(argv[++i]); else if (!strcmp(argv[i], "--n0") && i + 1 < argc) n0 = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--n1") && i + 1 < argc) n1 = (float)atof(argv[++i]); else if (!strcmp(argv[i], "--minval") && i + 1 < argc) minval = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--rot") && i + 1 < argc) sscanf(argv[++i], "%d,%d", &rot_gu, &rot_dn);
    }
    (void)rot_gu; (void)rot_dn;
    StIndex ix = { 0 }; if (st_open_dir(&ix, argv[1])) return 1;
    char pth[600]; snprintf(pth, sizeof pth, "%s/meta.txt", argv[2]); FILE *mf = fopen(pth, "r"); if (!mf) { perror(pth); return 1; }
    int seqlen[4096], nseq = 0, sq, sd, tk; while (fscanf(mf, "seq %d seed %d tokens %d\n", &sq, &sd, &tk) == 3 && nseq < 4096) seqlen[nseq++] = tk; fclose(mf);
    long ntok = 0, nval_tok = 0; for (int i = 0; i < nseq; i++) { ntok += seqlen[i]; if (i >= nseq - nval) nval_tok += seqlen[i]; }
    long ntrain = ntok - nval_tok; fprintf(stderr, "calibration: %d sequences, %ld tokens (train %ld, held-out %ld)\n", nseq, ntok, ntrain, nval_tok);
    FILE *out = fopen(argv[3], "wb"); if (!out) { perror(argv[3]); return 1; }
    uint8_t hdr[HDR] = { 0 }; memcpy(hdr, "AFP4v1\0\0", 8); uint32_t dims[6] = { 48, E, GU_N, GU_K, DN_N, DN_K }; memcpy(hdr + 8, dims, sizeof dims); fwrite(hdr, 1, HDR, out);
    uint8_t *buf = malloc(LAYER_STRIDE); int nth = omp_get_max_threads(); float **works = calloc(nth, sizeof(float *));
    double tot_rtn_n = 0, tot_gp_n = 0, tot_den = 0; long kept_gu = 0, kept_dn = 0;
    for (int Ly = 0; Ly < 48; Ly++) {
        if (Ly < first || Ly >= first + layers) { /* write a placeholder layer so the file stays complete and parseable */ memset(buf, 0, LAYER_STRIDE); fwrite(buf, 1, LAYER_STRIDE, out); continue; }
        snprintf(pth, sizeof pth, "%s/act_%02d.bin", argv[2], Ly); int fd = open(pth, O_RDONLY); struct stat sb; if (fd < 0 || fstat(fd, &sb)) { perror(pth); return 1; }
        if ((long)(sb.st_size / (REC * 4)) < ntok) { fprintf(stderr, "%s: %lld records, meta says %ld\n", pth, (long long)(sb.st_size / (REC * 4)), ntok); return 1; }
        const float *act = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0); close(fd);
        /* per-expert token lists */
        int *cnt_tr = calloc(E, sizeof(int)), *cnt_va = calloc(E, sizeof(int));
        for (long t = 0; t < ntok; t++) { const int *ids = (const int *)(act + t * REC + D); for (int j = 0; j < TOPK; j++) { if (t < ntrain) cnt_tr[ids[j]]++; else cnt_va[ids[j]]++; } }
        int **tr = calloc(E, sizeof(int *)), **va = calloc(E, sizeof(int *)); for (int e = 0; e < E; e++) { tr[e] = malloc((cnt_tr[e] + 1) * 2 * sizeof(int)); va[e] = malloc((cnt_va[e] + 1) * 2 * sizeof(int)); cnt_tr[e] = cnt_va[e] = 0; }
        for (long t = 0; t < ntok; t++) { const int *ids = (const int *)(act + t * REC + D); for (int j = 0; j < TOPK; j++) { int e = ids[j]; if (t < ntrain) { tr[e][2 * cnt_tr[e]] = (int)t; tr[e][2 * cnt_tr[e]++ + 1] = j; } else { va[e][2 * cnt_va[e]] = (int)t; va[e][2 * cnt_va[e]++ + 1] = j; } } }
        /* layer-pooled Hessian of the expert input (gate|up prior), mean over training tokens */
        float *HL = calloc((size_t)D * D, 4);
        { const float **xsl = malloc(ntrain * sizeof(float *)); float *wsl = malloc(ntrain * 4); for (long t = 0; t < ntrain; t++) { xsl[t] = act + t * REC; wsl[t] = 1.0f; }
          #pragma omp parallel for schedule(dynamic, 1)
          for (int a0 = 0; a0 < D; a0 += 32) accum_gram(HL, D, a0, a0 + 32, (int)ntrain, xsl, wsl);
          free(xsl); free(wsl); }
        for (size_t i = 0; i < (size_t)D * D; i++) HL[i] /= (float)ntrain;
        uint8_t *gw = buf, *gs = gw + (size_t)E * GU_W; float *gt = (float *)(gs + (size_t)E * GU_S); uint8_t *dw = (uint8_t *)(gt + E), *ds = dw + (size_t)E * DN_W; float *dt = (float *)(ds + (size_t)E * DN_S);
        double lr_n = 0, lg_n = 0, l_den = 0, ldr_n = 0, ldg_n = 0, ldn_den = 0; int lkeep_gu = 0, lkeep_dn = 0, bad = 0;
        #pragma omp parallel
        {
            int th = omp_get_thread_num(); if (!works[th]) works[th] = malloc(((size_t)3 * D * D + D) * 4);
            float *mat = malloc((size_t)GU_N * GU_K * 4), *rtnq = malloc((size_t)GU_N * GU_K * 4), *gq = malloc((size_t)GU_N * GU_K * 4), *H = malloc((size_t)D * D * 4);
            uint8_t *rw = malloc(GU_W), *rs = malloc(GU_S), *g4 = malloc(GU_W), *gsb = malloc(GU_S);
            #pragma omp for schedule(dynamic, 1) reduction(+: lr_n, lg_n, l_den, ldr_n, ldg_n, ldn_den, lkeep_gu, lkeep_dn, bad)
            for (int e = 0; e < E; e++) {
                char nm[160]; const StTensor *w, *s; static const char *pn[3] = { "gate_proj", "up_proj", "down_proj" };
                for (int proj = 0; proj < 2; proj++) {
                    snprintf(nm, sizeof nm, "model.layers.%d.mlp.experts.%d.%s.weight", Ly, e, pn[proj]); w = st_get(&ix, nm);
                    snprintf(nm, sizeof nm, "model.layers.%d.mlp.experts.%d.%s.weight_scale_inv", Ly, e, pn[proj]); s = st_get(&ix, nm);
                    if (!w || !s) { bad++; continue; } fp8_dequant(w, s, mat + (size_t)proj * 768 * GU_K, GU_K);
                }
                /* ---- gate|up ---- */
                float tsg = fq_quantize(mat, GU_N, GU_K, rw, rs, lo, hi); dequant_store(rw, rs, tsg, GU_N, GU_K, rtnq);
                { /* Hessian: expert part weighted by wt^2, blended with the layer prior */
                    int nt = cnt_tr[e]; double wsum = 0; for (size_t i = 0; i < (size_t)D * D; i++) H[i] = 0.0f;
                    { const float **xs = malloc((nt + 1) * sizeof(float *)); float *ws = malloc((nt + 1) * 4); for (int q = 0; q < nt; q++) { long t = tr[e][2 * q]; int j = tr[e][2 * q + 1]; float wv = act[t * REC + D + TOPK + j]; wv *= wv; wsum += wv; xs[q] = act + t * REC; ws[q] = wv; }
                      accum_gram(H, D, 0, D, nt, xs, ws); free(xs); free(ws); }
                    float al = n0 / ((float)nt + n0); float inv = wsum > 0 ? (float)(1.0 / wsum) : 0.0f; if (wsum <= 0) al = 1.0f;
                    for (int a = 0; a < D; a++) for (int b = 0; b <= a; b++) { size_t i = (size_t)a * D + b; H[i] = (1.0f - al) * H[i] * inv + al * HL[i]; }
                }
                memcpy(gq, mat, (size_t)GU_N * GU_K * 4);
                int rc = gptq_fp4(gq, GU_N, GU_K, H, damp, (double)tsg, lo, hi, g4, gsb, works[th]); if (rc) { bad++; continue; }
                { double rn = 0, gn = 0, dn = 0; float *gdq = H; /* reuse as scratch is too small; recompute dequant into rtnq/gq style below */ (void)gdq;
                  float *gdeq = malloc((size_t)GU_N * GU_K * 4); dequant_store(g4, gsb, tsg, GU_N, GU_K, gdeq);
                  for (int q = 0; q < cnt_va[e]; q++) { long t = va[e][2 * q]; const float *x = act + t * REC; out_err(mat, rtnq, GU_N, GU_K, x, &rn, &dn); out_err(mat, gdeq, GU_N, GU_K, x, &gn, &dn); }
                  dn *= 0.5; /* den was added twice */
                  int use_g = cnt_va[e] >= minval ? gn < rn : cnt_tr[e] >= 4 * minval;
                  if (use_g) { memcpy(gw + (size_t)e * GU_W, g4, GU_W); memcpy(gs + (size_t)e * GU_S, gsb, GU_S); lkeep_gu++; } else { memcpy(gw + (size_t)e * GU_W, rw, GU_W); memcpy(gs + (size_t)e * GU_S, rs, GU_S); }
                  gt[e] = tsg; lr_n += rn; lg_n += use_g ? gn : rn; l_den += dn; free(gdeq); }
                /* ---- down ---- */
                snprintf(nm, sizeof nm, "model.layers.%d.mlp.experts.%d.down_proj.weight", Ly, e); w = st_get(&ix, nm);
                snprintf(nm, sizeof nm, "model.layers.%d.mlp.experts.%d.down_proj.weight_scale_inv", Ly, e); s = st_get(&ix, nm);
                fp8_dequant(w, s, mat, DN_K);
                float tsd2 = fq_quantize(mat, DN_N, DN_K, rw, rs, lo, hi); dequant_store(rw, rs, tsd2, DN_N, DN_K, rtnq);
                { int nt = cnt_tr[e]; double wsum = 0; for (size_t i = 0; i < (size_t)FF * FF; i++) H[i] = 0.0f;
                  { const float **xs = malloc((nt + 1) * sizeof(float *)); float *ws = malloc((nt + 1) * 4); for (int q = 0; q < nt; q++) { long t = tr[e][2 * q]; int j = tr[e][2 * q + 1]; float wv = act[t * REC + D + TOPK + j]; wv *= wv; wsum += wv; xs[q] = act + t * REC + D + 2 * TOPK + (size_t)j * FF; ws[q] = wv; }
                    accum_gram(H, FF, 0, FF, nt, xs, ws); free(xs); free(ws); }
                  float inv = wsum > 0 ? (float)(1.0 / wsum) : 0.0f; double mean = 0; for (int a = 0; a < FF; a++) { for (int b = 0; b <= a; b++) H[(size_t)a * FF + b] *= inv; mean += H[(size_t)a * FF + a]; } mean /= FF;
                  float al = n1 / ((float)nt + n1); if (wsum <= 0) { al = 1; mean = 1; }
                  for (int a = 0; a < FF; a++) H[(size_t)a * FF + a] += (float)(al * mean); }
                memcpy(gq, mat, (size_t)DN_N * DN_K * 4);
                rc = gptq_fp4(gq, DN_N, DN_K, H, damp, (double)tsd2, lo, hi, g4, gsb, works[th]); if (rc) { bad++; continue; }
                { double rn = 0, gn = 0, dn = 0; float *gdeq = malloc((size_t)DN_N * DN_K * 4); dequant_store(g4, gsb, tsd2, DN_N, DN_K, gdeq);
                  for (int q = 0; q < cnt_va[e]; q++) { long t = va[e][2 * q]; int j = va[e][2 * q + 1]; const float *x = act + t * REC + D + 2 * TOPK + (size_t)j * FF; out_err(mat, rtnq, DN_N, DN_K, x, &rn, &dn); out_err(mat, gdeq, DN_N, DN_K, x, &gn, &dn); }
                  dn *= 0.5; int use_g = cnt_va[e] >= minval ? gn < rn : cnt_tr[e] >= 4 * minval;
                  if (use_g) { memcpy(dw + (size_t)e * DN_W, g4, DN_W); memcpy(ds + (size_t)e * DN_S, gsb, DN_S); lkeep_dn++; } else { memcpy(dw + (size_t)e * DN_W, rw, DN_W); memcpy(ds + (size_t)e * DN_S, rs, DN_S); }
                  dt[e] = tsd2; ldr_n += rn; ldg_n += use_g ? gn : rn; ldn_den += dn; free(gdeq); }
            }
            free(mat); free(rtnq); free(gq); free(H); free(rw); free(rs); free(g4); free(gsb);
        }
        if (bad) { fprintf(stderr, "layer %d: %d failures (missing tensor or Cholesky)\n", Ly, bad); return 1; }
        if (omega_fp4_scan_scales(gs, (size_t)E * GU_S) || omega_fp4_scan_scales(ds, (size_t)E * DN_S)) { fprintf(stderr, "layer %d: illegal scale byte\n", Ly); return 1; }
        fwrite(buf, 1, LAYER_STRIDE, out); fflush(out);
        printf("layer %2d held-out output rel error: gate|up RTN %.4f -> kept %.4f (GPTQ used on %d/128) | down RTN %.4f -> kept %.4f (GPTQ used on %d/128)\n", Ly,
               sqrt(lr_n / l_den), sqrt(lg_n / l_den), lkeep_gu, sqrt(ldr_n / ldn_den), sqrt(ldg_n / ldn_den), lkeep_dn);
        fflush(stdout);
        tot_rtn_n += lr_n + ldr_n; tot_gp_n += lg_n + ldg_n; tot_den += l_den + ldn_den; kept_gu += lkeep_gu; kept_dn += lkeep_dn;
        for (int e = 0; e < E; e++) { free(tr[e]); free(va[e]); } free(tr); free(va); free(cnt_tr); free(cnt_va); free(HL); munmap((void *)act, sb.st_size);
    }
    printf("ALL: held-out output rel error RTN %.4f -> kept %.4f; GPTQ kept on gate|up %ld, down %ld matrices\n", sqrt(tot_rtn_n / tot_den), sqrt(tot_gp_n / tot_den), kept_gu, kept_dn);
    fclose(out); return 0;
}
