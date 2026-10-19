/*
 * rx_cascade.h -- per-request kernel and draft-source selection for the Qwen3.8-27B cascade (M6 of
 * OMEGA_SYMBOLIC_REWRITE).
 *
 * A request is decoded as rounds. A round either runs the one-row decode graph (GEMV kernels, one token), or
 * verifies a draft of k tokens as one (k+1)-row pass (the multi-row "exact rows" kernels, bit-identical per row to a
 * one-row pass) and keeps the longest agreeing prefix plus the model's own next token. Running k drafted tokens as
 * one multi-row pass is itself a rewrite: map(decode, tokens) -> one batched pass over shared weights. Its exactness
 * is a chip verdict (every row byte-equal to a one-row pass), recorded like every other rewrite verdict.
 *
 * Choices per request: kernel (1-row graph vs N-row verify), draft source (none, the model's MTP head, n-gram prompt
 * lookup, DFlash2), draft length k, weight format (FP8, 4-bit). They are priced by rx_costmodel (Bayesian linear
 * regression on log2 time per round, fed with measured rounds) and an acceptance posterior per draft source, filtered
 * first by the rewrite layer (an arm whose output is not bit-identical to the FP8 greedy answer is refused when the
 * request needs the exact answer) and by the physics budget (an arm whose lower bound cannot beat the incumbent).
 */
#ifndef RX_CASCADE_H
#define RX_CASCADE_H

#include "rx_costmodel.h"
#include "rx_symbolic.h"

#include <stdint.h>

enum { RX_CASC_SRC_NONE = 0, RX_CASC_SRC_MTP, RX_CASC_SRC_NGRAM, RX_CASC_SRC_DFLASH2, RX_CASC_SRC_COUNT };
enum { RX_CASC_KERN_GRAPH1 = 0, RX_CASC_KERN_ROWS = 1 };
enum { RX_CASC_FMT_FP8 = 0, RX_CASC_FMT_W4 = 1 };
#define RX_CASC_KMAX 8u

typedef struct { uint8_t src, kern, fmt, k; } RxCascArm;

typedef struct {
    RxCostModel cm;                        /* arm 0: graph, 1 row; arm 1: rows kernel, M = rows */
    double draft_ms[RX_CASC_SRC_COUNT][RX_CASC_KMAX + 1];   /* per round, measured (0 = unknown) */
    double snapshot_ms, rollback_ms;       /* per verify round; per rolled-back round */
    double acc_a[RX_CASC_SRC_COUNT], acc_b[RX_CASC_SRC_COUNT];  /* Beta posterior of per-position acceptance */
    int src_available[RX_CASC_SRC_COUNT];  /* an engine exists here that can run the source */
    int exact_required;                    /* the request needs the FP8 greedy answer bit for bit */
    int fmt_exact[2];                      /* rewrite verdict: format gives the FP8 answer bit for bit */
    int rows_exact;                        /* rewrite verdict: N-row verify rows equal one-row passes */
    RxSymBudget budget;
    double weight_bytes[2];                /* bytes per pass, FP8 and 4-bit */
} RxCascModel;

void   rx_casc_init(RxCascModel *m);
/* One measured round: kernel, rows, milliseconds. */
int    rx_casc_observe_round(RxCascModel *m, uint32_t kern, uint32_t rows, double ms);
/* Predicted milliseconds of one verify round (log-normal mean) and its sd in log2 units. */
double rx_casc_predict_round(const RxCascModel *m, uint32_t kern, uint32_t rows, double *sd_log2, int *known);
/* Expected tokens per round of a chain of k drafts with per-position acceptance p. */
double rx_casc_tokens_per_round(double p, uint32_t k);
/* Predicted ms per token of an arm under acceptance p. */
double rx_casc_predict_ms_per_token(const RxCascModel *m, RxCascArm a, double p);

/* Filter (rewrite verdicts, availability, physics budget) then choose the cheapest predicted arm. p_src: expected
 * acceptance per source for this request (NAN: use the posterior mean). Writes one trace record per arm. Returns
 * the number of admissible arms. */
uint32_t rx_casc_select(const RxCascModel *m, const double *p_src, RxCascArm *out, double *pred_ms, RxSymTrace *tr,
                        const char *request);

/* n-gram prompt lookup: the longest suffix of ctx (n_max down to 1 tokens) that occurred earlier in ctx; the up to k
 * tokens that followed its latest earlier occurrence. Returns the draft length (0: no match). */
uint32_t rx_casc_ngram_draft(const uint32_t *ctx, uint32_t n, uint32_t k, uint32_t n_max, uint32_t *out);

/* Exact replay of an n-gram policy over a known greedy stream (the verify is exact, so what is accepted is the
 * longest common prefix of the draft and the true continuation). adaptive: verify a draft only when the predicted
 * cost per token beats the one-row graph under the online acceptance posterior; otherwise run the graph. Costs per
 * round from the measured tables (graph_ms; rows_ms[rows]; snapshot, rollback). */
typedef struct {
    double ms_total;
    uint32_t rounds, graph_rounds, verify_rounds, drafted, accepted, rollbacks, tokens;
    double host_draft_ns;
} RxCascReplay;

int rx_casc_replay_ngram(const RxCascModel *m, const uint32_t *prompt, uint32_t n_prompt, const uint32_t *gen,
                         uint32_t n_gen, uint32_t k, int adaptive, double graph_ms, const double *rows_ms,
                         RxCascReplay *out);

/* Prompt-only estimate of n-gram usefulness: run the drafter over the prompt itself. */
void rx_casc_ngram_prompt_stats(const uint32_t *prompt, uint32_t n, uint32_t k, double *hit_rate, double *accept_frac);

#endif
