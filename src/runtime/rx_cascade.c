/*
 * rx_cascade.c -- per-request kernel and draft-source selection (see rx_cascade.h).
 */
#include "rx_cascade.h"

#include <math.h>
#include <string.h>
#include <time.h>

static void features(RxCmFeatures *f, uint32_t rows) {
    memset(f, 0, sizeof *f);
    f->op = RX_CM_OP_MATVEC;
    f->M = 5120;                 /* hidden width: the same for every round of this model */
    f->N = rows;                 /* rows verified in the pass (1 for the graph) */
    f->state_bytes = 1;
    f->cache_bytes = 1;
    f->core = RX_CM_CORE_OTHER;  /* the GB10 GPU, not a CPU core class */
    f->eligible = 3u;
    f->need_evidence = 1;
}

void rx_casc_init(RxCascModel *m) {
    memset(m, 0, sizeof *m);
    rx_cm_init(&m->cm, 2);
    for (int s = 0; s < RX_CASC_SRC_COUNT; s++) { m->acc_a[s] = 1; m->acc_b[s] = 1; }
    m->src_available[RX_CASC_SRC_NONE] = 1;
    m->exact_required = 1;
    m->fmt_exact[RX_CASC_FMT_FP8] = 1;
}

int rx_casc_observe_round(RxCascModel *m, uint32_t kern, uint32_t rows, double ms) {
    RxCmFeatures f; features(&f, rows);
    RxCmObservation o = { (uint64_t)(ms * 1e9), 0 };
    return rx_cm_observe(&m->cm, &f, kern, &o);
}

double rx_casc_predict_round(const RxCascModel *m, uint32_t kern, uint32_t rows, double *sd_log2, int *known) {
    RxCmFeatures f; features(&f, rows);
    RxCmPrediction p; memset(&p, 0, sizeof p);
    rx_cm_predict(&m->cm, &f, kern, &p);
    if (sd_log2) *sd_log2 = p.sd_log2;
    if (known) *known = p.known && p.n > 0;
    if (p.n == 0) return NAN;
    /* log-normal mean of 2^X, X ~ N(mu, sd): 2^(mu + sd^2 ln2 / 2) */
    return pow(2.0, p.mean_log2_ps + 0.5 * p.sd_log2 * p.sd_log2 * log(2.0)) / 1e9;
}

double rx_casc_tokens_per_round(double p, uint32_t k) {
    double e = 1, q = 1;
    for (uint32_t i = 0; i < k; i++) { q *= p; e += q; }
    return e;
}

double rx_casc_predict_ms_per_token(const RxCascModel *m, RxCascArm a, double p) {
    if (a.src == RX_CASC_SRC_NONE || a.k == 0) {
        double g = rx_casc_predict_round(m, a.kern, 1, NULL, NULL);
        return g;
    }
    double v = rx_casc_predict_round(m, RX_CASC_KERN_ROWS, a.k + 1u, NULL, NULL);
    double d = m->draft_ms[a.src][a.k];
    double rb = 1.0 - pow(p, a.k);
    return (v + d + m->snapshot_ms + rb * m->rollback_ms) / rx_casc_tokens_per_round(p, a.k);
}

static void arm_name(RxCascArm a, char *b, size_t n) {
    static const char *src[] = { "none", "mtp", "ngram", "dflash2" };
    snprintf(b, n, "%s/%s/%s/k%u", a.kern ? "rows" : "graph1", src[a.src], a.fmt ? "w4" : "fp8", a.k);
}

uint32_t rx_casc_select(const RxCascModel *m, const double *p_src, RxCascArm *out, double *pred_ms, RxSymTrace *tr,
                        const char *request) {
    double best = INFINITY, incumbent = rx_casc_predict_round(m, RX_CASC_KERN_GRAPH1, 1, NULL, NULL);
    uint32_t admitted = 0;
    for (uint32_t fmt = 0; fmt < 2; fmt++)
        for (uint32_t src = 0; src < RX_CASC_SRC_COUNT; src++)
            for (uint32_t k = 0; k <= 4; k++)
                for (uint32_t kern = 0; kern < 2; kern++) {
                    if ((src == RX_CASC_SRC_NONE) != (k == 0)) continue;
                    if (src != RX_CASC_SRC_NONE && kern != RX_CASC_KERN_ROWS) continue;   /* drafts need N rows */
                    RxCascArm a = { (uint8_t)src, (uint8_t)kern, (uint8_t)fmt, (uint8_t)k };
                    RxSymTraceRec r; memset(&r, 0, sizeof r);
                    snprintf(r.rule, sizeof r.rule, "cascade-arm");
                    char nm[48]; arm_name(a, nm, sizeof nm);
                    snprintf(r.where, sizeof r.where, "%.15s %.30s", request ? request : "", nm);
                    int ok = 1;
                    if (m->exact_required && !m->fmt_exact[fmt]) {
                        ok = 0; snprintf(r.note, sizeof r.note, "refused: format not bit-identical to the FP8 answer (rewrite verdict)");
                        r.verdict.verdict = RX_SYM_V_CHIP_DIFFERENT;
                    } else if (kern == RX_CASC_KERN_ROWS && src != RX_CASC_SRC_NONE && !m->rows_exact) {
                        ok = 0; snprintf(r.note, sizeof r.note, "refused: N-row verify not shown bit-identical");
                    } else if (!m->src_available[src]) {
                        ok = 0; snprintf(r.note, sizeof r.note, "not measurable here: no engine runs this draft source");
                    } else if (src != RX_CASC_SRC_NONE && m->draft_ms[src][k] <= 0 && src != RX_CASC_SRC_NGRAM) {
                        ok = 0; snprintf(r.note, sizeof r.note, "not measurable here: no measured draft cost at this k");
                    }
                    double p = NAN, ms = NAN;
                    if (ok) {
                        p = p_src && !isnan(p_src[src]) ? p_src[src] : m->acc_a[src] / (m->acc_a[src] + m->acc_b[src]);
                        ms = rx_casc_predict_ms_per_token(m, a, p);
                        if (isnan(ms)) { ok = 0; snprintf(r.note, sizeof r.note, "no cost observations for this kernel and row count"); }
                    }
                    if (ok) {
                        /* physics: no arm can go below the weight stream of one pass per expected tokens */
                        RxSymWork w; memset(&w, 0, sizeof w);
                        w.bytes = m->weight_bytes[fmt] / rx_casc_tokens_per_round(p, k);
                        r.budget = rx_sym_budget_admit(&m->budget, &w, incumbent * 1e6, &r.lb_ns);
                        r.incumbent_ns = ms * 1e6;
                        if (r.budget == RX_SYM_B_CANNOT_WIN || r.budget == RX_SYM_B_OVER_LIMIT) {
                            ok = 0; snprintf(r.note, sizeof r.note, "refused by the physics budget (lower bound %.1f ms)", r.lb_ns / 1e6);
                        }
                    }
                    if (ok) {
                        admitted++;
                        snprintf(r.note, sizeof r.note, "predicted %.2f ms/token at acceptance %.3f", ms, p);
                        if (ms < best) { best = ms; *out = a; }
                    }
                    rx_sym_trace_add(tr, &r);
                }
    if (pred_ms) *pred_ms = best;
    return admitted;
}

uint32_t rx_casc_ngram_draft(const uint32_t *ctx, uint32_t n, uint32_t k, uint32_t n_max, uint32_t *out) {
    if (n < 2 || k == 0) return 0;
    for (uint32_t g = n_max; g >= 1; g--) {
        if (g >= n) continue;
        const uint32_t *suf = ctx + n - g;
        for (int64_t s = (int64_t)n - g - 1; s >= 0; s--) {
            if (memcmp(ctx + s, suf, g * sizeof *ctx) != 0) continue;
            uint32_t len = 0;
            for (uint64_t j = (uint64_t)s + g; j < n && len < k; j++) out[len++] = ctx[j];
            if (len) return len;
        }
    }
    return 0;
}

void rx_casc_ngram_prompt_stats(const uint32_t *prompt, uint32_t n, uint32_t k, double *hit_rate, double *accept_frac) {
    uint32_t hits = 0, tries = 0, drafted = 0, acc = 0, d[RX_CASC_KMAX];
    for (uint32_t i = 8; i + 1 < n; i++) {
        tries++;
        uint32_t len = rx_casc_ngram_draft(prompt, i, k, 4, d);
        if (!len) continue;
        hits++; drafted += len;
        uint32_t a = 0;
        while (a < len && i + a < n && d[a] == prompt[i + a]) a++;
        acc += a;
    }
    if (hit_rate) *hit_rate = tries ? (double)hits / tries : 0;
    if (accept_frac) *accept_frac = drafted ? (double)acc / drafted : 0;
}

static double now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e9 + t.tv_nsec; }

int rx_casc_replay_ngram(const RxCascModel *m, const uint32_t *prompt, uint32_t n_prompt, const uint32_t *gen,
                         uint32_t n_gen, uint32_t k, int adaptive, double graph_ms, const double *rows_ms,
                         RxCascReplay *out) {
    static uint32_t ctx[1u << 15];
    if (n_prompt + n_gen > (1u << 15) || k > RX_CASC_KMAX) return -1;
    memset(out, 0, sizeof *out);
    memcpy(ctx, prompt, n_prompt * sizeof *ctx);
    uint32_t n = n_prompt, done = 0, d[RX_CASC_KMAX];
    double a = 1, b = 1;     /* online Beta of per-position acceptance of n-gram drafts in this request */
    /* The first generated token comes out of the prompt pass (not counted, as in the measured runs). */
    ctx[n++] = gen[0]; done = 1;
    while (done < n_gen) {
        double t0 = now_ns();
        uint32_t len = rx_casc_ngram_draft(ctx, n, k, 4, d);
        out->host_draft_ns += now_ns() - t0;
        if (len > n_gen - done) len = n_gen - done;
        int verify = len > 0;
        if (verify && adaptive) {
            double p = a / (a + b);
            double v = (rows_ms[len + 1] + m->snapshot_ms + (1.0 - pow(p, len)) * m->rollback_ms) / rx_casc_tokens_per_round(p, len);
            verify = v < graph_ms;
        }
        out->rounds++;
        if (!verify) {
            out->graph_rounds++;
            out->ms_total += graph_ms;
            ctx[n++] = gen[done++];
            continue;
        }
        uint32_t acc = 0;
        while (acc < len && d[acc] == gen[done + acc]) acc++;
        out->verify_rounds++; out->drafted += len; out->accepted += acc;
        out->ms_total += rows_ms[len + 1] + m->snapshot_ms + (acc < len ? m->rollback_ms : 0);
        out->rollbacks += acc < len;
        /* accepted drafts plus the model's own next token */
        uint32_t adv = acc + 1;
        if (adv > n_gen - done) adv = n_gen - done;
        for (uint32_t j = 0; j < adv; j++) ctx[n++] = gen[done++];
        a += acc; b += (len - acc);
    }
    out->tokens = n_gen - 1;
    return 0;
}
