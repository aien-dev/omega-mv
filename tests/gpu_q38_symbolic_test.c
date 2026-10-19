/*
 * gpu_q38_symbolic_test -- OMEGA_SYMBOLIC_REWRITE workload 2: the Qwen3.8-27B decode graph (omega_gpu_q38_graph).
 *
 * The rewrite layer reads the decode token as a launch list (kind, buffers read and written, measured time) and
 * proposes rewrites; each one is checked, never assumed:
 *   1. barrier elimination: the launch list becomes an AgGraph (one AG_PHYSICAL node per launch, a data edge per
 *      RAW / WAR / WAW hazard, an order edge per recorded barrier) and runs through rx_graph_optimize_ex with the
 *      symbolic pass; a recorded barrier with no hazard under it is droppable.
 *   2. gather fold: GATHER(GEMV_fp8(W, x), P) -> GEMV_fp8(P W, x). Side condition on the REAL checkpoint's block scales
 *      (read from the safetensors files; --model DIR), and on a synthetic model whose scales satisfy it, where the
 *      folded graph is run on the chip and compared with the unfolded one byte for byte, then timed (interleaved).
 *   3. FP8 lm_head (bf16 -> e4m3 128x128 block quantised): a machine-arithmetic change, so the verifier must see it on
 *      the chip; logits compared byte for byte with the bf16 head.
 *   --host-only: steps 1 (with the timeline of an earlier run absent: unit times) and 2 on the checkpoint only.
 *   Options: --layers N (4) --mask M (0x8) --reps R (21 timed tokens per arm, interleaved) --model DIR --out DIR
 * Exit 0 when every CHECK passed. The verdict per rewrite (applied / refused / refuted, and why) is in the trace.
 */
#include "runtime/rx_symbolic.h"

#define main q38_graph_test_main
#include "gpu_q38_graph_test.c"
#undef main

#include <errno.h>

/* ---- buffers the launches touch (bit numbers) ---- */
enum { B_X, B_XQ, B_MIX, B_AB, B_AGG, B_ATT, B_O, B_GU, B_ACT, B_HID, B_LOG, B_AM, B_TOK, B_GDN, B_KV };
#define BIT(b) (1u << (b))

static const uint8_t q2s[OMEGA_GPU_Q38_K_COUNT] = {
    [OMEGA_GPU_Q38_K_NORM] = RX_SYM_K_NORM, [OMEGA_GPU_Q38_K_MIX] = RX_SYM_K_GEMV_FP8, [OMEGA_GPU_Q38_K_AB] = RX_SYM_K_GEMV_BF16,
    [OMEGA_GPU_Q38_K_GDN] = RX_SYM_K_GDN, [OMEGA_GPU_Q38_K_GATHER] = RX_SYM_K_GATHER, [OMEGA_GPU_Q38_K_ATTN] = RX_SYM_K_ATTN,
    [OMEGA_GPU_Q38_K_GATE] = RX_SYM_K_GATE, [OMEGA_GPU_Q38_K_O] = RX_SYM_K_GEMV_FP8, [OMEGA_GPU_Q38_K_RES] = RX_SYM_K_ADDNORM,
    [OMEGA_GPU_Q38_K_GATE_UP] = RX_SYM_K_GEMV_FP8, [OMEGA_GPU_Q38_K_SWIGLU] = RX_SYM_K_SWIGLU, [OMEGA_GPU_Q38_K_DOWN] = RX_SYM_K_GEMV_FP8,
    [OMEGA_GPU_Q38_K_LM_HEAD] = RX_SYM_K_GEMV_BF16, [OMEGA_GPU_Q38_K_ARGMAX] = RX_SYM_K_ARGMAX };

typedef struct { uint8_t qkind; uint32_t r, w; uint8_t indep; uint64_t wbytes; } Desc;

/* The launch list omega_gpu_q38_graph_record_layer / record_final produce (overlap a|b on, no fold, bf16 head). */
static uint32_t describe(uint32_t layers, uint64_t mask, const uint8_t *folded, Desc *d) {
    uint32_t n = 0;
    const uint64_t H5 = 5120, I17 = 17408;
    d[n++] = (Desc){ OMEGA_GPU_Q38_K_NORM, BIT(B_X), BIT(B_XQ), 0, 0 };
    for (uint32_t l = 0; l < layers; l++) {
        const int attn = (int)((mask >> l) & 1u), last = l + 1 == layers;
        if (!attn) {
            d[n++] = (Desc){ OMEGA_GPU_Q38_K_AB, BIT(B_XQ), BIT(B_AB), 0, 96 * H5 * 2 };
            d[n++] = (Desc){ OMEGA_GPU_Q38_K_MIX, BIT(B_XQ), BIT(B_MIX), 1, 16384 * H5 };
            d[n++] = (Desc){ OMEGA_GPU_Q38_K_GDN, BIT(B_MIX) | BIT(B_AB) | BIT(B_GDN), BIT(B_ATT) | BIT(B_GDN), 0, 0 };
        } else {
            d[n++] = (Desc){ OMEGA_GPU_Q38_K_MIX, BIT(B_XQ), BIT(B_MIX), 0, 14336 * H5 };
            uint32_t agg = folded && folded[l] ? BIT(B_MIX) : BIT(B_AGG);
            if (!(folded && folded[l])) d[n++] = (Desc){ OMEGA_GPU_Q38_K_GATHER, BIT(B_MIX), BIT(B_AGG), 0, 0 };
            d[n++] = (Desc){ OMEGA_GPU_Q38_K_ATTN, agg | BIT(B_KV), BIT(B_ATT) | BIT(B_KV), 0, 0 };
            d[n++] = (Desc){ OMEGA_GPU_Q38_K_GATE, BIT(B_ATT) | agg, BIT(B_ATT), 0, 0 };
        }
        d[n++] = (Desc){ OMEGA_GPU_Q38_K_O, BIT(B_ATT), BIT(B_O), 0, H5 * 6144 };
        d[n++] = (Desc){ OMEGA_GPU_Q38_K_RES, BIT(B_X) | BIT(B_O), BIT(B_X) | BIT(B_XQ), 0, 0 };
        d[n++] = (Desc){ OMEGA_GPU_Q38_K_GATE_UP, BIT(B_XQ), BIT(B_GU), 0, 2 * I17 * H5 };
        d[n++] = (Desc){ OMEGA_GPU_Q38_K_SWIGLU, BIT(B_GU), BIT(B_ACT), 0, 0 };
        d[n++] = (Desc){ OMEGA_GPU_Q38_K_DOWN, BIT(B_ACT), BIT(B_O), 0, H5 * I17 };
        d[n++] = (Desc){ OMEGA_GPU_Q38_K_RES, BIT(B_X) | BIT(B_O), BIT(B_X) | (last ? BIT(B_HID) : BIT(B_XQ)), 0, 0 };
    }
    d[n++] = (Desc){ OMEGA_GPU_Q38_K_LM_HEAD, BIT(B_HID), BIT(B_LOG), 0, 248320ull * H5 * 2 };
    d[n++] = (Desc){ OMEGA_GPU_Q38_K_ARGMAX, BIT(B_LOG), BIT(B_AM), 0, 0 };
    d[n++] = (Desc){ OMEGA_GPU_Q38_K_ARGMAX, BIT(B_AM), BIT(B_TOK), 0, 0 };
    return n;
}

/* ---- the real checkpoint's block scales (safetensors header, bf16 [rows/128][cols/128]) ---- */
static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1); if (!b) { fclose(f); return NULL; }
    size_t got = fread(b, 1, (size_t)n, f); fclose(f); b[got] = 0; if (len) *len = got; return b;
}

static int read_scale(const char *dir, const char *index, const char *name, uint16_t *out, uint32_t cap, uint32_t *rows, uint32_t *cols) {
    char key[256]; snprintf(key, sizeof key, "\"%s\": \"", name);
    const char *p = strstr(index, key); if (!p) return -1;
    p += strlen(key);
    char file[256]; size_t k = 0; while (*p && *p != '"' && k + 1 < sizeof file) file[k++] = *p++; file[k] = 0;
    char path[1024]; snprintf(path, sizeof path, "%s/%s", dir, file);
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    uint64_t hl = 0; if (fread(&hl, 8, 1, f) != 1 || hl > (64u << 20)) { fclose(f); return -1; }
    char *h = malloc(hl + 1); if (!h || fread(h, 1, hl, f) != hl) { free(h); fclose(f); return -1; }
    h[hl] = 0;
    snprintf(key, sizeof key, "\"%s\":", name);
    const char *q = strstr(h, key);
    int rc = -1;
    unsigned long long a = 0, b = 0; unsigned r = 0, c = 0;
    if (q) {
        const char *s = strstr(q, "\"shape\""), *o = strstr(q, "\"data_offsets\""), *dt = strstr(q, "\"BF16\"");
        if (s && o && dt && sscanf(strchr(s, '['), "[%u,%u]", &r, &c) == 2 && sscanf(strchr(o, '['), "[%llu,%llu]", &a, &b) == 2 &&
            b - a == (unsigned long long)r * c * 2 && r * c <= cap) {
            if (fseek(f, (long)(8 + hl + a), SEEK_SET) == 0 && fread(out, 2, (size_t)r * c, f) == (size_t)r * c) { *rows = r; *cols = c; rc = 0; }
        }
    }
    free(h); fclose(f);
    return rc;
}

static RxSymStore g_st;
static RxSymTrace g_tr;

static void trace_rec(const char *rule, const char *where, int budget, int verdict, int applied, uint32_t cb, uint32_t ca,
                      double lb, double inc, const char *note) {
    RxSymTraceRec r; memset(&r, 0, sizeof r);
    snprintf(r.rule, sizeof r.rule, "%s", rule); snprintf(r.where, sizeof r.where, "%s", where);
    /* left / right side ids: kernel terms of the launch kinds involved */
    rx_sym_store_init(&g_st);
    RxSymType t = { RX_SYM_TY_FP8E4M3, RX_SYM_AR_BLOCK, 8, 14336, 5120 };
    RxSymTerm k; memset(&k, 0, sizeof k);
    k.head = RX_SYM_H_KERNEL; k.type = t; k.op = RX_SYM_K_GEMV_FP8; k.imm = (uint64_t)strlen(rule);
    uint32_t a = rx_sym_intern(&g_st, &k);
    k.op = RX_SYM_K_GATHER; k.n_args = 1; k.arg[0] = a;
    uint32_t b = rx_sym_intern(&g_st, &k);
    r.lhs = g_st.t[b].id; r.rhs = g_st.t[a].id;
    r.budget = budget; r.verdict.verdict = verdict; r.applied = applied; r.cost_before = cb; r.cost_after = ca; r.lb_ns = lb; r.incumbent_ns = inc;
    snprintf(r.note, sizeof r.note, "%s", note ? note : "");
    rx_sym_trace_add(&g_tr, &r);
}

/* Real checkpoint: every attention layer's mix scales (q_proj 96 rows | k_proj 8 | v_proj 8 = 112 block rows). */
static int checkpoint_gather(const char *dir, FILE *tsv) {
    char ip[1024]; snprintf(ip, sizeof ip, "%s/model.safetensors.index.json", dir);
    char *index = slurp(ip, NULL);
    if (!index) { printf("checkpoint: %s not readable (%s)\n", ip, strerror(errno)); return -1; }
    static uint32_t tab[OMEGA_GPU_Q38_ATT_MIX];
    omega_q38_gather_table(tab);
    static uint16_t sc[112 * 40], part[96 * 40];
    int eligible = 0, layers = 0;
    for (uint32_t l = 3; l < 64; l += 4) {
        static const char *proj[3] = { "q_proj", "k_proj", "v_proj" };
        uint32_t off = 0, ok = 1, cols = 0;
        for (int p = 0; p < 3 && ok; p++) {
            char name[160]; uint32_t r = 0, c = 0;
            snprintf(name, sizeof name, "model.language_model.layers.%u.self_attn.%s.weight_scale_inv", l, proj[p]);
            if (read_scale(dir, index, name, part, 96 * 40, &r, &c) != 0 || c != 40) { ok = 0; break; }
            memcpy(sc + (size_t)off * 40, part, (size_t)r * c * 2); off += r; cols = c;
        }
        if (!ok || off != 112) { printf("checkpoint layer %u: scales not read\n", l); free(index); return -1; }
        uint32_t bad = 0; char why[200];
        int e = rx_sym_gather_fold_check(tab, OMEGA_GPU_Q38_ATT_MIX, sc, 14336, cols * 128, &bad, why, sizeof why);
        layers++; eligible += e;
        printf("checkpoint layer %2u: gather fold %s (%s)\n", l, e ? "ELIGIBLE" : "refused", why);
        if (tsv) fprintf(tsv, "checkpoint\t%u\t%s\t%u\t%s\n", l, e ? "eligible" : "refused", bad, why);
        char where[48]; snprintf(where, sizeof where, "checkpoint layer %u", l);
        trace_rec("gather-fold/block-scales", where, RX_SYM_B_ADMIT, 0, 0, 1, 1, 0, 0, why);
    }
    free(index);
    printf("checkpoint: gather fold eligible in %d of %d attention layers\n", eligible, layers);
    return eligible;
}

/* ---- synthetic model, attention mix tensors with scales that satisfy the fold ---- */
static void fp8_host(uint32_t seed, uint32_t n, uint32_t k, uint8_t *w, uint16_t *s) {
    size_t wb = (size_t)n * k, sb = (size_t)(n / 128) * (k / 128);
    uint32_t st = seed;
    for (size_t i = 0; i < wb; i += 4) { uint32_t r = lcg(&st); for (unsigned j = 0; j < 4 && i + j < wb; j++) w[i + j] = (uint8_t)((r >> (8 * j)) & 0xbfu); }
    const float sc = 1.5f / sqrtf((float)k);
    for (size_t i = 0; i < sb; i++) s[i] = bf16_rne(frand(&st, 0.5f * sc, sc));
    /* the fold's side condition: every block row holds block row 0's scales */
    const size_t cb = k / 128;
    for (size_t r = 1; r < n / 128; r++) memcpy(s + r * cb, s, cb * 2);
}

typedef struct { uint64_t span[256]; uint64_t kind[256][OMEGA_GPU_Q38_K_COUNT]; uint32_t n; } Tl;

static int stamped(OmegaGpuQ38Graph *g, Model *m, Tl *tl, uint8_t *kinds_out, int *n_out) {
    static uint64_t stp[OMEGA_GPU_CMDLIST_MAX_LAUNCHES + 1]; static uint8_t kd[OMEGA_GPU_CMDLIST_MAX_LAUNCHES];
    static Out M; OmegaGpuDecodeGraphStats st;
    omega_gpu_q38_graph_set_stamps(g, 1);
    if (state_reset(m) != 0 || graph_token(g, m, m->x0[0], 2, &M, &st) != 0) return -1;
    int n = omega_gpu_q38_graph_stamps(g, stp, kd, OMEGA_GPU_CMDLIST_MAX_LAUNCHES + 1);
    omega_gpu_q38_graph_set_stamps(g, 0);
    if (n <= 0) return -1;
    if (tl && tl->n < 256) {
        for (int i = 0; i < n; i++) tl->kind[tl->n][kd[i]] += stp[i + 1] - stp[i];
        tl->span[tl->n++] = stp[n] - stp[0];
    }
    if (kinds_out) memcpy(kinds_out, kd, (size_t)n);
    if (n_out) *n_out = n;
    return 0;
}

static double med_kind(Tl *t, int k) {
    static uint64_t c[256];
    for (uint32_t i = 0; i < t->n; i++) c[i] = k < 0 ? t->span[i] : t->kind[i][k];
    return (double)median_u64(c, t->n) / 1e3;
}

int main(int argc, char **argv) {
    uint32_t layers = 4, reps = 21; uint64_t mask = 0x8; int host = 0;
    const char *model = NULL, *out = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host-only") == 0) host = 1;
        else if (strcmp(argv[i], "--layers") == 0 && i + 1 < argc) layers = (uint32_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--mask") == 0 && i + 1 < argc) mask = strtoull(argv[++i], NULL, 0);
        else if (strcmp(argv[i], "--reps") == 0 && i + 1 < argc) reps = (uint32_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--model") == 0 && i + 1 < argc) model = argv[++i];
        else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) out = argv[++i];
        else { printf("usage: %s [--host-only] [--layers N] [--mask M] [--reps R] [--model DIR] [--out DIR]\n", argv[0]); return 2; }
    }
    if (layers == 0 || layers > 6 || reps < 3 || reps > 255) { printf("layers 1..6, reps 3..255\n"); return 2; }
    char path[1024];
    FILE *tsv = NULL;
    if (out) { snprintf(path, sizeof path, "%s/workload-q38.tsv", out); tsv = fopen(path, "w"); }
    if (tsv) fprintf(tsv, "section\titem\tbaseline\trewrite\tdetail\n");

    /* 2a. the real checkpoint */
    int ck = -2;
    if (model) ck = checkpoint_gather(model, tsv);
    CHECK(!model || ck >= 0, "checkpoint scales read (%d eligible)", ck);

    /* 1. barriers, with launch times from the chip timeline when there is one */
    static Desc d[64];
    uint32_t nd = describe(layers, mask, NULL, d);
    RxSymLaunchList ll; memset(&ll, 0, sizeof ll);
    ll.n = nd;
    for (uint32_t i = 0; i < nd; i++) {
        ll.l[i].kind = q2s[d[i].qkind]; ll.l[i].reads = d[i].r; ll.l[i].writes = d[i].w;
        ll.l[i].weight_bytes = d[i].wbytes; ll.l[i].marked_independent = d[i].indep; ll.l[i].us = 1;
    }
    Model m; OmegaGpuQ38Graph *G = NULL, *F = NULL;
    Tl *tg = calloc(1, sizeof *tg), *tf = calloc(1, sizeof *tf);
    static uint8_t kinds[OMEGA_GPU_CMDLIST_MAX_LAUNCHES];
    int nk = 0;
    if (!host) {
        if (model_build(&m, layers, mask, 248320u) != 0) { CHECK(0, "model build"); return 1; }
        /* replace each attention layer's mix with an eligible tensor, and build its folded copy */
        static OmegaGpuFp8Tensor *fold[8];
        static uint32_t tab[OMEGA_GPU_Q38_ATT_MIX];
        omega_q38_gather_table(tab);
        const uint32_t n = OMEGA_GPU_Q38_ATT_MIX, k = H, cb = k / 128;
        uint8_t *w = malloc((size_t)n * k), *w2 = malloc((size_t)n * k);
        uint16_t *s = malloc((size_t)(n / 128) * cb * 2), *s2 = malloc((size_t)(n / 128) * cb * 2);
        for (uint32_t l = 0; l < layers; l++) {
            if (!((mask >> l) & 1u)) continue;
            fp8_host(100 + l, n, k, w, s);
            uint32_t bad = 0; char why[200];
            int e = rx_sym_gather_fold_check(tab, n, s, n, k, &bad, why, sizeof why);
            CHECK(e == 1, "synthetic layer %u: fold side condition holds (%s)", l, why);
            for (uint32_t r = 0; r < n; r++) memcpy(w2 + (size_t)r * k, w + (size_t)tab[r] * k, k);
            for (uint32_t b = 0; b < n / 128; b++) memcpy(s2 + (size_t)b * cb, s + (size_t)(tab[b * 128] / 128) * cb, cb * 2);
            omega_gpu_fp8_tensor_free(m.L[l].mix);
            if (omega_gpu_fp8_tensor_upload(1, n, k, w, s, &m.L[l].mix) != 0 || omega_gpu_fp8_tensor_upload(1, n, k, w2, s2, &fold[l]) != 0) {
                CHECK(0, "upload: %s", omega_gpu_fp8_last_error()); return 1; }
        }
        free(w); free(w2); free(s); free(s2);
        /* warm the kernels the record paths need (one token through the APIs) */
        static Out A0; if (baseline_token(&m, m.x0[0], 0, &A0) != 0) { CHECK(0, "warm"); return 1; }
        { static OmegaGpuFp8Tensor *keep[8]; for (uint32_t l = 0; l < layers; l++) if (fold[l]) { keep[l] = m.L[l].mix; m.L[l].mix = fold[l]; }
          static float tmp[OMEGA_GPU_Q38_ATT_MIX]; for (uint32_t l = 0; l < layers; l++) if (fold[l]) omega_gpu_fp8_gemv_f32(fold[l], 0, m.x0[0], tmp, NULL);
          for (uint32_t l = 0; l < layers; l++) if (fold[l]) m.L[l].mix = keep[l]; }
        if (graph_build(&m, &G) != 0) { CHECK(0, "graph G"); return 1; }
        if (graph_build(&m, &F) != 0) { CHECK(0, "graph F"); return 1; }
        for (uint32_t l = 0; l < layers; l++) if (fold[l]) {
            CHECK(omega_gpu_q38_graph_set_layer(F, l, fold[l], m.L[l].ab, m.L[l].o, m.L[l].gu, m.L[l].dn, m.L[l].in_norm, m.L[l].post_norm) == 0 &&
                  omega_gpu_q38_graph_set_gather_folded(F, l, 1) == 0, "graph F layer %u folded", l);
        }
        /* the real recorded kind sequence must be the one the rewrite layer reasons about */
        if (stamped(G, &m, NULL, kinds, &nk) != 0) { CHECK(0, "stamped token"); return 1; }
        int same = nk == (int)nd;
        for (int i = 0; same && i < nk; i++) same = kinds[i] == d[i].qkind;
        CHECK(same, "rewrite layer's launch list matches the recorded list (%d launches)", nk);

        /* 2b. fold verified bit for bit: three tokens through both graphs from the same state */
        static Out OG[TOKENS], OF;
        int equal = 1;
        if (state_reset(&m) != 0) return 1;
        OmegaGpuDecodeGraphStats st, stf;
        for (uint32_t t = 0; t < TOKENS; t++) if (graph_token(G, &m, m.x0[t], t, &OG[t], &st) != 0) { CHECK(0, "G token"); return 1; }
        if (state_reset(&m) != 0) return 1;
        for (uint32_t t = 0; t < TOKENS; t++) {
            if (graph_token(F, &m, m.x0[t], t, &OF, &stf) != 0) { CHECK(0, "F token"); return 1; }
            if (memcmp(OF.x, OG[t].x, sizeof OF.x) || memcmp(OF.logits, OG[t].logits, 248320u * 4u) || OF.token != OG[t].token) equal = 0;
        }
        CHECK(stf.launches + m.n_attn == st.launches, "folded list has %u launches, %u fewer", stf.launches, st.launches - stf.launches);
        printf("gather fold on chip: x, logits and token %s over %u tokens\n", equal ? "BYTE-IDENTICAL" : "DIFFERENT", TOKENS);

        /* 2c. timing, interleaved G / F */
        for (uint32_t r = 0; r < reps; r++) {
            if (stamped(r & 1u ? F : G, &m, r & 1u ? tf : tg, NULL, NULL) != 0 || stamped(r & 1u ? G : F, &m, r & 1u ? tg : tf, NULL, NULL) != 0) {
                CHECK(0, "timed token %u", r); return 1; }
        }
        double sg = med_kind(tg, -1), sf = med_kind(tf, -1);
        printf("timeline span: baseline %.1f us, folded %.1f us (%u tokens each, interleaved), gather %.2f us baseline\n", sg, sf, tg->n,
               med_kind(tg, OMEGA_GPU_Q38_K_GATHER));
        for (int kk = 0; kk < OMEGA_GPU_Q38_K_COUNT; kk++) {
            double a = med_kind(tg, kk), b = med_kind(tf, kk);
            if (a == 0 && b == 0) continue;
            printf("  %-8s baseline %9.1f us  folded %9.1f us\n", omega_gpu_q38_graph_kind_name((uint8_t)kk), a, b);
            if (tsv) fprintf(tsv, "timeline_us\t%s\t%.2f\t%.2f\tmedian of %u\n", omega_gpu_q38_graph_kind_name((uint8_t)kk), a, b, tg->n);
        }
        if (tsv) fprintf(tsv, "timeline_us\tspan\t%.2f\t%.2f\tmedian of %u interleaved stamped tokens\n", sg, sf, tg->n);
        if (tsv) fprintf(tsv, "verify\tgather-fold synthetic\t-\t%s\tx, logits, token over %u tokens\n", equal ? "byte-identical" : "DIFFERENT", TOKENS);
        trace_rec("gather-fold/synthetic-eligible", "attention layers (synthetic scales)", RX_SYM_B_ADMIT,
                  equal ? RX_SYM_V_CHIP_EQUAL : RX_SYM_V_CHIP_DIFFERENT, 0, st.launches, stf.launches, sf * 1e3, sg * 1e3,
                  "measured on the chip; applies to the real checkpoint only where its scales allow (see checkpoint records)");
        CHECK(equal, "gather fold is bit-exact on the chip (the FP8 GEMV is row independent)");
        for (uint32_t i = 0; i < nd; i++) {
            /* per-launch time: the kind's median share */
            uint32_t cnt = 0; for (uint32_t j = 0; j < nd; j++) cnt += d[j].qkind == d[i].qkind;
            ll.l[i].us = med_kind(tg, d[i].qkind) / (cnt ? cnt : 1);
        }

        /* 3. FP8 lm_head: logits byte compare */
        {
            uint16_t *wb = malloc((size_t)248320 * H * 2); uint8_t *w8 = malloc((size_t)248320 * H); uint16_t *s8 = malloc((size_t)(248320 / 128) * (H / 128) * 2);
            uint32_t sst = 600; for (size_t i = 0; i < (size_t)248320 * H; i++) wb[i] = bf16_rne(frand(&sst, -0.05f, 0.05f));
            OmegaGpuFp8Tensor *h8 = NULL;
            int qrc = omega_gpu_fp8_quantize_bf16(248320, H, wb, w8, s8);
            int urc = qrc ? -1 : omega_gpu_fp8_tensor_upload(1, 248320, H, w8, s8, &h8);
            free(wb); free(w8); free(s8);
            CHECK(urc == 0, "fp8 head built");
            static Out H0, H1;
            if (urc == 0) {
                omega_gpu_fp8_gemv_f32(h8, 0, m.x0[0], H1.logits, NULL);    /* warm */
                if (state_reset(&m) == 0 && graph_token(G, &m, m.x0[0], 0, &H0, &st) == 0 && omega_gpu_q38_graph_set_lm_head_fp8(G, h8) == 0 &&
                    state_reset(&m) == 0 && graph_token(G, &m, m.x0[0], 0, &H1, &st) == 0) {
                    uint32_t diff = 0; double worst = 0;
                    for (uint32_t i = 0; i < 248320u; i++) if (memcmp(&H0.logits[i], &H1.logits[i], 4)) { diff++; double dd = fabs((double)H0.logits[i] - H1.logits[i]); if (dd > worst) worst = dd; }
                    printf("fp8 lm_head: %u of 248320 logits differ (worst %.3g), token %u vs %u\n", diff, worst, H0.token, H1.token);
                    char note[96]; snprintf(note, sizeof note, "%u of 248320 logits differ, worst %.3g: refuted, not applied", diff, worst);
                    trace_rec("lm-head-fp8/requantize", "final", RX_SYM_B_ADMIT, diff ? RX_SYM_V_CHIP_DIFFERENT : RX_SYM_V_CHIP_EQUAL, 0, 1, 1, 0, 0, note);
                    if (tsv) fprintf(tsv, "verify\tlm-head-fp8\t-\t%s\t%u logits differ, worst %.3g\n", diff ? "DIFFERENT (refuted)" : "identical", diff, worst);
                    CHECK(diff > 0, "the verifier refutes the FP8 head as an exact rewrite");
                    omega_gpu_q38_graph_set_lm_head_fp8(G, NULL);
                } else CHECK(0, "fp8 head tokens");
                omega_gpu_fp8_tensor_free(h8);
            }
        }
    }

    /* 1 (continued): barrier analysis inside rx_graph_optimize */
    static RxSymPass pass;
    rx_sym_pass_init(&pass, &g_st, 64);
    RxSymBarrierReport br; AgReport rep;
    int brc = rx_sym_barriers(&ll, &pass, &br, &rep);
    CHECK(brc == 0, "barrier analysis ran (%d)", brc);
    printf("barriers: %u recorded edges, %u already dropped (a|b beside mix), %u needed, %u droppable; glue %.1f us of span %.1f us (%.2f%%)\n",
           br.barriers_total, br.already_dropped, br.barriers_needed, br.barriers_droppable, br.glue_us, br.span_us,
           br.span_us > 0 ? 100.0 * br.glue_us / br.span_us : 0.0);
    for (uint32_t i = 0; i < br.n_droppable_at; i++) printf("  droppable before launch %u (%s)\n", br.droppable_at[i], omega_gpu_q38_graph_kind_name(d[br.droppable_at[i]].qkind));
    if (tsv) fprintf(tsv, "barriers\trecorded\t%u\t%u\talready dropped %u, needed %u, droppable %u\n", br.barriers_total, br.barriers_total - br.barriers_droppable,
                     br.already_dropped, br.barriers_needed, br.barriers_droppable);
    if (tsv) fprintf(tsv, "bound\tglue_share_of_span\t%.4f\t-\tglue %.1f us, weight GEMVs %.1f us; the most any rewrite of glue can remove\n",
                     br.span_us > 0 ? br.glue_us / br.span_us : 0.0, br.glue_us, br.weight_us);
    {
        char note[96]; snprintf(note, sizeof note, "%u of %u recorded barriers carry no hazard", br.barriers_droppable, br.barriers_total);
        trace_rec("barrier-elimination/hazards", "whole token", RX_SYM_B_ADMIT, RX_SYM_V_PROVED, br.barriers_droppable > 0, br.barriers_total,
                  br.barriers_total - br.barriers_droppable, 0, 0, note);
    }
    if (out) {
        snprintf(path, sizeof path, "%s/trace-q38.evidence.json", out);
        FILE *f = fopen(path, "w");
        char concl[300], meas[300];
        snprintf(concl, sizeof concl, "Qwen3.8-27B decode graph (%u-layer prefix): rewrites the layer can verify touch only glue launches, %.2f%% of the token",
                 layers, br.span_us > 0 ? 100.0 * br.glue_us / br.span_us : 0.0);
        snprintf(meas, sizeof meas, "glue_us=%.1f=us;weight_gemv_us=%.1f=us;span_us=%.1f=us;droppable_barriers=%u=;checkpoint_fold_eligible_layers=%d=",
                 br.glue_us, br.weight_us, br.span_us, br.barriers_droppable, ck);
        if (f) { rx_sym_trace_json(&g_tr, "omega/gpu_q38_symbolic_test", "qwen38-decode", concl, meas, f); fclose(f); }
    }
    if (tsv) fclose(tsv);
    if (G) omega_gpu_q38_graph_free(G);
    if (F) omega_gpu_q38_graph_free(F);
    printf("gpu_q38_symbolic_test: %d checks, %d failed: %s\n", g_checks, g_failed, g_failed ? "FAIL" : "PASS");
    return g_failed ? 1 : 0;
}
