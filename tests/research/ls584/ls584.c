/*
 * ls584.c -- bounded learning-mechanism search harness (omega-mv#584, ledger L048).
 *
 * Everything this file does is fixed by research/learning-search-584/FREEZE.md.
 * It is additive: it calls the existing realization path (omega_program.h,
 * omega_realize_synth.h), the existing semantic evaluator, and the existing
 * Cortex contract (runtime/rx_cortex.h) for persistence. It edits no runtime
 * file and holds no authority.
 *
 *   ls584 run   (--arm A|B|C0|C1 | --arms A,A2,B,C0) --seed HEX --out DIR [--reuse 1..6]
 *               [--control none|shuffle|nolearn|stale|unseen] [--restart-after N]
 *               [--mutant none|M1|M2|M3|M4|M5] [--no-persist]
 *   ls584 stream --seed HEX [--reuse R]   the stream as seq,block,kind,key (checks)
 *   ls584 neth  C p Delta r Hmax          net(H) diagnostic with a direct finite-sum check
 *
 * Output: DIR/rows.csv (one row per objective), DIR/summary.json; with --arms, the same
 * per lane in DIR/<lane>/.
 *
 * r2 (ledger L167, read at research/learning-search-584@b557e9e7: FREEZE.md, AMENDMENTS
 * A1-A3, evidence README "Next discriminating experiment"): lanes run interleaved per
 * objective in one process, and --reuse R repeats every key's reuse opportunities R times
 * (AMENDMENTS A4). With one lane and R = 1 every decision is that of the sealed harness.
 */
#include "omega_program.h"
#include "omega_realize_synth.h"
#include "omega_exec.h"
#include "omega_machine.h"
#include "runtime/rx_cortex.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- frozen numbers (FREEZE.md section 3, 4) ---- */
#define LS_WIDTH        32u
#define LS_NCONST       16u
static const uint64_t LS_D[LS_NCONST] = { 1, 2, 3, 4, 5, 7, 8, 15, 16, 31, 32, 64, 100, 255, 256, 1000 };
static const uint8_t  LS_OPS[5] = { OP_ADD, OP_SUB, OP_MUL, OP_AND, OP_OR };
#define LS_NOPS         5u
#define LS_MAX_RULE     3u
#define LS_MAX_ACTION   6u
#define LS_NEX          12u          /* examples per objective: 4 structured + 8 random */
#define LS_NCHECK       16u          /* sealed check inputs per objective (SAMPLED) */
#define LS_SEARCH_BUDGET 524288u     /* candidate evaluations per objective */
#define LS_REALIZE_BUDGET 4u
#define LS_TABLE_CAP    32u          /* = PL_MAX_TEMPLATES */
#define LS_POOL         64u
#define LS_NOBJ         240u
#define LS_NPREFACE     8u           /* malformed / ambiguous commands (adapter checks) */
#define LS_NSHAPES      (5u + 25u + 125u)
#define LS_NREP         48u          /* block 2 objectives per pass */
#define LS_NRECALL      24u          /* block 5 objectives per pass */
#define LS_MAX_REUSE    6u           /* AMENDMENTS A4: reuse opportunities per key, 1 = sealed design */
#define LS_MAX_STREAM   (LS_NPREFACE + LS_NOBJ + (LS_MAX_REUSE - 1u) * (LS_NREP + LS_NRECALL))
#define LS_MAX_LANES    5u

enum { K_SOLVE = 1, K_RECALL, K_COMPOSE, K_CORRECT };
enum { ST_CANDIDATE = 1, ST_VERIFIED, ST_STALE };
enum { CTL_NONE = 0, CTL_SHUFFLE, CTL_NOLEARN, CTL_STALE, CTL_UNSEEN };
enum { MUT_NONE = 0, MUT_M1, MUT_M2, MUT_M3, MUT_M4, MUT_M5 };
enum { OUT_SUCCESS = 1, OUT_WRONG, OUT_ABSTAIN, OUT_FAILED_SEARCH, OUT_REFUSED_REALIZE, OUT_CLARIFY };

/* ---- rng ---- */
static uint64_t rng_s;
static uint64_t rnd(void) {
    uint64_t x = rng_s;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    rng_s = x;
    return x * 0x2545F4914F6CDD1Dull;
}
static uint32_t rnd_below(uint32_t n) { return (uint32_t)(rnd() % n); }

static uint64_t now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec; }
static uint64_t cpu_ns(void) { struct timespec t; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t); return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec; }

/* ---- programs ---- */
typedef struct { uint8_t n; uint8_t op[LS_MAX_ACTION]; uint64_t imm[LS_MAX_ACTION]; } Body;

static uint64_t wmask(void) { return (1ull << LS_WIDTH) - 1ull; }

/* Harness interpreter of the V0 semantics (ring ops mod 2^w, bitwise and/or). The
 * committed program is always re-checked by omega_program_eval and the native
 * differential, so a divergence here is caught, not trusted. */
static uint64_t interp(const Body *b, uint64_t x) {
    uint64_t v = x;
    for (uint32_t i = 0; i < b->n; i++) {
        switch (b->op[i]) {
        case OP_ADD: v += b->imm[i]; break;
        case OP_SUB: v -= b->imm[i]; break;
        case OP_MUL: v *= b->imm[i]; break;
        case OP_AND: v &= b->imm[i]; break;
        case OP_OR:  v |= b->imm[i]; break;
        default: return 0;
        }
    }
    return v & wmask();
}

static int make_prog(OmegaProgram *p, const Body *b) {
    omega_program_init(p, "ls584");
    p->contract.input_type = p->contract.output_type = TYPE_UNSIGNED_INT;
    p->contract.input_width = p->contract.output_width = (uint16_t)LS_WIDTH;
    snprintf(p->contract.precondition, sizeof p->contract.precondition, "true");
    snprintf(p->contract.postcondition, sizeof p->contract.postcondition, "true");
    omega_build_constraint_id(CONST_PRECONDITION, "true", &p->contract.precondition_id);
    omega_build_constraint_id(CONST_POSTCONDITION, "true", &p->contract.postcondition_id);
    p->body.has_body = true;
    p->body.step_count = b->n;
    for (uint32_t i = 0; i < b->n; i++) { p->body.steps[i].op = b->op[i]; p->body.steps[i].imm = b->imm[i]; }
    return omega_program_compute_id(p);
}

static uint32_t shape_index(const Body *b) {     /* 0..154 for n in 1..3 */
    uint32_t base = b->n == 1 ? 0 : b->n == 2 ? 5 : 30, idx = 0;
    for (uint32_t i = 0; i < b->n; i++) {
        uint32_t o = 0;
        while (o < LS_NOPS && LS_OPS[o] != b->op[i]) o++;
        idx = idx * LS_NOPS + o;
    }
    return base + idx;
}
static uint32_t const_index(uint64_t imm) { for (uint32_t i = 0; i < LS_NCONST; i++) if (LS_D[i] == imm) return i; return LS_NCONST; }

/* ---- objectives and streams (FREEZE.md section 4) ---- */
typedef struct {
    uint32_t seq, kind, block;
    uint64_t key, key_a, key_b;    /* COMPOSE names a and b */
    Body rule;                     /* hidden */
    uint32_t n_ex;
    uint64_t ex_x[LS_NEX], ex_y[LS_NEX];
    uint64_t chk_x[LS_NCHECK], chk_y[LS_NCHECK];
    uint64_t fix_x, fix_y;         /* CORRECT counterexample */
    int malformed;                 /* preface: adapter must abstain */
    char text[1024];               /* the bounded command */
} Objective;

typedef struct { uint64_t key; Body rule; int touched_after_block1; } PoolKey;

static Body rnd_rule(uint32_t n) {
    Body b; memset(&b, 0, sizeof b);
    b.n = (uint8_t)n;
    for (uint32_t i = 0; i < n; i++) { b.op[i] = LS_OPS[rnd_below(LS_NOPS)]; b.imm[i] = LS_D[rnd_below(LS_NCONST)]; }
    return b;
}
static uint32_t rnd_len(void) { uint32_t r = rnd_below(10); return r < 4 ? 1 : r < 8 ? 2 : 3; }

static const uint64_t STRUCTURED[4] = { 0, 1, 0xFFFFFFFFull, 0x80000000ull };
static void fill_examples(Objective *o) {
    if (!o->n_ex) o->n_ex = LS_NEX;
    for (uint32_t i = 0; i < 4 && i < o->n_ex; i++) o->ex_x[i] = STRUCTURED[i];
    for (uint32_t i = 4; i < o->n_ex; i++) o->ex_x[i] = rnd() & wmask();
    for (uint32_t i = 0; i < o->n_ex; i++) o->ex_y[i] = interp(&o->rule, o->ex_x[i]);
    for (uint32_t i = 0; i < LS_NCHECK; i++) { o->chk_x[i] = rnd() & wmask(); o->chk_y[i] = interp(&o->rule, o->chk_x[i]); }
}

static void render_text(Objective *o) {
    char *p = o->text, *e = o->text + sizeof o->text;
    const char *k = o->kind == K_SOLVE ? "SOLVE" : o->kind == K_RECALL ? "RECALL" : o->kind == K_COMPOSE ? "COMPOSE" : "CORRECT";
    p += snprintf(p, (size_t)(e - p), "%s k=%016" PRIx64 " w=%u", k, o->key, LS_WIDTH);
    if (o->kind == K_COMPOSE) p += snprintf(p, (size_t)(e - p), " a=%016" PRIx64 " b=%016" PRIx64, o->key_a, o->key_b);
    p += snprintf(p, (size_t)(e - p), " ex=");
    for (uint32_t i = 0; i < o->n_ex; i++) p += snprintf(p, (size_t)(e - p), "%s%" PRIx64 ":%" PRIx64, i ? "," : "", o->ex_x[i], o->ex_y[i]);
    if (o->kind == K_CORRECT) p += snprintf(p, (size_t)(e - p), " fix=%" PRIx64 ":%" PRIx64, o->fix_x, o->fix_y);
}

static PoolKey pool[LS_POOL + 64];
static uint32_t pool_n;
static Objective *stream;              /* LS_NPREFACE + LS_NOBJ + (R - 1) * 72 */
static uint32_t stream_n;

static int g_mutant, g_control;
static uint32_t g_reuse = 1;          /* reuse opportunities per key, as a multiple of the sealed design */

/* AMENDMENTS A4: passes 2..R of block 2 (or 5) repeat pass 1's key sequence with fresh
 * example and check inputs, so every key gets exactly R times its sealed-design reuse
 * opportunities. They draw from their own generator and the main generator is restored,
 * so blocks 1, 3, 4, 6, 7 and pass 1 are the same objectives for every R, and R = 1 is the
 * sealed stream. M5 (mutant): passes 2..R draw keys afresh (the per-key count check fails).
 * UNVERIFIED: that placing the extra passes right after pass 1, inside the same block, is
 * the right reading of "longer horizon"; it keeps the block order and lengthens the
 * interference distance of block 5 by (R - 1) * 48 objectives. */
static void extra_passes(uint64_t seed, uint32_t first, uint32_t n, uint32_t *seq) {
    uint64_t keep = rng_s;
    for (uint32_t pass = 2; pass <= g_reuse; pass++) {
        rng_s = seed ^ 0xA4D1F00Dull ^ ((uint64_t)stream[first].block << 40) ^ ((uint64_t)pass << 32); if (!rng_s) rng_s = 1;
        for (uint32_t i = 0; i < n; i++) {
            const Objective *src = &stream[first + i];
            Objective *o = &stream[stream_n++]; memset(o, 0, sizeof *o);
            o->seq = (*seq)++; o->block = src->block; o->kind = src->kind; o->key = src->key; o->rule = src->rule;
            if (g_mutant == MUT_M5) { uint32_t k = rnd_below(LS_POOL); o->key = pool[k].key; o->rule = pool[k].rule; }
            fill_examples(o); render_text(o);
        }
    }
    rng_s = keep;
}

static void build_stream(uint64_t seed, int control) {
    rng_s = seed ^ 0x9E3779B97F4A7C15ull; if (!rng_s) rng_s = 1;
    pool_n = 0; stream_n = 0;
    for (uint32_t i = 0; i < LS_POOL; i++) { pool[pool_n].key = rnd() | 1; pool[pool_n].rule = rnd_rule(rnd_len()); pool[pool_n].touched_after_block1 = 0; pool_n++; }
    uint32_t seq = 1;
    /* preface: malformed / ambiguous commands */
    for (uint32_t i = 0; i < LS_NPREFACE; i++) {
        Objective *o = &stream[stream_n++]; memset(o, 0, sizeof *o);
        o->seq = seq++; o->block = 0; o->malformed = 1; o->kind = K_SOLVE; o->key = pool[i].key; o->rule = pool[i].rule;
        fill_examples(o);
        switch (i % 4) {
        case 0: snprintf(o->text, sizeof o->text, "FROBNICATE k=%016" PRIx64 " w=32 ex=1:2", o->key); break;         /* unknown kind */
        case 1: snprintf(o->text, sizeof o->text, "SOLVE k=%016" PRIx64 " w=32", o->key); break;                     /* no examples */
        case 2: snprintf(o->text, sizeof o->text, "SOLVE k=%016" PRIx64 " w=32 ex=5:9,5:a", o->key); break;          /* conflicting */
        default: snprintf(o->text, sizeof o->text, "SOLVE k=%016" PRIx64 " w=16 ex=1:2", o->key); break;             /* width not frozen */
        }
    }
    uint32_t block1_keys[LS_POOL];
    /* block 1: first exposure */
    for (uint32_t i = 0; i < 64; i++) {
        Objective *o = &stream[stream_n++]; memset(o, 0, sizeof *o);
        o->seq = seq++; o->block = 1; o->kind = K_SOLVE; o->key = pool[i].key; o->rule = pool[i].rule; block1_keys[i] = i;
        fill_examples(o); render_text(o);
    }
    uint32_t first2 = stream_n;
    /* block 2: repeated */
    for (uint32_t i = 0; i < 48; i++) {
        uint32_t k = rnd_below(64); pool[k].touched_after_block1 = 1;
        Objective *o = &stream[stream_n++]; memset(o, 0, sizeof *o);
        o->seq = seq++; o->block = 2; o->kind = K_SOLVE; o->key = pool[k].key; o->rule = pool[k].rule;
        fill_examples(o); render_text(o);
    }
    extra_passes(seed, first2, LS_NREP, &seq);
    /* block 3: novel bindings: seen shape, new constants */
    for (uint32_t i = 0; i < 32; i++) {
        uint32_t k = rnd_below(64);
        Body r = pool[k].rule;
        for (uint32_t s = 0; s < r.n; s++) { uint64_t c; do c = LS_D[rnd_below(LS_NCONST)]; while (c == pool[k].rule.imm[s]); r.imm[s] = c; }
        pool[pool_n].key = rnd() | 1; pool[pool_n].rule = r; pool[pool_n].touched_after_block1 = 1;
        Objective *o = &stream[stream_n++]; memset(o, 0, sizeof *o);
        o->seq = seq++; o->block = 3; o->kind = K_SOLVE; o->key = pool[pool_n].key; o->rule = r; pool_n++;
        fill_examples(o); render_text(o);
    }
    /* block 4: compositions of seen keys (unseen-composition control: of keys never in the stream) */
    for (uint32_t i = 0; i < 32; i++) {
        uint32_t a = rnd_below(64), b = rnd_below(64);
        Body r; memset(&r, 0, sizeof r);
        Body ra = pool[a].rule, rb = pool[b].rule;
        if (control == CTL_UNSEEN) { ra = rnd_rule(rnd_len()); rb = rnd_rule(rnd_len()); }
        for (uint32_t s = 0; s < ra.n; s++) { r.op[r.n] = ra.op[s]; r.imm[r.n++] = ra.imm[s]; }
        for (uint32_t s = 0; s < rb.n; s++) { r.op[r.n] = rb.op[s]; r.imm[r.n++] = rb.imm[s]; }
        pool[a].touched_after_block1 = pool[b].touched_after_block1 = 1;
        Objective *o = &stream[stream_n++]; memset(o, 0, sizeof *o);
        o->seq = seq++; o->block = 4; o->kind = K_COMPOSE; o->key = rnd() | 1; o->rule = r;
        o->key_a = control == CTL_UNSEEN ? (rnd() | 1) : pool[a].key;
        o->key_b = control == CTL_UNSEEN ? (rnd() | 1) : pool[b].key;
        fill_examples(o); render_text(o);
    }
    /* block 5: delayed recall on untouched block-1 keys */
    uint32_t untouched[LS_POOL], nu = 0;
    for (uint32_t i = 0; i < 64; i++) if (!pool[i].touched_after_block1) untouched[nu++] = i;
    uint32_t first5 = stream_n;
    for (uint32_t i = 0; i < 24; i++) {
        uint32_t k = nu ? untouched[i % nu] : block1_keys[i];
        Objective *o = &stream[stream_n++]; memset(o, 0, sizeof *o);
        o->seq = seq++; o->block = 5; o->kind = K_RECALL; o->key = pool[k].key; o->rule = pool[k].rule;
        fill_examples(o); render_text(o);
    }
    extra_passes(seed, first5, LS_NRECALL, &seq);
    /* block 6: changed rules on seen keys */
    for (uint32_t i = 0; i < 24; i++) {
        uint32_t k = rnd_below(64);
        Body r; do r = rnd_rule(rnd_len()); while (memcmp(&r, &pool[k].rule, sizeof r) == 0);
        pool[k].rule = r;
        Objective *o = &stream[stream_n++]; memset(o, 0, sizeof *o);
        o->seq = seq++; o->block = 6; o->kind = K_SOLVE; o->key = pool[k].key; o->rule = r;
        fill_examples(o); render_text(o);
    }
    /* block 7: correction. The rule changes to r' which agrees with the old rule on the
     * 4 structured inputs (the only examples given), so retained state passes the
     * applicability check and the committed answer is wrong; the counterexample (a
     * random input where old and new differ) is the human correction. The second
     * answer is scored on the new rule. When no agreeing r' is found in 4096 tries the
     * change is an ordinary one (refused up front) and is counted as such. */
    for (uint32_t i = 0; i < 16; i++) {
        uint32_t k = rnd_below(64);
        Body old = pool[k].rule, r; int agreeing = 0;
        for (uint32_t t = 0; t < 4096 && !agreeing; t++) {
            r = rnd_rule(rnd_len());
            if (memcmp(&r, &old, sizeof r) == 0) continue;
            int ok = 1; for (uint32_t s = 0; s < 4; s++) if (interp(&r, STRUCTURED[s]) != interp(&old, STRUCTURED[s])) ok = 0;
            if (!ok) continue;
            int differs = 0; for (uint32_t s = 0; s < 16 && !differs; s++) { uint64_t x = rnd() & wmask(); differs = interp(&r, x) != interp(&old, x); }
            agreeing = differs;
        }
        if (!agreeing) do r = rnd_rule(rnd_len()); while (memcmp(&r, &old, sizeof r) == 0);
        Objective *o = &stream[stream_n++]; memset(o, 0, sizeof *o);
        o->seq = seq++; o->block = 7; o->kind = K_CORRECT; o->key = pool[k].key; o->rule = r; o->n_ex = 4;
        fill_examples(o);                       /* 4 structured examples under the NEW rule */
        pool[k].rule = r;
        uint32_t tries = 0;
        do { o->fix_x = rnd() & wmask(); o->fix_y = interp(&r, o->fix_x); } while (interp(&old, o->fix_x) == o->fix_y && ++tries < 4096);
        o->key_a = agreeing;                    /* recorded: correction scenario realized */
        render_text(o);
    }
    if (control == CTL_SHUFFLE) {
        /* feedback of objective i replaced by that of a random other objective */
        for (uint32_t i = LS_NPREFACE; i < stream_n; i++) {
            uint32_t j = LS_NPREFACE + rnd_below(stream_n - LS_NPREFACE);
            Objective *o = &stream[i], *q = &stream[j];
            memcpy(o->ex_y, q->ex_y, sizeof o->ex_y);
            memcpy(o->ex_x, q->ex_x, sizeof o->ex_x);
            o->n_ex = q->n_ex;
            o->fix_x = q->fix_x; o->fix_y = q->fix_y;
            render_text(o);
        }
    }
}

/* ---- adapter: bounded text command -> goal fields (PL_OT_GOAL layout) ---- */
typedef struct {
    uint64_t seq, kind, max_steps, energy_budget, facts_ref;   /* the five PL_OT_GOAL fields */
    uint64_t key, key_a, key_b, fix_x, fix_y;
    uint32_t n_ex, width; uint64_t ex_x[LS_NEX], ex_y[LS_NEX];
    int has_fix;
    uint32_t bytes; uint64_t parse_cpu_ns;
    const char *why;                      /* non-NULL: clarification needed (abstain) */
} Goal;

static int parse_goal(const char *text, uint64_t seq, Goal *g) {
    memset(g, 0, sizeof *g);
    uint64_t c0 = cpu_ns();
    g->seq = seq; g->bytes = (uint32_t)strlen(text); g->max_steps = LS_MAX_ACTION; g->energy_budget = 0;
    char kind[16] = {0};
    const char *p = text;
    int n = 0;
    if (sscanf(p, "%15s%n", kind, &n) != 1) { g->why = "empty"; goto out; }
    p += n;
    g->kind = !strcmp(kind, "SOLVE") ? K_SOLVE : !strcmp(kind, "RECALL") ? K_RECALL : !strcmp(kind, "COMPOSE") ? K_COMPOSE : !strcmp(kind, "CORRECT") ? K_CORRECT : 0;
    if (!g->kind) { g->why = "unknown command kind"; goto out; }
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (!strncmp(p, "k=", 2)) { g->key = strtoull(p + 2, (char **)&p, 16); }
        else if (!strncmp(p, "a=", 2)) { g->key_a = strtoull(p + 2, (char **)&p, 16); }
        else if (!strncmp(p, "b=", 2)) { g->key_b = strtoull(p + 2, (char **)&p, 16); }
        else if (!strncmp(p, "w=", 2)) { g->width = (uint32_t)strtoul(p + 2, (char **)&p, 10); }
        else if (!strncmp(p, "fix=", 4)) { g->fix_x = strtoull(p + 4, (char **)&p, 16); if (*p != ':') { g->why = "bad fix"; goto out; } g->fix_y = strtoull(p + 1, (char **)&p, 16); g->has_fix = 1; }
        else if (!strncmp(p, "ex=", 3)) {
            p += 3;
            while (*p && *p != ' ') {
                if (g->n_ex >= LS_NEX) { g->why = "too many examples"; goto out; }
                g->ex_x[g->n_ex] = strtoull(p, (char **)&p, 16);
                if (*p != ':') { g->why = "bad example"; goto out; }
                g->ex_y[g->n_ex] = strtoull(p + 1, (char **)&p, 16);
                g->n_ex++;
                if (*p == ',') p++;
            }
        } else { g->why = "unknown field"; goto out; }
    }
    if (!g->key) { g->why = "no key"; goto out; }
    if (g->width != LS_WIDTH) { g->why = "width not in the frozen domain"; goto out; }
    if (g->n_ex == 0) { g->why = "no examples: nothing to solve"; goto out; }
    if (g->kind == K_COMPOSE && (!g->key_a || !g->key_b)) { g->why = "compose without components"; goto out; }
    if (g->kind == K_CORRECT && !g->has_fix) { g->why = "correct without counterexample"; goto out; }
    for (uint32_t i = 0; i < g->n_ex; i++) {
        if (g->ex_x[i] & ~wmask() || g->ex_y[i] & ~wmask()) { g->why = "example outside the width"; goto out; }
        for (uint32_t j = 0; j < i; j++) if (g->ex_x[i] == g->ex_x[j] && g->ex_y[i] != g->ex_y[j]) { g->why = "conflicting examples: clarify"; goto out; }
    }
    g->facts_ref = g->n_ex;
out:
    g->parse_cpu_ns = cpu_ns() - c0;
    return g->why ? -1 : 0;
}

/* ---- arm state z ---- */
typedef struct {
    uint64_t key; Body body; uint8_t semantic_id[32]; uint32_t status;
    uint64_t uses, successes, failures, cx_id;      /* cx_id: Cortex record of the candidate */
    int has_real; RealizationObject real;           /* VERIFIED: the machine-bound realization */
} Entry;

typedef struct {
    int learn_table, learn_order, learn_compose, update_on;
    Entry t[LS_TABLE_CAP]; uint32_t n, stored, dedup, refused_full;
    uint16_t shape_count[LS_NSHAPES];
    uint16_t const_count[LS_NOPS][LS_NCONST];
} Arm;

static size_t arm_state_bytes(const Arm *a) {
    size_t b = 0;
    if (a->learn_table) for (uint32_t i = 0; i < a->n; i++) b += sizeof(uint64_t) + sizeof(Body) + 32 + 4 + (a->t[i].has_real ? 96 + a->t[i].real.code_len : 0);
    if (a->learn_order) b += sizeof a->shape_count + sizeof a->const_count;
    return b;
}
static Entry *table_find(Arm *a, uint64_t key) { for (uint32_t i = 0; i < a->n; i++) if (a->t[i].key == key) return &a->t[i]; return NULL; }

/* ---- per-objective accounting ---- */
typedef struct {
    uint32_t seq, block, kind;
    int outcome, path;                 /* path: 1 search, 2 reuse, 3 compose, 4 abstain */
    uint32_t candidates, realizations, lookups, hits, refusals, compose_tries, attempts, native_execs, verified_reuse;
    uint64_t cpu_ns, wall_ns, adapter_cpu_ns, adapter_bytes, persist_cpu_ns;
    uint32_t steps, verify_inputs, consistent_with_examples, sealed_agree;
    uint32_t bad_rows;                 /* harness checks that failed (mutant honesty) */
    char why[96];
} Row;

typedef struct {
    uint32_t n_obj, success, wrong, abstain, failed_search, refused_realize, clarify;
    uint32_t preface_n, preface_abstained, preface_committed;
    uint32_t by_block_n[8], by_block_success[8], by_block_hits[8], by_block_reuse_opp[8];
    uint64_t cpu_total, cpu_by_block[8], cands_total, realizations_total;
    uint32_t inconsistent_commits, verify_mismatch, unknown_op_rejected;
    uint32_t table_stored, table_dedup, table_refused_full, stale_marked, promotions;
    uint32_t mutant_bad_rows;
    size_t peak_state_bytes; uint64_t persist_bytes, persist_cpu;
    uint32_t restart_loaded; int journal_refused;
} Sum;

/* ---- lanes: one arm's whole state (z, Cortex journal, counters, rows). Several lanes
 * run interleaved per objective in one process (AMENDMENTS A4); every per-arm global of
 * the sealed harness lives here, so lanes cannot see each other. L is the lane running. */
typedef struct {
    char name[8]; Arm a;
    CxStore cx; int cx_on; uint64_t cx_t, persist_bytes, persist_cpu;
    Row *rows; uint32_t nrows; Sum S;
    uint32_t reload_ok, reload_bad;
    char out[512], jpath[600]; FILE *rf; int stale_applied; uint64_t wall_rows;
} Lane;
static Lane *L;

/* ---- Cortex persistence (FREEZE.md section 9) ---- */
#define LS_CX_VERSION 1ull
#define LS_CX_SUBJECT 1ull
#define CX_TOKEN 0x584584ull

static uint64_t cx_store_candidate(const Entry *e) {
    if (!L->cx_on) return 0;
    uint64_t c0 = cpu_ns();
    uint64_t w[1 + 1 + 4 + 1 + LS_MAX_ACTION * 2];
    uint32_t n = 0;
    w[n++] = LS_CX_VERSION; w[n++] = e->key;
    for (int i = 0; i < 4; i++) { uint64_t v = 0; for (int j = 0; j < 8; j++) v |= (uint64_t)e->semantic_id[i * 8 + j] << (8 * j); w[n++] = v; }
    w[n++] = e->body.n;
    for (uint32_t i = 0; i < e->body.n; i++) { w[n++] = e->body.op[i]; w[n++] = e->body.imm[i]; }
    CxHeader h = { CX_CLAIM, CX_K_CANDIDATE, LS_CX_SUBJECT, ++L->cx_t, 1, 0, 0, 0, { 0, 0, 0, 0 } };
    uint64_t id = 0;
    if (cx_append_as(&L->cx, CX_TOKEN, &h, w, n, &id) != CX_OK) id = 0;
    L->persist_bytes += 13 * 8 + n * 8 + 32;
    L->persist_cpu += cpu_ns() - c0;
    return id;
}
static void cx_promote_entry(Entry *e, const RealizationObject *real) {
    if (!L->cx_on || !e->cx_id) return;
    uint64_t c0 = cpu_ns();
    /* evidence payload: realization id (4), machine id (4), code_len, code words */
    uint64_t w[9 + AARCH64_MAX_CODE_BYTES / 8];
    uint32_t n = 0;
    for (int i = 0; i < 4; i++) { uint64_t v = 0; for (int j = 0; j < 8; j++) v |= (uint64_t)real->realization_id.bytes[i * 8 + j] << (8 * j); w[n++] = v; }
    for (int i = 0; i < 4; i++) { uint64_t v = 0; for (int j = 0; j < 8; j++) v |= (uint64_t)real->machine_id.bytes[i * 8 + j] << (8 * j); w[n++] = v; }
    w[n++] = real->code_len;
    for (size_t i = 0; i < real->code_len; i += 8) { uint64_t v = 0; for (size_t j = 0; j < 8 && i + j < real->code_len; j++) v |= (uint64_t)real->code_bytes[i + j] << (8 * j); w[n++] = v; }
    CxHeader h = { CX_EVIDENCE, CX_K_EVIDENCE_REF, LS_CX_SUBJECT, ++L->cx_t, 1, 0, CX_PROT_VERIFY_EVIDENCE, 0, { e->cx_id, 0, 0, 0 } };
    uint64_t ev = 0, pr = 0;
    if (cx_append_as(&L->cx, CX_TOKEN, &h, w, n, &ev) == CX_OK) cx_promote(&L->cx, CX_TOKEN, e->cx_id, ev, ++L->cx_t, &pr);
    L->persist_bytes += 2 * (13 * 8 + 32) + n * 8;
    L->persist_cpu += cpu_ns() - c0;
}

/* Rebuild z from the journal: newest verified candidate per key; VERIFIED only with a
 * promotion record (OLD-or-NEW). Returns entries loaded, or -1 when the journal is refused. */
typedef struct { Arm *a; uint64_t promoted[4096], evidence[4096]; uint32_t np; uint32_t bound_ok, bound_bad; } Reload;
static int visit_promotion(void *u, const CxObject *o) { Reload *r = u; if (o->kind == CX_K_PROMOTION && r->np < 4096) { r->promoted[r->np] = o->links[0]; r->evidence[r->np++] = o->links[1]; } return 0; }
/* Restore a VERIFIED entry's realization from its evidence record; the triple id must
 * recompute from the stored bytes (digest-bound), else the entry stays CANDIDATE. */
static int restore_real(Entry *e, uint64_t ev_id, Reload *r) {
    const CxObject *o = cx_get(&L->cx, ev_id);
    if (!o || o->kind != CX_K_EVIDENCE_REF || cx_verify(&L->cx, ev_id) != CX_OK) return -1;
    const uint64_t *w = cx_payload(&L->cx, o);
    if (o->n < 9) return -1;
    RealizationObject real; memset(&real, 0, sizeof real);
    for (int i = 0; i < 4; i++) for (int j = 0; j < 8; j++) { real.realization_id.bytes[i * 8 + j] = (uint8_t)(w[i] >> (8 * j)); real.machine_id.bytes[i * 8 + j] = (uint8_t)(w[4 + i] >> (8 * j)); }
    real.code_len = (size_t)w[8];
    if (real.code_len > AARCH64_MAX_CODE_BYTES || o->n != 9u + (real.code_len + 7) / 8) return -1;
    for (size_t i = 0; i < real.code_len; i++) real.code_bytes[i] = (uint8_t)(w[9 + i / 8] >> (8 * (i % 8)));
    memcpy(real.semantic_id.bytes, e->semantic_id, 32);
    real.has_id = real.has_machine_id = true; real.target_profile = 0; real.entry_offset = 0;
    OmegaMachineGraph mg; omega_machine_build_dgx_spark(&mg);
    real.target_profile = mg.target_profile;
    SemanticId id;
    if (omega_realize_compute_triple_id(&real.semantic_id, &real.machine_id, &real, &id) != 0 || memcmp(id.bytes, real.realization_id.bytes, 32) != 0) { r->bound_bad++; return -1; }
    e->real = real; e->has_real = 1; r->bound_ok++;
    return 0;
}
static int visit_candidate(void *u, const CxObject *o) {
    Reload *r = u;
    if (o->kind != CX_K_CANDIDATE) return 0;
    if (cx_verify(&L->cx, o->id) != CX_OK) return 0;
    const uint64_t *w = cx_payload(&L->cx, o);
    if (o->n < 7 || w[0] != LS_CX_VERSION) return 0;
    Entry e; memset(&e, 0, sizeof e);
    e.key = w[1];
    for (int i = 0; i < 4; i++) for (int j = 0; j < 8; j++) e.semantic_id[i * 8 + j] = (uint8_t)(w[2 + i] >> (8 * j));
    e.body.n = (uint8_t)w[6];
    if (e.body.n > LS_MAX_ACTION || o->n != 7u + 2u * e.body.n) return 0;
    for (uint32_t i = 0; i < e.body.n; i++) { e.body.op[i] = (uint8_t)w[7 + 2 * i]; e.body.imm[i] = w[8 + 2 * i]; }
    e.cx_id = o->id; e.status = ST_CANDIDATE;
    for (uint32_t i = 0; i < r->np; i++) if (r->promoted[i] == o->id) { if (restore_real(&e, r->evidence[i], r) == 0) e.status = ST_VERIFIED; }
    /* identity must recompute: digest-bound reload */
    OmegaProgram p; if (make_prog(&p, &e.body) != 0 || memcmp(p.program_id.bytes, e.semantic_id, 32) != 0) return 0;
    Entry *have = table_find(r->a, e.key);
    if (have) *have = e;                          /* newest wins (time order) */
    else if (r->a->n < LS_TABLE_CAP) r->a->t[r->a->n++] = e;
    return 0;
}
static int reload_from_cortex(Arm *a) {
    static Reload r; memset(&r, 0, sizeof r); r.a = a;
    CxFilter f = { 0, 0, 0, 1 };
    cx_range(&L->cx, LS_CX_SUBJECT, 0, UINT64_MAX, &f, visit_promotion, &r);
    cx_range(&L->cx, LS_CX_SUBJECT, 0, UINT64_MAX, &f, visit_candidate, &r);
    L->reload_ok = r.bound_ok; L->reload_bad = r.bound_bad;
    return (int)a->n;
}


/* Search in an order: canonical (A, B) or learned (C). Returns 1 and fills *out when a
 * consistent program is found within the budget; counts candidates. */
static int consistent(const Body *b, const Goal *g) { for (uint32_t i = 0; i < g->n_ex; i++) if (interp(b, g->ex_x[i]) != g->ex_y[i]) return 0; return 1; }

typedef struct { uint32_t shape; uint32_t score; } ShapeRank;
static int cmp_rank(const void *x, const void *y) {
    const ShapeRank *a = x, *b = y;
    if (a->score != b->score) return a->score > b->score ? -1 : 1;
    return a->shape < b->shape ? -1 : a->shape > b->shape;
}
static void shape_body(uint32_t s, Body *b) {
    uint32_t n = s < 5 ? 1 : s < 30 ? 2 : 3, idx = s - (n == 1 ? 0 : n == 2 ? 5 : 30);
    memset(b, 0, sizeof *b); b->n = (uint8_t)n;
    for (int i = (int)n - 1; i >= 0; i--) { b->op[i] = LS_OPS[idx % LS_NOPS]; idx /= LS_NOPS; }
}
static int search(Arm *a, const Goal *g, Body *out, uint32_t *cands) {
    ShapeRank rank[LS_NSHAPES];
    for (uint32_t s = 0; s < LS_NSHAPES; s++) { rank[s].shape = s; rank[s].score = a->learn_order ? a->shape_count[s] : 0; }
    if (a->learn_order) qsort(rank, LS_NSHAPES, sizeof rank[0], cmp_rank);
    *cands = 0;
    for (uint32_t r = 0; r < LS_NSHAPES; r++) {
        Body b; shape_body(rank[r].shape, &b);
        /* constant order per step: learned frequency for that op (C) or D order */
        uint32_t order[LS_MAX_RULE][LS_NCONST];
        for (uint32_t s = 0; s < b.n; s++) {
            uint32_t o = 0; while (LS_OPS[o] != b.op[s]) o++;
            for (uint32_t i = 0; i < LS_NCONST; i++) order[s][i] = i;
            if (a->learn_order)
                for (uint32_t i = 1; i < LS_NCONST; i++) for (uint32_t j = i; j > 0 && a->const_count[o][order[s][j - 1]] < a->const_count[o][order[s][j]]; j--) { uint32_t t = order[s][j]; order[s][j] = order[s][j - 1]; order[s][j - 1] = t; }
        }
        uint32_t total = 1; for (uint32_t s = 0; s < b.n; s++) total *= LS_NCONST;
        for (uint32_t c = 0; c < total; c++) {
            uint32_t cc = c;
            for (int s = (int)b.n - 1; s >= 0; s--) { b.imm[s] = LS_D[order[s][cc % LS_NCONST]]; cc /= LS_NCONST; }
            if (++*cands > LS_SEARCH_BUDGET) return 0;
            if (consistent(&b, g)) { *out = b; return 1; }
        }
    }
    return 0;
}

/* Realize through Omega: synthesis + verification (native differential). */
static int realize(const Body *b, OmegaProgram *p, RealizationSynthesisResult *res, Row *row) {
    if (make_prog(p, b) != 0) { snprintf(row->why, sizeof row->why, "program id refused"); return -1; }
    row->realizations++;
    int rc = omega_synthesize_for_dgx_spark(p, res);
    if (rc != 0) { snprintf(row->why, sizeof row->why, "realize refused: %.60s", res->why); return -1; }
    if (g_mutant == MUT_M1) {
        /* flip one immediate inside the realized code after synthesis; the differential
         * re-verification below must catch it (bad rows > 0) */
        if (res->realization.code_len >= 8) res->realization.code_bytes[5] ^= 0x20;
        char why[192]; uint32_t checked = 0;
        OmegaMachineGraph mg; omega_machine_build_dgx_spark(&mg);
        int v = omega_realization_verify_program(p, &mg, &res->realization, &checked, why, sizeof why);
        if (v == 0) { row->bad_rows = 0; snprintf(row->why, sizeof row->why, "M1 NOT CAUGHT"); }
        else { row->bad_rows++; snprintf(row->why, sizeof row->why, "M1 caught: %.50s", why); }
        L->S.mutant_bad_rows += row->bad_rows;
        return -1;
    }
    row->verify_inputs = res->inputs_checked;
    return 0;
}

static void update_counts(Arm *a, const Body *b) {
    if (!a->learn_order || !a->update_on) return;
    if (b->n <= LS_MAX_RULE) { uint32_t s = shape_index(b); if (a->shape_count[s] < 65535) a->shape_count[s]++; }
    for (uint32_t i = 0; i < b->n; i++) { uint32_t o = 0; while (LS_OPS[o] != b->op[i]) o++; uint32_t c = const_index(b->imm[i]); if (c < LS_NCONST && a->const_count[o][c] < 65535) a->const_count[o][c]++; }
}
static Entry *table_store(Arm *a, uint64_t key, const Body *b, const uint8_t id[32]) {
    if (!a->learn_table || !a->update_on) return NULL;
    Entry *e = table_find(a, key);
    if (e) {
        if (memcmp(e->semantic_id, id, 32) == 0) { a->dedup++; e->status = ST_CANDIDATE; return e; }
        e->body = *b; memcpy(e->semantic_id, id, 32); e->status = ST_CANDIDATE; e->uses = e->successes = e->failures = 0;
        e->cx_id = cx_store_candidate(e); a->stored++; return e;
    }
    if (a->n >= LS_TABLE_CAP) { a->refused_full++; return NULL; }
    e = &a->t[a->n++]; memset(e, 0, sizeof *e);
    e->key = key; e->body = *b; memcpy(e->semantic_id, id, 32); e->status = ST_CANDIDATE;
    e->cx_id = cx_store_candidate(e); a->stored++;
    return e;
}

/* One attempt at a goal: reuse -> compose -> search; realize; commit. */
static int attempt(Arm *a, const Goal *g, Row *row, Body *committed, Entry **used) {
    Body b; int found = 0; *used = NULL;
    row->attempts++;
    if (a->learn_table) {
        row->lookups++;
        Entry *e = table_find(a, g->key);
        int m2_injected = 0;
        if (e && g_mutant == MUT_M2 && e->status != ST_STALE) {
            /* M2: the retained program is silently replaced by one that differs in one
             * constant (a lying entry). The applicability check must refuse it; if the
             * arm commits it anyway the sealed check scores it WRONG (the test can fail). */
            Body m = e->body; m.imm[0] = m.imm[0] == LS_D[0] ? LS_D[1] : LS_D[0];
            if (!consistent(&m, g)) { e->body = m; e->has_real = 0; m2_injected = 1; }
        }
        if (e) {
            if (e->status == ST_STALE) { row->refusals++; snprintf(row->why, sizeof row->why, "refused: stale"); }
            else if (!consistent(&e->body, g)) { row->refusals++; e->status = ST_STALE; L->S.stale_marked++; snprintf(row->why, sizeof row->why, "refused: examples disagree"); if (m2_injected) { row->bad_rows++; L->S.mutant_bad_rows++; } }
            else { b = e->body; found = 1; row->hits++; row->path = 2; *used = e; }
        }
    }
    if (!found && a->learn_compose && g->kind == K_COMPOSE) {
        Entry *ea = table_find(a, g->key_a), *eb = table_find(a, g->key_b);
        if (ea && eb && ea->status != ST_STALE && eb->status != ST_STALE && ea->body.n + eb->body.n <= LS_MAX_ACTION) {
            row->compose_tries++;
            memset(&b, 0, sizeof b);
            for (uint32_t s = 0; s < ea->body.n; s++) { b.op[b.n] = ea->body.op[s]; b.imm[b.n++] = ea->body.imm[s]; }
            for (uint32_t s = 0; s < eb->body.n; s++) { b.op[b.n] = eb->body.op[s]; b.imm[b.n++] = eb->body.imm[s]; }
            if (consistent(&b, g)) { found = 1; row->path = 3; }
            else { row->refusals++; snprintf(row->why, sizeof row->why, "compose candidate refused"); }
        }
    }
    if (!found) {
        uint32_t c = 0;
        found = search(a, g, &b, &c);
        row->candidates += c;
        if (!found) { row->outcome = OUT_FAILED_SEARCH; row->path = 4; snprintf(row->why, sizeof row->why, "search budget exhausted"); return -1; }
        row->path = row->path ? row->path : 1;
    }
    if (g_mutant == MUT_M2 && row->path == 2) {
        /* a retained program that reproduces all but one example must be refused */
        Body m = b; m.imm[0] = m.imm[0] == LS_D[0] ? LS_D[1] : LS_D[0];
        uint32_t agree = 0; for (uint32_t i = 0; i < g->n_ex; i++) agree += interp(&m, g->ex_x[i]) == g->ex_y[i];
        if (agree < g->n_ex && !consistent(&m, g)) { row->bad_rows++; L->S.mutant_bad_rows++; }
    }
    if (row->realizations >= LS_REALIZE_BUDGET) { row->outcome = OUT_REFUSED_REALIZE; snprintf(row->why, sizeof row->why, "realization budget"); return -1; }
    OmegaProgram p; static RealizationSynthesisResult res;
    const RealizationObject *real = NULL;
    if (*used && (*used)->status == ST_VERIFIED && (*used)->has_real) {
        /* VERIFIED reuse (rx_plan lifecycle): no reference re-run. The realization is
         * identity-bound: its triple id must recompute from the stored bytes. */
        SemanticId id;
        const RealizationObject *r = &(*used)->real;
        if (omega_realize_compute_triple_id(&r->semantic_id, &r->machine_id, r, &id) == 0 && memcmp(id.bytes, r->realization_id.bytes, 32) == 0 && memcmp(r->semantic_id.bytes, (*used)->semantic_id, 32) == 0) {
            real = r; row->verified_reuse = 1;
        } else { row->refusals++; (*used)->status = ST_STALE; L->S.stale_marked++; snprintf(row->why, sizeof row->why, "refused: realization identity does not bind"); return -1; }
    }
    if (!real) {
        if (realize(&b, &p, &res, row) != 0) { row->outcome = OUT_REFUSED_REALIZE; return -1; }
        real = &res.realization;
    }
    /* commit: native execution on the examples (the arm's own check of its answer) */
    for (uint32_t i = 0; i < g->n_ex; i++) {
        uint64_t y = 0;
        row->native_execs++;
        if (omega_exec_native_f3(real, g->ex_x[i], 0, 0, &y) != 0 || y != g->ex_y[i]) {
            row->outcome = OUT_REFUSED_REALIZE; snprintf(row->why, sizeof row->why, "native output disagrees with example"); L->S.inconsistent_commits++;
            if (*used) { (*used)->status = ST_STALE; L->S.stale_marked++; }
            return -1;
        }
    }
    row->consistent_with_examples = 1;
    row->steps = b.n;
    *committed = b;
    if (*used) {
        (*used)->uses++; (*used)->successes++;
        if ((*used)->status == ST_CANDIDATE && a->update_on) {
            (*used)->status = ST_VERIFIED; (*used)->real = *real; (*used)->has_real = 1; L->S.promotions++;
            cx_promote_entry(*used, real);
        }
    } else {
        Entry *e = table_store(a, g->key, &b, real->semantic_id.bytes);
        if (e) { e->real = *real; e->has_real = 1; }     /* kept in memory; persisted on promotion */
    }
    update_counts(a, &b);
    return 0;
}

static void run_objective(Arm *a, Objective *o, Row *row) {
    memset(row, 0, sizeof *row);
    row->seq = o->seq; row->block = o->block; row->kind = o->kind;
    uint64_t w0 = now_ns(), c0 = cpu_ns(), p0 = L->persist_cpu;
    Goal g;
    int prc = parse_goal(o->text, o->seq, &g);
    row->adapter_cpu_ns = g.parse_cpu_ns; row->adapter_bytes = g.bytes;
    if (prc != 0) {
        row->outcome = OUT_CLARIFY; row->path = 4; snprintf(row->why, sizeof row->why, "clarify: %.80s", g.why);
        if (o->malformed) L->S.preface_abstained++;
        row->cpu_ns = cpu_ns() - c0; row->wall_ns = now_ns() - w0; return;
    }
    if (o->malformed) { L->S.preface_committed++; }
    Body committed; Entry *used = NULL;
    int rc = attempt(a, &g, row, &committed, &used);
    if (rc == 0 && g.kind == K_CORRECT) {
        /* the counterexample arrives; charge both attempts */
        if (interp(&committed, g.fix_x) != g.fix_y) {
            if (used) { used->status = ST_STALE; L->S.stale_marked++; }
            Goal g2 = g;
            if (g2.n_ex < LS_NEX) { g2.ex_x[g2.n_ex] = g.fix_x; g2.ex_y[g2.n_ex] = g.fix_y; g2.n_ex++; }
            else { g2.ex_x[0] = g.fix_x; g2.ex_y[0] = g.fix_y; }
            row->path = 0; row->why[0] = 0;
            rc = attempt(a, &g2, row, &committed, &used);
        }
    }
    if (rc == 0) {
        uint32_t agree = 0;
        for (uint32_t i = 0; i < LS_NCHECK; i++) agree += interp(&committed, o->chk_x[i]) == o->chk_y[i];
        row->sealed_agree = agree;
        row->outcome = agree == LS_NCHECK ? OUT_SUCCESS : OUT_WRONG;
        if (row->outcome == OUT_WRONG) {
            snprintf(row->why, sizeof row->why, "sealed disagreement %u/%u (SAMPLED)", LS_NCHECK - agree, LS_NCHECK);
            if (used) { used->status = ST_STALE; L->S.stale_marked++; }
        }
    } else if (!row->outcome) row->outcome = OUT_ABSTAIN;
    row->persist_cpu_ns = L->persist_cpu - p0;
    row->cpu_ns = cpu_ns() - c0; row->wall_ns = now_ns() - w0;
}

static const char *outcome_name(int o) {
    switch (o) { case OUT_SUCCESS: return "success"; case OUT_WRONG: return "wrong"; case OUT_ABSTAIN: return "abstain"; case OUT_FAILED_SEARCH: return "failed_search"; case OUT_REFUSED_REALIZE: return "refused_realize"; case OUT_CLARIFY: return "clarify"; default: return "?"; }
}

static void apply_stale_control(Arm *a) {
    /* overwrite half of the retained entries with a different valid program (silently) */
    for (uint32_t i = 0; i < a->n; i += 2) {
        Body w = rnd_rule(rnd_len());
        if (memcmp(&w, &a->t[i].body, sizeof w) == 0) w.imm[0] = w.imm[0] == LS_D[0] ? LS_D[1] : LS_D[0];
        a->t[i].body = w; a->t[i].has_real = 0;      /* semantic id left as it was: the entry lies */
    }
}
static int usage(void) { fprintf(stderr, "ls584 run (--arm A|B|C0|C1 | --arms LIST) --seed HEX --out DIR [--reuse 1..6] [--control none|shuffle|nolearn|stale|unseen] [--restart-after N] [--mutant none|M1|M2|M3|M4|M5] [--no-persist]\n  LIST: comma list of lanes from A A2 B C0 C1 (A2 = a second copy of A, the same-code noise floor), run interleaved per objective\nls584 stream --seed HEX [--reuse R] [--mutant M5]   print seq,block,kind,key of the stream\nls584 neth C p Delta r Hmax\n"); return 2; }

static double neth(double C, double p, double D, double r, uint32_t H) {
    if (r >= 1.0) return (double)H * p * D - C;
    double g = 1.0; for (uint32_t i = 0; i < H; i++) g *= r;
    return p * D * (1.0 - g) / (1.0 - r) - C;
}

/* Lane order for objective i: a balanced (Williams) Latin square, so each lane runs in
 * each position equally often and directly after every other lane equally often.
 * UNVERIFIED: that this balance removes the order effect on this host; the A vs A2
 * difference is published as the measure of what it leaves. */
static void lane_order(uint32_t n, uint32_t i, uint32_t *ord) {
    uint32_t base[8], rows = n % 2 ? 2 * n : n, r = i % rows;
    for (uint32_t k = 0; k < n; k++) base[k] = k == 0 ? 0 : k % 2 ? (k + 1) / 2 : n - k / 2;   /* 0, 1, n-1, 2, n-2, ... */
    for (uint32_t k = 0; k < n; k++) ord[k] = (base[k] + r) % n;
    if (r >= n) for (uint32_t k = 0; k < n / 2; k++) { uint32_t t = ord[k]; ord[k] = ord[n - 1 - k]; ord[n - 1 - k] = t; }
}

static int lane_init(Lane *l, const char *name, const char *out, int persist) {
    memset(l, 0, sizeof *l);
    snprintf(l->name, sizeof l->name, "%s", name);
    Arm *a = &l->a;
    if (!strcmp(name, "A") || !strcmp(name, "A2")) { }
    else if (!strcmp(name, "B")) { a->learn_table = 1; }
    else if (!strcmp(name, "C0")) { a->learn_table = 1; a->learn_order = 1; }
    else if (!strcmp(name, "C1")) { a->learn_table = 1; a->learn_order = 1; a->learn_compose = 1; }
    else return -1;
    a->update_on = g_control != CTL_NOLEARN;
    if (g_control == CTL_NOLEARN) { a->learn_table = a->learn_order = a->learn_compose = 0; }   /* U = identity, pi = A */
    snprintf(l->out, sizeof l->out, "%s", out);
    mkdir(l->out, 0775);
    l->rows = calloc(LS_MAX_STREAM, sizeof *l->rows);
    if (!l->rows) return -1;
    snprintf(l->jpath, sizeof l->jpath, "%s/cortex.journal", l->out);
    l->cx_on = persist && a->learn_table;
    if (l->cx_on) {
        unlink(l->jpath);
        if (cx_open(&l->cx, l->jpath, 4, 0) != CX_OK || cx_claim_writer(&l->cx, CX_TOKEN) != CX_OK) { fprintf(stderr, "cortex open failed\n"); return -1; }
    }
    char rpath[600]; snprintf(rpath, sizeof rpath, "%s/rows.csv", l->out);
    l->rf = fopen(rpath, "w");
    if (!l->rf) return -1;
    fprintf(l->rf, "seq,block,kind,outcome,path,attempts,candidates,realizations,lookups,hits,refusals,compose_tries,steps,verify_inputs,sealed_agree,cpu_ns,wall_ns,adapter_cpu_ns,adapter_bytes,persist_cpu_ns,state_bytes,bad_rows,native_execs,verified_reuse,why\n");
    return 0;
}

static uint64_t g_rng_after_build;
static int g_restart_after;

/* One objective for lane L (the sealed harness's loop body, unchanged in effect). */
static void lane_step(Lane *l, Objective *o) {
    L = l;
    Arm *a = &l->a;
    if (g_control == CTL_STALE && o->block >= 2 && !l->stale_applied) {
        rng_s = g_rng_after_build;                /* every lane sees the same overwrite as a standalone run */
        apply_stale_control(a); l->stale_applied = 1;
    }
    if (g_restart_after && (int)o->seq == g_restart_after + (int)LS_NPREFACE + 1) {
        /* restart: drop process state, reopen the journal, rebuild z */
        if (l->cx_on) {
            cx_release_writer(&l->cx, CX_TOKEN); cx_close(&l->cx);
            if (g_mutant == MUT_M3) {
                FILE *jf = fopen(l->jpath, "r+b");
                if (jf) { fseek(jf, 32 + 13 * 8 + 8, SEEK_SET); uint64_t z = 0xDEADBEEFull; fwrite(&z, 8, 1, jf); fclose(jf); }
            }
            memset(&a->t, 0, sizeof a->t); a->n = 0; memset(a->shape_count, 0, sizeof a->shape_count); memset(a->const_count, 0, sizeof a->const_count);
            int rc = cx_open(&l->cx, l->jpath, 4, 0);
            if (rc != CX_OK) { l->S.journal_refused = 1; if (g_mutant == MUT_M3) l->S.mutant_bad_rows++; l->cx_on = 0; unlink(l->jpath); }
            else { cx_claim_writer(&l->cx, CX_TOKEN); l->cx_t = l->cx.n + 1; l->S.restart_loaded = (uint32_t)reload_from_cortex(a); }
        }
    }
    Row *row = &l->rows[l->nrows++];
    run_objective(a, o, row);
    l->wall_rows += row->wall_ns;
    size_t sb = arm_state_bytes(a); if (sb > l->S.peak_state_bytes) l->S.peak_state_bytes = sb;
    fprintf(l->rf, "%u,%u,%u,%s,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%zu,%u,%u,%u,%s\n",
            row->seq, row->block, row->kind, outcome_name(row->outcome), row->path, row->attempts, row->candidates, row->realizations,
            row->lookups, row->hits, row->refusals, row->compose_tries, row->steps, row->verify_inputs, row->sealed_agree,
            row->cpu_ns, row->wall_ns, row->adapter_cpu_ns, row->adapter_bytes, row->persist_cpu_ns, sb, row->bad_rows, row->native_execs, row->verified_reuse, row->why);
    Sum *S = &l->S;
    if (o->malformed) { S->preface_n++; return; }
    S->n_obj++;
    S->by_block_n[o->block]++; S->cpu_by_block[o->block] += row->cpu_ns; S->cpu_total += row->cpu_ns;
    S->cands_total += row->candidates; S->realizations_total += row->realizations;
    S->by_block_hits[o->block] += row->hits;
    if (o->block == 2 || o->block == 5) S->by_block_reuse_opp[o->block]++;
    switch (row->outcome) {
    case OUT_SUCCESS: S->success++; S->by_block_success[o->block]++; break;
    case OUT_WRONG: S->wrong++; break;
    case OUT_ABSTAIN: S->abstain++; break;
    case OUT_FAILED_SEARCH: S->failed_search++; break;
    case OUT_REFUSED_REALIZE: S->refused_realize++; break;
    case OUT_CLARIFY: S->clarify++; break;
    }
}

static int lane_finish(Lane *l, const char *lanes, uint64_t seed, const char *ctl, const char *mut, int persist, uint64_t wall_total, uint32_t pos) {
    Arm *a = &l->a; Sum *S = &l->S;
    fclose(l->rf);
    S->table_stored = a->stored; S->table_dedup = a->dedup; S->table_refused_full = a->refused_full;
    S->persist_bytes = l->persist_bytes; S->persist_cpu = l->persist_cpu;
    if (l->cx_on) { cx_release_writer(&l->cx, CX_TOKEN); cx_close(&l->cx); }
    char spath[600]; snprintf(spath, sizeof spath, "%s/summary.json", l->out);
    FILE *sf = fopen(spath, "w");
    if (!sf) return 1;
    double load1 = 0; { FILE *lf = fopen("/proc/loadavg", "r"); if (lf) { if (fscanf(lf, "%lf", &load1) != 1) load1 = 0; fclose(lf); } }
    fprintf(sf, "{\n  \"schema\": \"LS584_RUN_V1\", \"arm\": \"%s\", \"seed\": \"%" PRIx64 "\", \"control\": \"%s\", \"mutant\": \"%s\", \"restart_after\": %d, \"persist\": %d,\n",
            l->name, seed, ctl, mut, g_restart_after, l->cx_on || (persist && a->learn_table));
    fprintf(sf, "  \"reuse_per_key\": %u, \"lanes\": \"%s\", \"lane_index\": %u, \"lane_order\": \"%s\",\n", g_reuse, lanes, pos, strchr(lanes, ',') ? "williams" : "single");
    fprintf(sf, "  \"load1_at_end\": %.2f, \"wall_ns_total\": %" PRIu64 ", \"wall_ns_rows\": %" PRIu64 ", \"cpu_ns_total\": %" PRIu64 ",\n", load1, wall_total, l->wall_rows, S->cpu_total);
    fprintf(sf, "  \"objectives\": %u, \"success\": %u, \"wrong\": %u, \"abstain\": %u, \"failed_search\": %u, \"refused_realize\": %u, \"clarify\": %u,\n",
            S->n_obj, S->success, S->wrong, S->abstain, S->failed_search, S->refused_realize, S->clarify);
    fprintf(sf, "  \"preface\": {\"n\": %u, \"abstained\": %u, \"committed\": %u},\n", S->preface_n, S->preface_abstained, S->preface_committed);
    fprintf(sf, "  \"semantic\": {\"inconsistent_commits\": %u, \"mutant_bad_rows\": %u},\n", S->inconsistent_commits, S->mutant_bad_rows);
    fprintf(sf, "  \"candidates_total\": %" PRIu64 ", \"realizations_total\": %" PRIu64 ",\n", S->cands_total, S->realizations_total);
    fprintf(sf, "  \"table\": {\"stored\": %u, \"dedup\": %u, \"refused_full\": %u, \"stale_marked\": %u, \"promotions\": %u, \"entries_at_end\": %u},\n",
            S->table_stored, S->table_dedup, S->table_refused_full, S->stale_marked, S->promotions, a->n);
    fprintf(sf, "  \"state\": {\"peak_bytes\": %zu, \"persist_bytes\": %" PRIu64 ", \"persist_cpu_ns\": %" PRIu64 "},\n", S->peak_state_bytes, S->persist_bytes, S->persist_cpu);
    fprintf(sf, "  \"restart\": {\"loaded_entries\": %u, \"journal_refused\": %d, \"realizations_bound\": %u, \"realizations_unbound\": %u},\n", S->restart_loaded, S->journal_refused, l->reload_ok, l->reload_bad);
    fprintf(sf, "  \"joules\": \"UNMEASURED\",\n  \"blocks\": {\n");
    static const char *bn[8] = { "preface", "first_exposure", "repeated", "novel_bindings", "compositions", "delayed_recall", "changed_rules", "correction" };
    static uint64_t v[LS_MAX_STREAM];
    for (uint32_t b = 1; b < 8; b++) {
        uint32_t n = 0;
        for (uint32_t i = 0; i < l->nrows; i++) if (l->rows[i].block == b) v[n++] = l->rows[i].cpu_ns;
        for (uint32_t i = 1; i < n; i++) for (uint32_t j = i; j > 0 && v[j - 1] > v[j]; j--) { uint64_t t = v[j]; v[j] = v[j - 1]; v[j - 1] = t; }
        uint64_t med = n ? v[n / 2] : 0;
        fprintf(sf, "    \"%s\": {\"n\": %u, \"success\": %u, \"hits\": %u, \"cpu_ns_total\": %" PRIu64 ", \"cpu_ns_median\": %" PRIu64 "}%s\n",
                bn[b], S->by_block_n[b], S->by_block_success[b], S->by_block_hits[b], S->cpu_by_block[b], med, b < 7 ? "," : "");
    }
    fprintf(sf, "  }\n}\n");
    fclose(sf);
    printf("ls584 %s seed=%" PRIx64 " reuse=%u control=%s mutant=%s: objectives=%u success=%u wrong=%u abstain=%u failed_search=%u refused=%u cpu_ms=%.1f peak_state_B=%zu mutant_bad_rows=%u\n",
           l->name, seed, g_reuse, ctl, mut, S->n_obj, S->success, S->wrong, S->abstain, S->failed_search, S->refused_realize, S->cpu_total / 1e6, S->peak_state_bytes, S->mutant_bad_rows);
    free(l->rows);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) return usage();
    if (!strcmp(argv[1], "neth")) {
        if (argc < 7) return usage();
        double C = atof(argv[2]), p = atof(argv[3]), D = atof(argv[4]), r = atof(argv[5]); uint32_t Hmax = (uint32_t)atoi(argv[6]);
        int be = -1; double maxdiff = 0;
        printf("{\"C\":%g,\"p\":%g,\"Delta\":%g,\"r\":%g,\"no_payoff\":%s,\"net\":[", C, p, D, r,
               (r < 1.0 && C >= p * D / (1.0 - r)) ? "true" : "false");
        double direct = -C;
        for (uint32_t H = 1; H <= Hmax; H++) {
            double g = 1.0; for (uint32_t i = 1; i < H; i++) g *= r;    /* r^(H-1) */
            direct += p * D * (r >= 1.0 ? 1.0 : g);                     /* direct finite sum */
            double v = neth(C, p, D, r, H);
            double d = v - direct; if (d < 0) d = -d; if (d > maxdiff) maxdiff = d;
            if (be < 0 && v >= 0) be = (int)H;
            printf("%s%.6g", H > 1 ? "," : "", v);
        }
        printf("],\"break_even_H\":%d,\"max_abs_diff_closed_vs_direct\":%g}\n", be, maxdiff);
        return 0;
    }
    int is_stream = !strcmp(argv[1], "stream");
    if (strcmp(argv[1], "run") && !is_stream) return usage();
    const char *arm_name = NULL, *lanes_arg = NULL, *out = NULL, *ctl = "none", *mut = "none"; uint64_t seed = 0x5841; int persist = 1;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--arm") && i + 1 < argc) arm_name = argv[++i];
        else if (!strcmp(argv[i], "--arms") && i + 1 < argc) lanes_arg = argv[++i];
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = strtoull(argv[++i], NULL, 16);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--reuse") && i + 1 < argc) g_reuse = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--control") && i + 1 < argc) ctl = argv[++i];
        else if (!strcmp(argv[i], "--mutant") && i + 1 < argc) mut = argv[++i];
        else if (!strcmp(argv[i], "--restart-after") && i + 1 < argc) g_restart_after = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--no-persist")) persist = 0;
        else return usage();
    }
    if (g_reuse < 1 || g_reuse > LS_MAX_REUSE) return usage();
    g_control = !strcmp(ctl, "none") ? CTL_NONE : !strcmp(ctl, "shuffle") ? CTL_SHUFFLE : !strcmp(ctl, "nolearn") ? CTL_NOLEARN : !strcmp(ctl, "stale") ? CTL_STALE : !strcmp(ctl, "unseen") ? CTL_UNSEEN : -1;
    g_mutant = !strcmp(mut, "none") ? MUT_NONE : !strcmp(mut, "M1") ? MUT_M1 : !strcmp(mut, "M2") ? MUT_M2 : !strcmp(mut, "M3") ? MUT_M3 : !strcmp(mut, "M4") ? MUT_M4 : !strcmp(mut, "M5") ? MUT_M5 : -1;
    if (g_control < 0 || g_mutant < 0) return usage();
    stream = calloc(LS_MAX_STREAM, sizeof *stream);
    if (!stream) return 1;
    build_stream(seed, g_control);
    g_rng_after_build = rng_s;
    if (is_stream) {
        for (uint32_t i = 0; i < stream_n; i++) printf("%u,%u,%u,%016" PRIx64 "\n", stream[i].seq, stream[i].block, stream[i].kind, stream[i].key);
        free(stream);
        return 0;
    }
    if (!out || !!arm_name == !!lanes_arg) return usage();

    /* lanes: one (--arm, output in DIR as in the sealed harness) or several (--arms,
     * output in DIR/<lane>), run interleaved per objective in lane_order */
    static Lane lanes[LS_MAX_LANES];
    char names[LS_MAX_LANES][8]; uint32_t nl = 0;
    if (arm_name) snprintf(names[nl++], 8, "%s", arm_name);
    else for (const char *p = lanes_arg; *p && nl < LS_MAX_LANES; ) {
        size_t k = strcspn(p, ",");
        if (k == 0 || k > 7) return usage();
        memcpy(names[nl], p, k); names[nl][k] = 0;
        for (uint32_t j = 0; j < nl; j++) if (!strcmp(names[j], names[nl])) return usage();
        nl++; p += k; if (*p == ',') p++;
    }
    if (lanes_arg) mkdir(out, 0775);
    for (uint32_t j = 0; j < nl; j++) {
        char d[512];
        if (arm_name) snprintf(d, sizeof d, "%s", out); else snprintf(d, sizeof d, "%s/%s", out, names[j]);
        if (lane_init(&lanes[j], names[j], d, persist) != 0) return usage();
    }
    /* M4 (lane-isolation mutant): lane A2, when present, runs on lane B's state, a shared-
     * state leak between lanes; the decision-identity check (A2 rows == A rows, every lane
     * == its standalone run) must go red. */
    int leak_from = -1, leak_to = -1;
    if (g_mutant == MUT_M4) for (uint32_t j = 0; j < nl; j++) { if (!strcmp(names[j], "B")) leak_from = (int)j; if (!strcmp(names[j], "A2")) leak_to = (int)j; }
    uint64_t wall0 = now_ns();
    uint32_t ord[LS_MAX_LANES];
    for (uint32_t i = 0; i < stream_n; i++) {
        lane_order(nl, i, ord);
        for (uint32_t k = 0; k < nl; k++) {
            Lane *l = &lanes[ord[k]];
            if (leak_from >= 0 && leak_to >= 0 && (int)ord[k] == leak_to) { Arm keep = l->a; l->a = lanes[leak_from].a; lane_step(l, &stream[i]); lanes[leak_from].a = l->a; l->a = keep; }
            else lane_step(l, &stream[i]);
        }
    }
    uint64_t wall_total = now_ns() - wall0;
    int rc = 0;
    for (uint32_t j = 0; j < nl; j++) rc |= lane_finish(&lanes[j], lanes_arg ? lanes_arg : arm_name, seed, ctl, mut, persist, wall_total, j);
    free(stream);
    return rc;
}
