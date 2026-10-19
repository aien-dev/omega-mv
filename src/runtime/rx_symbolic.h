/*
 * rx_symbolic.h -- Omega symbolic rewrite layer (OMEGA_SYMBOLIC_REWRITE).
 *
 * A typed symbolic expression protocol on top of Omega's canonical object
 * encoding, and bounded, assumption-aware, machine-arithmetic-aware rewrite
 * rules that run INSIDE rx_graph_optimize (through AgRewriteHook, rx_graph.h).
 * No second optimizer, cache or graph engine: graphs are AgGraph, identities
 * are Omega SEMANTIC_IDs (omega_compute_semantic_id), verdicts are cached in
 * the rx_semantic content-addressed cache (rx_symbolic_cache.c), choices are
 * priced by rx_costmodel.
 *
 * 1. Terms. A term is head[args] with a machine type, in the style of a
 *    Wolfram expression: VAR, CONST, OP (an Omega OpCode) or KERNEL (a
 *    decode-graph launch kind). Its canonical form is an OmegaObject of kind
 *    KIND_OPERATION with attribute "omega.sym" = "term/1", one constraint per
 *    assumption (CONST_RANGE / CONST_PRECONDITION) and a big-endian payload
 *    (head, op, type, imm, then the SEMANTIC_ID of every argument). The term's
 *    id IS that object's SEMANTIC_ID, so a term is content-addressed exactly
 *    like every other Omega object, and two builds of the same expression in
 *    any order get the same id. A proven rewrite is an object relation
 *    REL_EQUIVALENT_TO from the left side to the right side.
 *
 * 2. Machine arithmetic. Types carry their arithmetic: WRAP (the ring
 *    Z/2^width, what OVERFLOW_WRAP evaluation does), RNE (IEEE round to
 *    nearest even), BLOCK (block-scaled quantized: FP8 e4m3 128x128, MXFP4).
 *    Rules state which arithmetic they hold in. The affine normal form is
 *    valid in WRAP because Z/2^64 is a commutative ring; it is never applied
 *    to RNE or BLOCK values (associativity fails there). x*c/c -> x is false
 *    in WRAP and is only applied with a RANGE assumption proving no wrap.
 *
 * 3. Verification. Every candidate is checked bit for bit before it is
 *    applied. For WRAP affine terms the check is a proof: two affine maps
 *    over Z/2^64 in n variables are equal iff they agree at 0 and at the n
 *    unit vectors, and the checker re-derives affinity from the term itself,
 *    so it does not trust the rewriter. Every candidate is in addition
 *    evaluated on edge and random inputs inside its assumptions with the same
 *    evaluator the graph runs (omega_eval_pure_binary_uint, OVERFLOW_WRAP).
 *    Kernel rewrites are verified by the caller's chip run (byte compare of
 *    every output), reported through the same verdict record.
 *
 * 4. Budget. Before a candidate is verified, a physics budget filter drops
 *    the ones that cannot pay: lower bound = max(bytes / DRAM bandwidth,
 *    flops / peak) + launches * launch floor, and hard limits (memory, shared
 *    memory per CTA). A candidate whose lower bound is not below the
 *    incumbent's measured cost by more than the noise band is refused as
 *    "cannot be shown".
 *
 * 5. Trace. Every candidate (applied, refused, refuted) is one trace record,
 *    written as an aien.explain.evidence-graph v1 document (aienos-explain
 *    docs/EXPLAIN_EVIDENCE_GRAPH_V1.md).
 *
 * Bounded: fuel (candidates per optimize call), term depth, term store size,
 * at most AG_MAX_FUSED steps in a rewritten node.
 */
#ifndef RX_SYMBOLIC_H
#define RX_SYMBOLIC_H

#include "rx_graph.h"
#include "omega_types.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define RX_SYM_MAX_TERMS   2048u
#define RX_SYM_MAX_ARGS    4u
#define RX_SYM_MAX_ASSUME  2u
#define RX_SYM_AFF_MAX     4u      /* variables in an affine form */
#define RX_SYM_MAX_DEPTH   12u
#define RX_SYM_MAX_TRACE   256u
#define RX_SYM_VERIFY_VECTORS 4096u

/* ---- types ---- */
enum { RX_SYM_TY_NONE = 0, RX_SYM_TY_U64, RX_SYM_TY_BOOL, RX_SYM_TY_F32, RX_SYM_TY_BF16,
       RX_SYM_TY_FP8E4M3, RX_SYM_TY_MXFP4 };
enum { RX_SYM_AR_NONE = 0, RX_SYM_AR_WRAP, RX_SYM_AR_RNE, RX_SYM_AR_BLOCK };

typedef struct {
    uint8_t scalar;     /* RX_SYM_TY_* */
    uint8_t arith;      /* RX_SYM_AR_* */
    uint16_t width;     /* bits of one element */
    uint32_t rows, cols;/* 0 for a scalar */
} RxSymType;

/* ---- terms ---- */
enum { RX_SYM_H_VAR = 1, RX_SYM_H_CONST, RX_SYM_H_OP, RX_SYM_H_KERNEL };

/* Decode-graph launch kinds (the q38 timeline kinds plus the cascade's). */
enum { RX_SYM_K_NORM = 1, RX_SYM_K_GEMV_FP8, RX_SYM_K_GEMV_BF16, RX_SYM_K_GEMV_W4, RX_SYM_K_GDN,
       RX_SYM_K_GATHER, RX_SYM_K_ATTN, RX_SYM_K_GATE, RX_SYM_K_ADDNORM, RX_SYM_K_SWIGLU,
       RX_SYM_K_ARGMAX, RX_SYM_K_GEMM_ROWS_FP8, RX_SYM_K_DRAFT, RX_SYM_K_COUNT };

enum { RX_SYM_A_RANGE = 1,           /* lo <= value <= hi */
       RX_SYM_A_NONZERO,
       RX_SYM_A_PERMUTATION,          /* a gather table is a bijection */
       RX_SYM_A_ROW_INDEPENDENT,      /* each output row depends only on its own weight row */
       RX_SYM_A_SCALE_COMPATIBLE,     /* a row permutation maps whole scale blocks (data-checked) */
       RX_SYM_A_EXACT_REQUIRED };     /* the consumer needs bit-identical values */

typedef struct { uint8_t kind; uint64_t lo, hi; } RxSymAssume;

typedef struct {
    uint8_t head;
    uint16_t op;                 /* OpCode for OP, RX_SYM_K_* for KERNEL */
    RxSymType type;
    uint8_t n_args;
    uint32_t arg[RX_SYM_MAX_ARGS];
    uint64_t imm;                /* CONST value, VAR index, KERNEL parameter */
    uint8_t n_assume;
    RxSymAssume assume[RX_SYM_MAX_ASSUME];
    SemanticId id;               /* SEMANTIC_ID of the canonical object */
} RxSymTerm;

typedef struct {
    uint32_t n;
    RxSymTerm t[RX_SYM_MAX_TERMS];
} RxSymStore;

#define RX_SYM_NONE 0xFFFFFFFFu

void rx_sym_store_init(RxSymStore *s);
RxSymType rx_sym_u64(void);
RxSymType rx_sym_bool(void);

/* Canonical OmegaObject of a term (the args' ids must already be set). */
int rx_sym_object(const RxSymStore *s, const RxSymTerm *t, OmegaObject *out);
/* Intern: fills the SEMANTIC_ID and returns the index of the one term with
 * that id (hash-consed), or RX_SYM_NONE when the store is full. */
uint32_t rx_sym_intern(RxSymStore *s, const RxSymTerm *proto);
uint32_t rx_sym_var(RxSymStore *s, uint64_t index, RxSymType ty);
uint32_t rx_sym_const(RxSymStore *s, uint64_t v, RxSymType ty);
uint32_t rx_sym_op(RxSymStore *s, uint32_t op, RxSymType ty, uint32_t a, uint32_t b);
uint32_t rx_sym_with_assume(RxSymStore *s, uint32_t t, const RxSymAssume *a);
/* Proven equivalence as an Omega object relation: lhs object with
 * REL_EQUIVALENT_TO -> rhs. Its SEMANTIC_ID names the proof claim. */
int rx_sym_equivalence_object(const RxSymStore *s, uint32_t lhs, uint32_t rhs, OmegaObject *out);
/* Number of distinct OP and KERNEL nodes under t. */
uint32_t rx_sym_cost(const RxSymStore *s, uint32_t t);

/* ---- machine arithmetic ---- */

/* Affine form over Z/2^64: sum coef[i] * var[i] + k. 0 if t is affine (VAR,
 * CONST, ADD, SUB, MUL with a constant side, IDENTITY), all WRAP width 64. */
typedef struct {
    uint32_t n;
    uint64_t var[RX_SYM_AFF_MAX];
    uint64_t coef[RX_SYM_AFF_MAX];
    uint64_t k;
} RxSymAffine;

int rx_sym_affine(const RxSymStore *s, uint32_t t, RxSymAffine *out);
/* Evaluate with Omega's own pure evaluator (OVERFLOW_WRAP, 64 bits). vars
 * indexed by VAR imm. 0, or -1 when an operation fails (division by zero). */
int rx_sym_eval_u64(const RxSymStore *s, uint32_t t, const uint64_t *vars, uint32_t n_vars, uint64_t *out);

/* ---- verification ---- */
enum { RX_SYM_V_PROVED = 1,        /* affine proof plus vectors */
       RX_SYM_V_TESTED,            /* vectors only, inside the assumptions */
       RX_SYM_V_REFUTED,           /* a vector differs: counterexample recorded */
       RX_SYM_V_CHIP_EQUAL,        /* caller's chip run: every output byte equal */
       RX_SYM_V_CHIP_DIFFERENT,
       RX_SYM_V_SIDE_CONDITION_FAILED };   /* a rule's side condition checked against the data and false */

typedef struct {
    int verdict;
    uint32_t vectors;
    uint64_t counter[RX_SYM_AFF_MAX];   /* refuted: the input */
    uint64_t got_lhs, got_rhs;
    int cache_hit;                      /* served by the rx_semantic cache */
} RxSymVerdict;

/* n_vars variables named by VAR imm 0..n_vars-1; ranges[i] (kind RANGE) may be NULL. */
int rx_sym_verify_u64(const RxSymStore *s, uint32_t lhs, uint32_t rhs, uint32_t n_vars,
                      const RxSymAssume *ranges, RxSymVerdict *out);

/* ---- physics budget ---- */
typedef struct {
    double dram_bytes_per_s;     /* streaming bandwidth the bound uses */
    double peak_flops;           /* arithmetic peak */
    double launch_floor_ns;      /* smallest launch or reaction, measured */
    uint64_t mem_bytes;          /* memory a candidate may hold */
    uint32_t smem_bytes_per_cta; /* largest shared memory one CTA may take */
    double noise_frac;           /* run-to-run spread of the incumbent's measurement */
    const char *source;          /* where these numbers come from */
} RxSymBudget;

typedef struct {
    double bytes, flops, launches;
    uint64_t mem_bytes;
    uint32_t smem_bytes;
} RxSymWork;

enum { RX_SYM_B_ADMIT = 1, RX_SYM_B_CANNOT_WIN, RX_SYM_B_BELOW_NOISE, RX_SYM_B_OVER_LIMIT };

double rx_sym_lower_bound_ns(const RxSymBudget *b, const RxSymWork *w);
/* incumbent_ns: measured cost of what runs now. */
int rx_sym_budget_admit(const RxSymBudget *b, const RxSymWork *cand, double incumbent_ns, double *lb_ns);
const char *rx_sym_budget_name(int v);

/* ---- trace ---- */
typedef struct {
    char rule[40];
    char where[48];              /* e.g. "node 7 (origin 9)" */
    SemanticId lhs, rhs;
    int budget;                  /* RX_SYM_B_* */
    double lb_ns, incumbent_ns;
    RxSymVerdict verdict;
    int applied;
    uint32_t cost_before, cost_after;
    char note[96];
} RxSymTraceRec;

typedef struct {
    uint32_t n, dropped;
    RxSymTraceRec r[RX_SYM_MAX_TRACE];
} RxSymTrace;

void rx_sym_trace_add(RxSymTrace *tr, const RxSymTraceRec *r);
/* aien.explain.evidence-graph v1: one node per record (kind Diagnostic /
 * EvaluationReceipt), one Conclusion node per workload that rests on them.
 * extra_measurements: "name=value=unit;..." for the conclusion, may be NULL. */
int rx_sym_trace_json(const RxSymTrace *tr, const char *producer, const char *workload,
                      const char *conclusion, const char *extra_measurements, FILE *f);

/* ---- the rewrite pass inside rx_graph_optimize ---- */

/* Verdict cache hook (rx_symbolic_cache.c): look up (rule, lhs, rhs); on a
 * miss run verify and file the verdict. Return the verdict code. */
typedef int (*RxSymCacheFn)(void *cache, const char *rule, const SemanticId *lhs, const SemanticId *rhs,
                            int (*verify)(void *vctx, RxSymVerdict *v), void *vctx, RxSymVerdict *out);

typedef struct {
    AgRewriteHook hook;          /* must be first: rx_graph_optimize_ex calls hook.pass */
    RxSymStore *store;           /* scratch term store */
    uint32_t fuel;               /* candidates this optimize call may try */
    uint32_t tried, applied, refused, refuted;
    /* Assumptions by node origin (caller facts, e.g. a declared field range). */
    uint32_t n_facts;
    struct { uint32_t origin; RxSymAssume a; } fact[16];
    const RxSymBudget *budget;   /* NULL: no budget filter */
    double reaction_ns;          /* measured cost of one reaction (budget incumbent unit) */
    RxSymCacheFn cache_fn;
    void *cache;
    RxSymTrace *trace;           /* may be NULL */
    int test_unsound;            /* TEST ONLY: also propose (x / c) * c -> x with no assumption */
    uint32_t n_seen;             /* left sides already tried in this pass (refused ones are not retried) */
    SemanticId seen[64];
} RxSymPass;

/* Prepare a pass over `store` (cleared by every optimize call). */
void rx_sym_pass_init(RxSymPass *p, RxSymStore *store, uint32_t fuel);
int  rx_sym_pass_fact(RxSymPass *p, uint32_t origin, const RxSymAssume *a);

/* ---- decode-graph (kernel) rules ---- */

/* One launch of a decode graph: kind, the buffers it reads and writes (bit
 * per buffer id), its weight bytes and its measured chip time. */
typedef struct {
    uint8_t kind;                /* RX_SYM_K_* */
    uint32_t reads, writes;
    uint64_t weight_bytes;
    double us;
    uint8_t marked_independent;  /* recorded with no barrier before it */
    uint8_t layer;
} RxSymLaunch;

typedef struct {
    uint32_t n;
    RxSymLaunch l[64];
} RxSymLaunchList;

typedef struct {
    uint32_t barriers_total, barriers_needed, barriers_droppable, already_dropped;
    uint32_t droppable_at[64];        /* launch index i: no hazard with launch i - 1 */
    uint32_t n_droppable_at;
    double glue_us, weight_us, span_us;
} RxSymBarrierReport;

/* Build the AgGraph of a launch list (AG_PHYSICAL per launch, a DATA edge
 * per RAW/WAR/WAW hazard with its nearest earlier launch on that buffer, an
 * ORDER edge for every recorded barrier), run rx_graph_optimize_ex on it, and
 * report which recorded barriers carry no hazard. */
int rx_sym_barriers(const RxSymLaunchList *ll, RxSymPass *pass, RxSymBarrierReport *out, AgReport *rep);

/* Gather fold: GATHER(GEMV(W, x), P) -> GEMV(P W, x). Side conditions, each
 * checked, never assumed: P is a permutation; the GEMV realization is row
 * independent; for block-scaled weights every 128-row scale block of P W is
 * one scale block of W (rows map block to block) or the two source blocks
 * hold equal scales. scale_inv: [rows/128][cols/128] bf16 bits.
 * Returns 1 eligible, 0 refused (reason in why). */
int rx_sym_gather_fold_check(const uint32_t *perm, uint32_t n, const uint16_t *scale_inv, uint32_t rows,
                             uint32_t cols, uint32_t *blocks_bad, char *why, size_t why_cap);

#endif /* RX_SYMBOLIC_H */
