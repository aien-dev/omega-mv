/* fp4_study: CPU accuracy study for FP4 (OFP4v1) experts in Qwen3-30B-A3B-Instruct-2507 (OWN-30B cut L3). Own C forward pass, no Python.
 *   fp4_study <checkpoint_dir> <store.afp4> <prompts.tsv> [--only p1,p2] [--max-pos N] [--out result.json]
 * prompts.tsv: name <TAB> prompt tokens (comma separated) <TAB> golden tokens (comma separated), made with jq from golden.json.
 * Two model streams run in lockstep on the same token sequence (prompt + golden tokens, teacher forced):
 *   stream 0: experts exactly as the FP8 checkpoint (e4m3 * bf16 block-128 scale); validated against the golden greedy tokens.
 *   stream 1: experts from the FP4 store (value * e4m3 scale * tensor scale); everything else identical (FP8 attention, bf16 rest).
 * Reports: per-position logits cosine and top-1 agreement, FP4 argmax vs golden at generated positions, and, on the stream-0 activations,
 * the per-layer MoE output error (relative L2 and cosine) of FP4 experts against FP8 experts for the same input. */
#include "fp4_st.h"
#include "omega_gpu_fp4_api.h"
#include <math.h>
#include <omp.h>

enum { D = 2048, NL = 48, NE = 128, TOPK = 8, NH = 32, NKV = 4, HD = 128, QD = 4096, KVD = 512, FF = 768, GU_N = 1536, DN_N = 2048, HDR = 4096, MAXPOS = 800 };
#define GU_W ((size_t)GU_N * D / 2)
#define GU_S ((size_t)GU_N * D / 16)
#define DN_W ((size_t)DN_N * FF / 2)
#define DN_S ((size_t)DN_N * FF / 16)
#define LAYER_STRIDE ((size_t)NE * (GU_W + GU_S + 4 + DN_W + DN_S + 4))

static float g_e4m3[256], g_fp4v[16], g_sc4[128];
typedef struct { const uint8_t *w; const uint16_t *s; } F8;
typedef struct { const uint16_t *in_ln, *post_ln, *qn, *kn, *router; F8 q, k, v, o; F8 eg[NE], eu[NE], ed[NE]; const uint8_t *gw, *gs, *dw, *ds; const float *gt, *dt; } Layer;
static Layer L[NL];
static const uint16_t *g_embed, *g_final, *g_head;
static int g_threads = 8;

static F8 f8(const StIndex *ix, int l, const char *what, int e, const char *proj) {
    char nm[160], ns[176]; F8 r;
    if (e >= 0) snprintf(nm, sizeof nm, "model.layers.%d.mlp.experts.%d.%s.weight", l, e, proj); else snprintf(nm, sizeof nm, "model.layers.%d.%s.weight", l, what);
    snprintf(ns, sizeof ns, "%s_scale_inv", nm);
    const StTensor *w = st_get(ix, nm), *s = st_get(ix, ns);
    if (!w || !s) { fprintf(stderr, "missing %s\n", nm); exit(1); }
    r.w = w->data; r.s = (const uint16_t *)s->data; return r;
}
static const uint16_t *bf(const StIndex *ix, const char *fmt, int l) {
    char nm[160]; snprintf(nm, sizeof nm, fmt, l); const StTensor *t = st_get(ix, nm); if (!t) { fprintf(stderr, "missing %s\n", nm); exit(1); } return (const uint16_t *)t->data;
}

/* y[r] = sum_c W[r][c] x[c], FP8 block-128, rows [r0,r1) */
static void mv8(F8 w, uint32_t cols, uint32_t r0, uint32_t r1, const float *x, float *y) {
    uint32_t sb = cols / 128;
    for (uint32_t r = r0; r < r1; r++) {
        const uint8_t *row = w.w + (size_t)r * cols; float acc = 0;
        for (uint32_t b = 0; b < sb; b++) {
            float a = 0; const uint8_t *p = row + b * 128; const float *xx = x + b * 128;
            for (int c = 0; c < 128; c++) a += g_e4m3[p[c]] * xx[c];
            acc += a * st_bf16(w.s[(r / 128) * sb + b]);
        }
        y[r] = acc;
    }
}
static void mv8p(F8 w, uint32_t rows, uint32_t cols, const float *x, float *y) {
    #pragma omp parallel for schedule(static) num_threads(g_threads)
    for (uint32_t r = 0; r < rows; r += 16) mv8(w, cols, r, r + 16 < rows ? r + 16 : rows, x, y);
}
/* FP4 expert matrix rows [0,rows) */
static void mv4(const uint8_t *w, const uint8_t *s, float ts, uint32_t rows, uint32_t k, const float *x, float *y) {
    uint32_t nb = k / 16;
    for (uint32_t r = 0; r < rows; r++) {
        const uint8_t *row = w + (size_t)r * (k / 2); const uint8_t *sr = s + (size_t)r * nb; float acc = 0;
        for (uint32_t b = 0; b < nb; b++) {
            float a = 0; const uint8_t *p = row + b * 8; const float *xx = x + b * 16;
            for (int i = 0; i < 8; i++) a += g_fp4v[p[i] & 15] * xx[2 * i] + g_fp4v[p[i] >> 4] * xx[2 * i + 1];
            acc += a * g_sc4[sr[b] & 127];
        }
        y[r] = acc * ts;
    }
}
static void rmsnorm(const float *x, const uint16_t *w, int n, float *out) {
    double ss = 0; for (int i = 0; i < n; i++) ss += (double)x[i] * x[i];
    float r = 1.0f / sqrtf((float)(ss / n) + 1e-6f); for (int i = 0; i < n; i++) out[i] = x[i] * r * st_bf16(w[i]);
}
static void headnorm(float *x, const uint16_t *w, int nh) {
    for (int h = 0; h < nh; h++) { float *p = x + h * HD; double ss = 0; for (int i = 0; i < HD; i++) ss += (double)p[i] * p[i]; float r = 1.0f / sqrtf((float)(ss / HD) + 1e-6f); for (int i = 0; i < HD; i++) p[i] = p[i] * r * st_bf16(w[i]); }
}
static void rope(float *x, int nh, int pos) {
    for (int h = 0; h < nh; h++) { float *p = x + h * HD; for (int i = 0; i < HD / 2; i++) {
        double f = pow(1.0e7, -2.0 * i / HD), a = pos * f; float c = (float)cos(a), s = (float)sin(a); float x0 = p[i], x1 = p[i + HD / 2];
        p[i] = x0 * c - x1 * s; p[i + HD / 2] = x1 * c + x0 * s; } }
}

typedef struct { float *kc, *vc; float h[D]; } Stream;
static void stream_init(Stream *s) { s->kc = calloc((size_t)NL * MAXPOS * KVD, 4); s->vc = calloc((size_t)NL * MAXPOS * KVD, 4); }

static void silu_mul(float *g, const float *u, int n) { for (int i = 0; i < n; i++) g[i] = g[i] / (1.0f + expf(-g[i])) * u[i]; }
/* MoE for the chosen experts; mode 0 = FP8 checkpoint, 1 = FP4 store. out[D] */
static void moe(int l, int mode, const int *ids, const float *wt, const float *xn, float *out) {
    float acc[TOPK][D]; Layer *La = &L[l];
    #pragma omp parallel for schedule(static, 1) num_threads(g_threads)
    for (int j = 0; j < TOPK; j++) {
        int e = ids[j]; float gu[GU_N], hh[FF];
        if (mode == 0) { mv8(La->eg[e], D, 0, FF, xn, gu); mv8(La->eu[e], D, 0, FF, xn, gu + FF); }
        else mv4(La->gw + (size_t)e * GU_W, La->gs + (size_t)e * GU_S, La->gt[e], GU_N, D, xn, gu);
        memcpy(hh, gu, sizeof hh); silu_mul(hh, gu + FF, FF);
        if (mode == 0) mv8(La->ed[e], FF, 0, DN_N, hh, acc[j]); else mv4(La->dw + (size_t)e * DN_W, La->ds + (size_t)e * DN_S, La->dt[e], DN_N, FF, hh, acc[j]);
    }
    for (int i = 0; i < D; i++) { float a = 0; for (int j = 0; j < TOPK; j++) a += wt[j] * acc[j][i]; out[i] = a; }
}

typedef struct { double rel_sum, cos_sum, rel_max; long n; } LayerStat;
static LayerStat g_ls[NL];

/* one token through one layer; probe != 0 also computes the FP4 MoE on the same input for the layer statistics */
static void layer_fwd(Stream *s, int l, int pos, int mode, int probe) {
    Layer *La = &L[l]; float xn[D], q[QD], k[KVD], v[KVD], att[QD], ao[D];
    rmsnorm(s->h, La->in_ln, D, xn);
    mv8p(La->q, QD, D, xn, q); mv8p(La->k, KVD, D, xn, k); mv8p(La->v, KVD, D, xn, v);
    headnorm(q, La->qn, NH); headnorm(k, La->kn, NKV); rope(q, NH, pos); rope(k, NKV, pos);
    float *kc = s->kc + ((size_t)l * MAXPOS) * KVD, *vc = s->vc + ((size_t)l * MAXPOS) * KVD;
    memcpy(kc + (size_t)pos * KVD, k, sizeof k); memcpy(vc + (size_t)pos * KVD, v, sizeof v);
    #pragma omp parallel for schedule(static) num_threads(g_threads)
    for (int h = 0; h < NH; h++) {
        int kv = h / (NH / NKV); const float *qh = q + h * HD; float sc[MAXPOS], mx = -1e30f;
        for (int t = 0; t <= pos; t++) { const float *kh = kc + (size_t)t * KVD + kv * HD; float d = 0; for (int i = 0; i < HD; i++) d += qh[i] * kh[i]; d *= 0.08838834764831845f; sc[t] = d; if (d > mx) mx = d; }
        float den = 0; for (int t = 0; t <= pos; t++) { sc[t] = expf(sc[t] - mx); den += sc[t]; }
        float *o = att + h * HD; for (int i = 0; i < HD; i++) o[i] = 0;
        for (int t = 0; t <= pos; t++) { const float *vh = vc + (size_t)t * KVD + kv * HD; float w = sc[t] / den; for (int i = 0; i < HD; i++) o[i] += w * vh[i]; }
    }
    mv8p(La->o, D, QD, att, ao);
    for (int i = 0; i < D; i++) s->h[i] += ao[i];
    rmsnorm(s->h, La->post_ln, D, xn);
    float lg[NE], mx = -1e30f, den = 0; int ids[TOPK]; float wt[TOPK];
    for (int e = 0; e < NE; e++) { const uint16_t *r = La->router + (size_t)e * D; float d = 0; for (int i = 0; i < D; i++) d += st_bf16(r[i]) * xn[i]; lg[e] = d; if (d > mx) mx = d; }
    for (int e = 0; e < NE; e++) { lg[e] = expf(lg[e] - mx); den += lg[e]; }
    for (int e = 0; e < NE; e++) lg[e] /= den;
    { float tmp[NE]; memcpy(tmp, lg, sizeof tmp); float sum = 0; for (int j = 0; j < TOPK; j++) { int b = 0; for (int e = 1; e < NE; e++) if (tmp[e] > tmp[b]) b = e; ids[j] = b; wt[j] = tmp[b]; sum += tmp[b]; tmp[b] = -1; } for (int j = 0; j < TOPK; j++) wt[j] /= sum; }
    float mo[D]; moe(l, mode, ids, wt, xn, mo);
    if (probe) {
        float m4[D]; moe(l, 1, ids, wt, xn, m4); double nn = 0, dd = 0, dot = 0, n4 = 0;
        for (int i = 0; i < D; i++) { double d = (double)m4[i] - mo[i]; nn += d * d; dd += (double)mo[i] * mo[i]; dot += (double)mo[i] * m4[i]; n4 += (double)m4[i] * m4[i]; }
        double rel = sqrt(nn / dd), cs = dot / sqrt(dd * n4); LayerStat *a = &g_ls[l]; a->rel_sum += rel; a->cos_sum += cs; if (rel > a->rel_max) a->rel_max = rel; a->n++;
    }
    for (int i = 0; i < D; i++) s->h[i] += mo[i];
}
static void logits_fwd(Stream *s, float *lg) {
    float xn[D]; rmsnorm(s->h, g_final, D, xn);
    #pragma omp parallel for schedule(static) num_threads(g_threads)
    for (int r = 0; r < 151936; r++) { const uint16_t *w = g_head + (size_t)r * D; float a = 0; for (int i = 0; i < D; i++) a += st_bf16(w[i]) * xn[i]; lg[r] = a; }
}
static void embed(Stream *s, int tok) { for (int i = 0; i < D; i++) s->h[i] = st_bf16(g_embed[(size_t)tok * D + i]); }

static int parse_list(const char *s, int *out, int cap) { int n = 0; while (*s >= '0' && *s <= '9' && n < cap) { out[n++] = (int)strtol(s, (char **)&s, 10); if (*s == ',') s++; } return n; }

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: fp4_study <ckpt> <store> <prompts.tsv> [--only a,b] [--max-pos N] [--out f.json] [--threads N]\n"); return 2; }
    const char *only = NULL, *outp = NULL; int maxpos = 1 << 30;
    for (int i = 4; i < argc; i++) { if (!strcmp(argv[i], "--only") && i + 1 < argc) only = argv[++i]; else if (!strcmp(argv[i], "--max-pos") && i + 1 < argc) maxpos = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) outp = argv[++i]; else if (!strcmp(argv[i], "--threads") && i + 1 < argc) g_threads = atoi(argv[++i]); }
    for (int b = 0; b < 256; b++) g_e4m3[b] = st_e4m3((uint8_t)b);
    for (int c = 0; c < 16; c++) g_fp4v[c] = omega_fp4_value((uint8_t)c);
    for (int b = 0; b < 128; b++) g_sc4[b] = b == 0x7F ? 0.0f : omega_fp4_e4m3_unsigned((uint8_t)b);
    StIndex ix = { 0 }; if (st_open_dir(&ix, argv[1])) return 1;
    int fd = open(argv[2], O_RDONLY); struct stat sb; fstat(fd, &sb); uint8_t *store = mmap(NULL, (size_t)sb.st_size, PROT_READ, MAP_SHARED, fd, 0); close(fd);
    if (store == MAP_FAILED || memcmp(store, "AFP4v1", 6) != 0 || (size_t)sb.st_size != HDR + NL * LAYER_STRIDE) { fprintf(stderr, "bad store (size %lld, want %zu)\n", (long long)sb.st_size, (size_t)(HDR + NL * LAYER_STRIDE)); return 1; }
    g_embed = (const uint16_t *)st_get(&ix, "model.embed_tokens.weight")->data; g_final = (const uint16_t *)st_get(&ix, "model.norm.weight")->data; g_head = (const uint16_t *)st_get(&ix, "lm_head.weight")->data;
    for (int l = 0; l < NL; l++) {
        Layer *La = &L[l];
        La->in_ln = bf(&ix, "model.layers.%d.input_layernorm.weight", l); La->post_ln = bf(&ix, "model.layers.%d.post_attention_layernorm.weight", l);
        La->qn = bf(&ix, "model.layers.%d.self_attn.q_norm.weight", l); La->kn = bf(&ix, "model.layers.%d.self_attn.k_norm.weight", l); La->router = bf(&ix, "model.layers.%d.mlp.gate.weight", l);
        La->q = f8(&ix, l, "self_attn.q_proj", -1, NULL); La->k = f8(&ix, l, "self_attn.k_proj", -1, NULL); La->v = f8(&ix, l, "self_attn.v_proj", -1, NULL); La->o = f8(&ix, l, "self_attn.o_proj", -1, NULL);
        for (int e = 0; e < NE; e++) { La->eg[e] = f8(&ix, l, NULL, e, "gate_proj"); La->eu[e] = f8(&ix, l, NULL, e, "up_proj"); La->ed[e] = f8(&ix, l, NULL, e, "down_proj"); }
        uint8_t *p = store + HDR + (size_t)l * LAYER_STRIDE; La->gw = p; La->gs = p + (size_t)NE * GU_W; La->gt = (const float *)(La->gs + (size_t)NE * GU_S);
        La->dw = (const uint8_t *)(La->gt + NE); La->ds = La->dw + (size_t)NE * DN_W; La->dt = (const float *)(La->ds + (size_t)NE * DN_S);
        if (omega_fp4_scan_scales(La->gs, (size_t)NE * GU_S) || omega_fp4_scan_scales(La->ds, (size_t)NE * DN_S)) { fprintf(stderr, "layer %d: illegal scale in store\n", l); return 1; }
    }
    FILE *tsv = fopen(argv[3], "r"); if (!tsv) { perror(argv[3]); return 1; }
    FILE *out = outp ? fopen(outp, "w") : NULL; if (out) fprintf(out, "{\"prompts\":[");
    char *line = malloc(1 << 20); int first = 1; long tot_pos = 0, tot_agree = 0, tot_gen = 0, tot_gen_fp4_ok = 0, tot_gen_fp8_ok = 0; double min_cos_all = 1.0;
    float *lg0 = malloc(151936 * 4), *lg1 = malloc(151936 * 4);
    while (fgets(line, 1 << 20, tsv)) {
        char name[64]; char *t1 = strchr(line, '\t'), *t2 = t1 ? strchr(t1 + 1, '\t') : NULL; if (!t2) continue;
        *t1 = 0; *t2 = 0; snprintf(name, sizeof name, "%s", line);
        if (only && !strstr(only, name)) continue;
        static int pt[MAXPOS], gd[64]; int np = parse_list(t1 + 1, pt, MAXPOS), ng = parse_list(t2 + 1, gd, 64);
        int seq[MAXPOS]; int n = 0; for (int i = 0; i < np; i++) seq[n++] = pt[i];
        int feed_gen = (ng && gd[ng - 1] == 151645 && ng < 16) ? ng - 1 : ng - 1; /* the last golden token is only predicted, never fed */
        for (int i = 0; i < feed_gen; i++) seq[n++] = gd[i];
        if (n > maxpos) n = maxpos; if (n > MAXPOS) { fprintf(stderr, "sequence too long\n"); return 1; }
        Stream *s0 = calloc(1, sizeof(Stream)), *s1 = calloc(1, sizeof(Stream)); stream_init(s0); stream_init(s1);
        long agree = 0, gen = 0, g4 = 0, g8 = 0; double cmin = 1, csum = 0; double t0 = omp_get_wtime(); int flips = 0; double flip_margin_min = 1e30;
        if (out) fprintf(out, "%s{\"name\":\"%s\",\"prompt_tokens\":%d,\"positions\":%d,\"per_position\":[", first ? "" : ",", name, np, n);
        first = 0;
        for (int pos = 0; pos < n; pos++) {
            embed(s0, seq[pos]); embed(s1, seq[pos]);
            for (int l = 0; l < NL; l++) { layer_fwd(s0, l, pos, 0, 1); layer_fwd(s1, l, pos, 1, 0); }
            logits_fwd(s0, lg0); logits_fwd(s1, lg1);
            double dot = 0, a = 0, b = 0; int am0 = 0, am1 = 0; for (int i = 0; i < 151936; i++) { dot += (double)lg0[i] * lg1[i]; a += (double)lg0[i] * lg0[i]; b += (double)lg1[i] * lg1[i]; if (lg0[i] > lg0[am0]) am0 = i; if (lg1[i] > lg1[am1]) am1 = i; }
            double cs = dot / sqrt(a * b); float second = -1e30f; for (int i = 0; i < 151936; i++) if (i != am0 && lg0[i] > second) second = lg0[i];
            double margin = lg0[am0] - second; int ok = am0 == am1; agree += ok; csum += cs; if (cs < cmin) cmin = cs;
            if (!ok) { flips++; if (margin < flip_margin_min) flip_margin_min = margin; }
            int gi = pos - (np - 1); /* position predicting golden[gi] */
            if (gi >= 0 && gi < ng) { gen++; g8 += am0 == gd[gi]; g4 += am1 == gd[gi]; }
            if (out) fprintf(out, "%s{\"pos\":%d,\"cos\":%.7f,\"fp8_top1\":%d,\"fp4_top1\":%d,\"fp8_margin\":%.4f%s}", pos ? "," : "", pos, cs, am0, am1, margin, gi >= 0 && gi < ng ? ",\"gen\":true" : "");
            if (pos % 20 == 0 || pos == n - 1) { fprintf(stderr, "[%s] pos %d/%d agree %ld/%d cos %.6f (%.0fs)\n", name, pos + 1, n, agree, pos + 1, cs, omp_get_wtime() - t0); }
        }
        printf("%s: positions %d, top-1 agree %ld (%.2f%%), logits cosine mean %.6f min %.6f, flips %d (smallest FP8 margin at a flip %.3f); generated positions %ld: FP8 matches golden %ld, FP4 matches golden %ld\n",
               name, n, agree, 100.0 * agree / n, csum / n, cmin, flips, flips ? flip_margin_min : 0.0, gen, g8, g4);
        fflush(stdout);
        if (out) fprintf(out, "],\"top1_agree\":%ld,\"cos_mean\":%.7f,\"cos_min\":%.7f,\"gen_positions\":%ld,\"fp8_matches_golden\":%ld,\"fp4_matches_golden\":%ld,\"seconds\":%.1f}", agree, csum / n, cmin, gen, g8, g4, omp_get_wtime() - t0);
        tot_pos += n; tot_agree += agree; tot_gen += gen; tot_gen_fp4_ok += g4; tot_gen_fp8_ok += g8; if (cmin < min_cos_all) min_cos_all = cmin;
        free(s0->kc); free(s0->vc); free(s1->kc); free(s1->vc); free(s0); free(s1);
    }
    printf("TOTAL: positions %ld, top-1 agree %ld (%.3f%%), min logits cosine %.6f; generated positions %ld: FP8 matches golden %ld, FP4 matches golden %ld\n", tot_pos, tot_agree, tot_pos ? 100.0 * tot_agree / tot_pos : 0.0, min_cos_all, tot_gen, tot_gen_fp8_ok, tot_gen_fp4_ok);
    printf("per-layer MoE output error of FP4 experts vs FP8 experts on the FP8 stream's real activations (mean over positions):\n");
    if (out) fprintf(out, "],\"layers\":[");
    for (int l = 0; l < NL; l++) { LayerStat *a = &g_ls[l]; if (!a->n) continue; printf("  layer %2d: rel L2 %.4f (max %.4f) cosine %.6f\n", l, a->rel_sum / a->n, a->rel_max, a->cos_sum / a->n);
        if (out) fprintf(out, "%s{\"layer\":%d,\"rel_l2_mean\":%.5f,\"rel_l2_max\":%.5f,\"cos_mean\":%.7f,\"n\":%ld}", l ? "," : "", l, a->rel_sum / a->n, a->rel_max, a->cos_sum / a->n, a->n); }
    if (out) { fprintf(out, "],\"total_positions\":%ld,\"top1_agree\":%ld,\"gen_positions\":%ld,\"fp8_matches_golden\":%ld,\"fp4_matches_golden\":%ld}\n", tot_pos, tot_agree, tot_gen, tot_gen_fp8_ok, tot_gen_fp4_ok); fclose(out); }
    return 0;
}
