/*
 * rx_cascade_test -- M6 of OMEGA_SYMBOLIC_REWRITE: per-request kernel and draft-source selection for the
 * Qwen3.8-27B cascade, measured against fixed choices on identical requests.
 *
 *   rx_cascade_test --f7 DIR [--out DIR]
 *
 * DIR: the sovereign-core evidence folder qwen38-spec-20261019T0323Z (f7, sc#679): for three requests (codelong 256
 * tokens, prose 256, the golden code answer 30) every arm was run end to end on the chip by qwen38_spec under the
 * GB10 lock with receipts: the one-row decode graph, and the exact N-row verify path with the model's MTP head at
 * k = 0..4, each run byte-identical to k = 0. Those runs give (a) the measured cost of every round shape, (b) the
 * MTP acceptance, (c) the greedy token streams.
 *
 * For each request, leave-one-out: the cost model and acceptance priors are built from the OTHER requests only; the
 * selector picks an arm (rx_casc_select); the realized cost is
 *   - for graph and MTP arms: the request's own measured end-to-end ms per token (a chip run, not a model);
 *   - for n-gram arms: an exact replay over the request's greedy stream (the verify is exact, so acceptance is the
 *     longest common prefix of draft and stream) priced with the request's own measured round costs (composite:
 *     measured parts, counted rounds; labelled so).
 * Fixed choices are priced the same way on the same requests.
 */
#include "runtime/rx_cascade.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks, g_fail;
#define CHECK(c, ...) do { g_checks++; if (!(c)) { g_fail++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- the f7 evidence (pretty-printed JSON, keys sorted) ---- */
static char *slurp(const char *p) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1); if (!b) { fclose(f); return NULL; }
    size_t got = fread(b, 1, (size_t)n, f); fclose(f); b[got] = 0; return b;
}
static double num_after(const char *from, const char *to, const char *key) {
    char k[64]; snprintf(k, sizeof k, "\"%s\":", key);
    const char *p = strstr(from, k);
    if (!p || (to && p >= to)) return NAN;
    return strtod(p + strlen(k), NULL);
}
static uint32_t arr_after(const char *from, const char *key, uint32_t *out, uint32_t cap) {
    char k[64]; snprintf(k, sizeof k, "\"%s\": [", key);
    const char *p = strstr(from, k); if (!p) return 0;
    p += strlen(k);
    uint32_t n = 0;
    while (*p && *p != ']' && n < cap) {
        while (*p == ' ' || *p == '\n' || *p == ',') p++;
        if (*p == ']') break;
        char *e; unsigned long v = strtoul(p, &e, 10); if (e == p) break;
        out[n++] = (uint32_t)v; p = e;
    }
    return n;
}

typedef struct {
    double ms_per_token, verify_ms, draft_ms, snapshot_ms, rollback_ms, accept_rate, tokens_per_round;
    double rounds, rollbacks, accepted, drafted;
} Run;

typedef struct {
    char name[32];
    uint32_t prompt[4096], n_prompt, gen[4096], n_gen;
    double graph_ms;                 /* measured one-row graph, ms per token */
    uint32_t graph_n; double graph_tok_ms[4096];
    Run run[5];                      /* exact rows path with MTP, k = 0..4 */
} Req;

static int load_req(const char *dir, const char *file, const char *name, Req *r) {
    char p[1024]; snprintf(p, sizeof p, "%s/%s", dir, file);
    char *b = slurp(p); if (!b) { printf("cannot read %s\n", p); return -1; }
    memset(r, 0, sizeof *r);
    snprintf(r->name, sizeof r->name, "%s", name);
    r->graph_ms = num_after(b, NULL, "chip_ms_per_token");
    {   /* graph chip_ns list (u64 values; read as doubles) */
        const char *q = strstr(b, "\"chip_ns\": ["); r->graph_n = 0;
        if (q) { q += 12; while (*q && *q != ']' && r->graph_n < 4096) { while (*q == ' ' || *q == '\n' || *q == ',') q++; if (*q == ']') break;
                 char *e; double v = strtod(q, &e); if (e == q) break; r->graph_tok_ms[r->graph_n++] = v / 1e6; q = e; } }
    }
    r->n_prompt = arr_after(b, "prompt_ids", r->prompt, 4096);
    const char *runs = strstr(b, "\"runs\": [");
    const char *p0 = runs;
    for (int k = 0; k <= 4 && p0; k++) {
        const char *s = strstr(p0, "\"accept_rate\":");
        if (!s) { free(b); return -1; }
        const char *nx = strstr(s + 10, "\"accept_rate\":");
        Run *u = &r->run[k];
        u->accept_rate = num_after(s, nx, "accept_rate");
        u->accepted = num_after(s, nx, "accepted");
        u->ms_per_token = num_after(s, nx, "decode_ms_per_token");
        u->draft_ms = num_after(s, nx, "draft_ms");
        u->drafted = num_after(s, nx, "drafted");
        double kk = num_after(s, nx, "k");
        u->rollback_ms = num_after(s, nx, "rollback_ms");
        u->rollbacks = num_after(s, nx, "rollbacks");
        u->rounds = num_after(s, nx, "rounds");
        u->snapshot_ms = num_after(s, nx, "snapshot_ms");
        u->tokens_per_round = num_after(s, nx, "tokens_per_round");
        u->verify_ms = num_after(s, nx, "verify_ms");
        if ((int)kk != k) { printf("%s: run %d has k %g\n", file, k, kk); free(b); return -1; }
        if (k == 0) r->n_gen = arr_after(s, "tokens", r->gen, 4096);
        p0 = nx;
    }
    free(b);
    return 0;
}

static double rows_ms(const Req *r, uint32_t rows) { const Run *u = &r->run[rows - 1]; return u->verify_ms / u->rounds; }

/* p with tokens_per_round(p, k) == t (bisection) */
static double p_from_tpr(double t, uint32_t k) {
    double lo = 0, hi = 1;
    for (int i = 0; i < 60; i++) { double mid = (lo + hi) / 2; if (rx_casc_tokens_per_round(mid, k) < t) lo = mid; else hi = mid; }
    return (lo + hi) / 2;
}

static const char *arm_s(RxCascArm a, char *b) {
    static const char *src[] = { "none", "mtp", "ngram", "dflash2" };
    sprintf(b, "%s/%s/%s/k%u", a.kern ? "rows" : "graph1", src[a.src], a.fmt ? "w4" : "fp8", a.k);
    return b;
}

/* realized ms per token of an arm on request r (measured, or exact n-gram replay with measured round costs) */
static double realized(const RxCascModel *m, const Req *r, RxCascArm a, int adaptive, int *composite, RxCascReplay *rp) {
    *composite = 0;
    if (a.src == RX_CASC_SRC_NONE) return a.kern == RX_CASC_KERN_GRAPH1 ? r->graph_ms : r->run[0].ms_per_token;
    if (a.src == RX_CASC_SRC_MTP) return r->run[a.k].ms_per_token;
    if (a.src == RX_CASC_SRC_NGRAM) {
        double rm[6]; for (uint32_t q = 1; q <= 5; q++) rm[q] = rows_ms(r, q);
        RxCascModel mm = *m;
        /* this request's own measured snapshot / rollback costs */
        double sn = 0, rbm = 0, rbn = 0, rr = 0;
        for (int k = 1; k <= 4; k++) { sn += r->run[k].snapshot_ms; rr += r->run[k].rounds; rbm += r->run[k].rollback_ms; rbn += r->run[k].rollbacks; }
        mm.snapshot_ms = sn / rr; mm.rollback_ms = rbn > 0 ? rbm / rbn : 0;
        RxCascReplay x;
        rx_casc_replay_ngram(&mm, r->prompt, r->n_prompt, r->gen, r->n_gen, a.k, adaptive, r->graph_ms, rm, &x);
        if (rp) *rp = x;
        *composite = 1;
        return x.ms_total / x.tokens;
    }
    return NAN;
}

int main(int argc, char **argv) {
    const char *dir = NULL, *out = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--f7") == 0 && i + 1 < argc) dir = argv[++i];
        else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) out = argv[++i];
    }
    /* ---- unit: n-gram drafter and replay ---- */
    {
        uint32_t ctx[] = { 1, 2, 3, 4, 9, 1, 2, 3 }, d[8];
        uint32_t n = rx_casc_ngram_draft(ctx, 8, 3, 4, d);
        CHECK(n == 3 && d[0] == 4 && d[1] == 9 && d[2] == 1, "n-gram: suffix 1 2 3 -> 4 9 1 (got %u: %u %u)", n, d[0], d[1]);
        CHECK(rx_casc_ngram_draft(ctx, 4, 3, 4, d) == 0, "no repeat, no draft");
        CHECK(fabs(rx_casc_tokens_per_round(0.5, 2) - 1.75) < 1e-12, "tokens per round");
    }
    if (!dir) { printf("rx_cascade_test (units only): checks %d failures %d -> %s\n", g_checks, g_fail, g_fail ? "FAIL" : "PASS"); return g_fail != 0; }
    static Req R[3];
    const char *files[3] = { "gen-codelong.json", "gen-prose.json", "gen-code-golden.json" };
    const char *names[3] = { "codelong", "prose", "golden-code" };
    for (int i = 0; i < 3; i++) { CHECK(load_req(dir, files[i], names[i], &R[i]) == 0, "load %s", files[i]); if (g_fail) return 1; }
    for (int i = 0; i < 3; i++)
        printf("request %-11s prompt %4u tokens, generated %4u; graph %.2f ms/token (%u stamped tokens); rows k0..4: %.1f %.1f %.1f %.1f %.1f\n",
               R[i].name, R[i].n_prompt, R[i].n_gen, R[i].graph_ms, R[i].graph_n, R[i].run[0].ms_per_token, R[i].run[1].ms_per_token,
               R[i].run[2].ms_per_token, R[i].run[3].ms_per_token, R[i].run[4].ms_per_token);

    char path[1024];
    FILE *tsv = NULL;
    if (out) { snprintf(path, sizeof path, "%s/workload-cascade.tsv", out); tsv = fopen(path, "w"); }
    if (tsv) fprintf(tsv, "request\tpolicy\tarm\tpredicted_ms_per_token\trealized_ms_per_token\trealized_kind\tdetail\n");
    static RxSymTrace tr;
    double sum_sel = 0, sum_fixed[6] = { 0 };
    const char *fixed_name[6] = { "graph1 (today)", "rows/mtp/k1", "rows/mtp/k3", "rows/ngram/k4", "rows/ngram/k4 adaptive", "oracle (best measured arm)" };
    double pred_err_sum = 0; int pred_err_n = 0;
    for (int r = 0; r < 3; r++) {
        RxCascModel m; rx_casc_init(&m);
        m.src_available[RX_CASC_SRC_MTP] = 1;
        m.src_available[RX_CASC_SRC_NGRAM] = 1;
        m.src_available[RX_CASC_SRC_DFLASH2] = 0;     /* no DFlash2 engine on the exact path in this build */
        m.rows_exact = 1;                              /* f7: every N-row run identical_to_k0, rows == one-row passes on the 8-layer probe */
        m.fmt_exact[RX_CASC_FMT_W4] = 0;               /* 4-bit: logits differ (f7 drift, ab-fp8head-4bit): not the FP8 answer */
        m.weight_bytes[0] = 26.89e9; m.weight_bytes[1] = 13.4e9;
        m.budget = (RxSymBudget){ 273e9, 207.7e12, 0, 0, 0, 0.0, "LPDDR5x 273 GB/s nominal; FP8 dense peak" };
        /* costs from the OTHER requests */
        double sn = 0, rr = 0, rbm = 0, rbn = 0, dm[5] = { 0 }, dr[5] = { 0 }, tpr[5] = { 0 }; int tn[5] = { 0 };
        for (int o = 0; o < 3; o++) {
            if (o == r) continue;
            for (uint32_t t = 0; t < R[o].graph_n; t++) rx_casc_observe_round(&m, RX_CASC_KERN_GRAPH1, 1, R[o].graph_tok_ms[t]);
            for (int k = 0; k <= 4; k++) {
                /* every round of the run at its mean (the evidence keeps totals per run) */
                const Run *u = &R[o].run[k];
                for (int q = 0; q < 8; q++) rx_casc_observe_round(&m, RX_CASC_KERN_ROWS, (uint32_t)k + 1, u->verify_ms / u->rounds);
                if (k > 0) { dm[k] += u->draft_ms; dr[k] += u->rounds; sn += u->snapshot_ms; rr += u->rounds; rbm += u->rollback_ms; rbn += u->rollbacks;
                             tpr[k] += u->tokens_per_round; tn[k]++; }
            }
        }
        for (int k = 1; k <= 4; k++) m.draft_ms[RX_CASC_SRC_MTP][k] = dm[k] / dr[k];
        for (int k = 1; k <= 4; k++) m.draft_ms[RX_CASC_SRC_NGRAM][k] = 0.001;   /* host lookup, microseconds (measured in the replay) */
        m.snapshot_ms = sn / rr; m.rollback_ms = rbn > 0 ? rbm / rbn : 0;
        /* acceptance priors: MTP from the other requests (per k folded into one per-position p at k = 1);
         * n-gram from this request's PROMPT only (available before decoding) */
        double p_src[RX_CASC_SRC_COUNT] = { 1, NAN, NAN, NAN };
        p_src[RX_CASC_SRC_MTP] = p_from_tpr(tpr[1] / tn[1], 1);
        double hit, accf;
        rx_casc_ngram_prompt_stats(R[r].prompt, R[r].n_prompt, 4, &hit, &accf);
        p_src[RX_CASC_SRC_NGRAM] = hit * accf;
        RxCascArm sel; double pred = NAN;
        char where[64]; snprintf(where, sizeof where, "%s", R[r].name);
        uint32_t adm = rx_casc_select(&m, p_src, &sel, &pred, &tr, where);
        int comp = 0; RxCascReplay rp;
        double real = realized(&m, &R[r], sel, sel.src == RX_CASC_SRC_NGRAM, &comp, &rp);
        char b[64];
        printf("request %-11s: %u admissible arms; selected %s, predicted %.2f, realized %.2f ms/token%s (prior p mtp %.3f, ngram prompt hit %.2f accept %.2f)\n",
               R[r].name, adm, arm_s(sel, b), pred, real, comp ? " (n-gram replay)" : " (measured)", p_src[RX_CASC_SRC_MTP], hit, accf);
        if (tsv) fprintf(tsv, "%s\tselected\t%s\t%.3f\t%.3f\t%s\tleave-one-out priors\n", R[r].name, b, pred, real, comp ? "replay" : "measured");
        if (!comp) { pred_err_sum += fabs(pred - real) / real; pred_err_n++; }
        sum_sel += real;
        /* fixed choices */
        RxCascArm fx[5] = { { 0, 0, 0, 0 }, { RX_CASC_SRC_MTP, 1, 0, 1 }, { RX_CASC_SRC_MTP, 1, 0, 3 }, { RX_CASC_SRC_NGRAM, 1, 0, 4 }, { RX_CASC_SRC_NGRAM, 1, 0, 4 } };
        double best = INFINITY; char bestn[64] = "";
        for (int f = 0; f < 5; f++) {
            double v = realized(&m, &R[r], fx[f], f == 4, &comp, &rp);
            sum_fixed[f] += v;
            printf("    fixed %-24s %8.2f ms/token %s", fixed_name[f], v, comp ? "(replay" : "(measured)");
            if (comp) printf(": %u rounds, %u verify, %u graph, accepted %u of %u drafted, host draft %.0f us)", rp.rounds, rp.verify_rounds,
                             rp.graph_rounds, rp.accepted, rp.drafted, rp.host_draft_ns / 1e3);
            printf("\n");
            if (tsv) fprintf(tsv, "%s\tfixed\t%s\t-\t%.3f\t%s\t%s\n", R[r].name, fixed_name[f], v, comp ? "replay" : "measured", f == 4 ? "online acceptance posterior" : "");
        }
        for (int k = 0; k <= 4; k++) {
            RxCascArm a = { (uint8_t)(k ? RX_CASC_SRC_MTP : RX_CASC_SRC_NONE), (uint8_t)(k ? 1 : 0), 0, (uint8_t)k };
            double v = realized(&m, &R[r], a, 0, &comp, NULL);
            if (v < best) { best = v; arm_s(a, bestn); }
            if (k) {
                double pk = rx_casc_predict_ms_per_token(&m, a, p_src[RX_CASC_SRC_MTP]);
                if (tsv) fprintf(tsv, "%s\tprediction-check\t%s\t%.3f\t%.3f\tmeasured\tcost model vs end-to-end run\n", R[r].name, arm_s(a, b), pk, v);
            }
        }
        double ng = realized(&m, &R[r], fx[4], 1, &comp, NULL);
        if (ng < best) { best = ng; snprintf(bestn, sizeof bestn, "rows/ngram/k4 adaptive"); }
        sum_fixed[5] += best;
        printf("    oracle (best arm on this request, chosen after the fact): %s %.2f ms/token\n", bestn, best);
        if (tsv) fprintf(tsv, "%s\toracle\t%s\t-\t%.3f\tmixed\tbest arm after the fact\n", R[r].name, bestn, best);
    }
    printf("\nmean over the 3 requests (ms/token): selected %.2f", sum_sel / 3);
    for (int f = 0; f < 6; f++) printf(" | %s %.2f", fixed_name[f], sum_fixed[f] / 3);
    printf("\ncost-model prediction error on measured arms: mean %.1f%%\n", pred_err_n ? 100 * pred_err_sum / pred_err_n : 0);
    if (tsv) {
        fprintf(tsv, "mean\tselected\t-\t-\t%.3f\t-\t\n", sum_sel / 3);
        for (int f = 0; f < 6; f++) fprintf(tsv, "mean\tfixed\t%s\t-\t%.3f\t-\t\n", fixed_name[f], sum_fixed[f] / 3);
        fclose(tsv);
    }
    if (out) {
        snprintf(path, sizeof path, "%s/trace-cascade.evidence.json", out);
        FILE *f = fopen(path, "w");
        char concl[300], meas[400];
        snprintf(concl, sizeof concl, "per-request selection over kernel, draft source, k and format: mean %.2f ms/token vs best fixed choice; see measurements",
                 sum_sel / 3);
        snprintf(meas, sizeof meas, "selected_mean=%.3f=ms/token;graph1_mean=%.3f=ms/token;mtp_k1_mean=%.3f=ms/token;mtp_k3_mean=%.3f=ms/token;ngram_k4_mean=%.3f=ms/token;ngram_k4_adaptive_mean=%.3f=ms/token;oracle_mean=%.3f=ms/token",
                 sum_sel / 3, sum_fixed[0] / 3, sum_fixed[1] / 3, sum_fixed[2] / 3, sum_fixed[3] / 3, sum_fixed[4] / 3, sum_fixed[5] / 3);
        if (f) { rx_sym_trace_json(&tr, "omega/rx_cascade_test", "cascade-selection", concl, meas, f); fclose(f); }
    }
    printf("rx_cascade_test: checks %d failures %d -> %s\n", g_checks, g_fail, g_fail ? "FAIL" : "PASS");
    return g_fail != 0;
}
