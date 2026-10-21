/* omega_osc_gpu.c -- the GPU back end for Omegese kernels (L386). Interface and scope: omega_osc_gpu.h;
 * meaning of every operation: docs/omegese/GPU.md (section 11 is this file's contract).
 *
 * Shape of the code:
 *   prologue   memory descriptor, every argument from constant bank 0, constants 1 and 0, the
 *              poison flag 0, shared memory zeroed by a strided loop, BAR.SYNC
 *   blocks     in IR order. Each block is [joins][arms][body][terminator]:
 *                joins: one BSYNC per reconvergence region whose join is this block, innermost first;
 *                arms:  one BSSY per region whose head is this block, outermost first;
 *              an edge from block u lands on the first join u belongs to, else on the first arm u is
 *              outside of, else on the body. Every block ends in explicit branches (no fall through).
 *   tail       EXIT, then the self branch.
 * Regions: a branch on a varying condition (section 6 of GPU.md) whose nearest join J is a block gets a
 * region: its members are the blocks reachable from the branch without passing J; its head is the one
 * block every edge into the members enters (the branch block, or a loop head when the branch is inside a
 * loop J is outside of). A branch whose paths only meet at the virtual exit gets none: the lanes that
 * leave through a ret exit, the rest stay together; it is refused if a region could start after it.
 * Barrier registers: two regions get different ones when one contains the other or both lie inside a
 * common region (lanes in different arms could hold both at once); otherwise they may share one.
 * Traps inside an operation: a predicate holds the fault; under it the thread stores 1 to status word
 * N, sets its poison flag and takes 0 as the result. Every store is guarded by the poison flag.
 * Scheduling: the omega_bw_em.h control words; every instruction waits on everything pending. */
#include "omega_osc_gpu.h"
#include "omega_bw_em.h"
#include "sha256.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#define NBW ((OSC_MAX_BLOCKS + 1 + 63) / 64)   /* block bitset words (blocks + the virtual exit) */
#define NVW ((OSC_MAX_VREGS + 63) / 64)        /* vreg bitset words */
#define MAX_REGIONS 64
#define MAX_FIX 4096
#define NTMP 6
#define GPR_LIMIT 128u

typedef struct { uint64_t w[NBW]; } Bset;
typedef struct { uint64_t w[NVW]; } Vset;

static bool bs_has(const Bset *s, int i) { return (s->w[i >> 6] >> (i & 63)) & 1u; }
static void bs_set(Bset *s, int i) { s->w[i >> 6] |= 1ull << (i & 63); }
static int bs_count(const Bset *s) { int n = 0; for (int i = 0; i < NBW; i++) n += __builtin_popcountll(s->w[i]); return n; }
static bool bs_eq(const Bset *a, const Bset *b) { return memcmp(a, b, sizeof *a) == 0; }
static bool bs_sub(const Bset *a, const Bset *b) { for (int i = 0; i < NBW; i++) if (a->w[i] & ~b->w[i]) return false; return true; }
static bool bs_meet(const Bset *a, const Bset *b) { for (int i = 0; i < NBW; i++) if (a->w[i] & b->w[i]) return true; return false; }
static bool vs_has(const Vset *s, int i) { return (s->w[i >> 6] >> (i & 63)) & 1u; }
static void vs_set(Vset *s, int i) { s->w[i >> 6] |= 1ull << (i & 63); }
static void vs_clr(Vset *s, int i) { s->w[i >> 6] &= ~(1ull << (i & 63)); }

typedef struct {
    Bset m;          /* members */
    int head, join, bar, parent;
    int bssy_at, bsync_at;
} Region;

enum { FX_EDGE, FX_BSYNC, FX_ARMS };
typedef struct { int at, kind, a, b; } Fix;

typedef struct {
    const OscUnit *u;
    const OscFunc *f;
    char *err; size_t errn; int failed;
    BlackwellIRProgram *p;
    Em e;
    int nb;
    int nsucc[OSC_MAX_BLOCKS], succ[OSC_MAX_BLOCKS][2];
    int ipdom[OSC_MAX_BLOCKS];               /* -1 none, nb = the virtual exit */
    Bset pdom[OSC_MAX_BLOCKS + 1];
    bool varying[OSC_MAX_VREGS];
    bool divergent[OSC_MAX_BLOCKS];
    Vset live_in[OSC_MAX_BLOCKS], live_out[OSC_MAX_BLOCKS];
    Region reg[MAX_REGIONS]; int nreg;
    int map[OSC_MAX_VREGS];                  /* BW vreg of each OSC vreg; -1 for shared arrays */
    uint32_t shoff[OSC_MAX_VREGS];           /* shared arrays: byte offset */
    uint32_t shlen[OSC_MAX_VREGS];           /* shared arrays: elements */
    uint16_t argoff[OSC_MAX_VREGS];          /* parameter registers: byte offset from the argument base */
    int udesc, st, one, zero, poison, a0, t[NTMP];
    int blk_first[OSC_MAX_BLOCKS], blk_last[OSC_MAX_BLOCKS], arms_at[OSC_MAX_BLOCKS], body_at[OSC_MAX_BLOCKS];
    int narms[OSC_MAX_BLOCKS], arms[OSC_MAX_BLOCKS][BW_RECONV_MAX_BAR]; /* region ids, outermost first */
    Fix fix[MAX_FIX]; int nfix;
    OscGpuKernel *k;
} G;

static int fail(G *g, const char *fmt, ...) {
    if (!g->failed) {
        int n = snprintf(g->err, g->errn, "%s: ", g->f ? g->f->name : "kernel");
        va_list ap; va_start(ap, fmt);
        if (n >= 0 && (size_t)n < g->errn) vsnprintf(g->err + n, g->errn - (size_t)n, fmt, ap);
        va_end(ap);
    }
    g->failed = 1;
    return -1;
}

/* ------------------------------------------------------------------ IR facts */

static const OscInsn *term(const OscFunc *f, int b) { return &f->insns[f->blocks[b].first + f->blocks[b].count - 1]; }

/* registers an instruction reads (GLOAD/GSTORE also read the buffer's length register a + 1) */
static int insn_uses(const OscInsn *in, int out[4]) {
    int n = 0;
    switch (in->op) {
    case OSC_I_MOV: case OSC_I_UN: case OSC_I_CAST: case OSC_I_CBR: out[n++] = in->a; break;
    case OSC_I_BIN: case OSC_I_CMP: case OSC_I_LOAD: case OSC_I_SHFL: out[n++] = in->a; out[n++] = in->b; break;
    case OSC_I_STORE: case OSC_I_MAD: out[n++] = in->a; out[n++] = in->b; out[n++] = in->c; break;
    case OSC_I_GLOAD: out[n++] = in->a; out[n++] = in->a + 1; out[n++] = in->b; break;
    case OSC_I_GSTORE: out[n++] = in->a; out[n++] = in->a + 1; out[n++] = in->b; out[n++] = in->c; break;
    case OSC_I_RET: if (in->a >= 0) out[n++] = in->a; break;
    default: break;
    }
    return n;
}
static int insn_def(const OscInsn *in) {
    switch (in->op) {
    case OSC_I_CONST: case OSC_I_MOV: case OSC_I_BIN: case OSC_I_UN: case OSC_I_CMP: case OSC_I_CAST:
    case OSC_I_LOAD: case OSC_I_GPU: case OSC_I_SHARED: case OSC_I_GLOAD: case OSC_I_SHFL: case OSC_I_MAD:
        return in->dst;
    default: return -1;
    }
}

/* the value of a register with exactly one assignment, a CONST (division by a number, SHFL distance) */
static int single_const(const OscFunc *f, int v, uint32_t *out) {
    int defs = 0; uint32_t val = 0;
    for (uint32_t i = 0; i < f->ninsns; i++) {
        if (insn_def(&f->insns[i]) != v) continue;
        defs++;
        if (f->insns[i].op != OSC_I_CONST) return -1;
        val = (uint32_t)f->insns[i].imm;
    }
    if (defs != 1 || v < f->nparams) return -1;
    *out = val;
    return 0;
}

static int cfg(G *g) {
    const OscFunc *f = g->f;
    g->nb = f->nblocks;
    for (int b = 0; b < g->nb; b++) {
        const OscInsn *t = term(f, b);
        g->nsucc[b] = 0;
        if (t->op == OSC_I_BR) g->succ[b][g->nsucc[b]++] = t->blk_t;
        else if (t->op == OSC_I_CBR) { g->succ[b][g->nsucc[b]++] = t->blk_t; g->succ[b][g->nsucc[b]++] = t->blk_f; }
        else if (t->op != OSC_I_RET) return fail(g, "block %d does not end in br, cbr or ret", b);
    }
    /* post-dominators over the blocks plus the virtual exit X = nb (every RET goes to X) */
    int X = g->nb;
    Bset all; memset(&all, 0, sizeof all);
    for (int b = 0; b <= X; b++) bs_set(&all, b);
    for (int b = 0; b < X; b++) g->pdom[b] = all;
    memset(&g->pdom[X], 0, sizeof g->pdom[X]); bs_set(&g->pdom[X], X);
    for (bool ch = true; ch;) {
        ch = false;
        for (int b = X - 1; b >= 0; b--) {
            Bset n;
            if (g->nsucc[b] == 0) n = g->pdom[X];
            else { n = g->pdom[g->succ[b][0]]; for (int s = 1; s < g->nsucc[b]; s++) for (int w = 0; w < NBW; w++) n.w[w] &= g->pdom[g->succ[b][s]].w[w]; }
            bs_set(&n, b);
            if (!bs_eq(&n, &g->pdom[b])) { g->pdom[b] = n; ch = true; }
        }
    }
    for (int b = 0; b < X; b++) {
        g->ipdom[b] = -1;
        int want = bs_count(&g->pdom[b]) - 1;
        for (int d = 0; d <= X; d++)
            if (d != b && bs_has(&g->pdom[b], d) && bs_count(&g->pdom[d]) == want) { g->ipdom[b] = d; break; }
    }
    return 0;
}

/* blocks reachable from the successors of b without passing j (j = nb: none excluded) */
static Bset reach_from(const G *g, int b, int j) {
    Bset m; memset(&m, 0, sizeof m);
    int stack[OSC_MAX_BLOCKS + 1], sp = 0;
    for (int s = 0; s < g->nsucc[b]; s++) if (g->succ[b][s] != j && !bs_has(&m, g->succ[b][s])) { bs_set(&m, g->succ[b][s]); stack[sp++] = g->succ[b][s]; }
    while (sp) {
        int x = stack[--sp];
        for (int s = 0; s < g->nsucc[x]; s++) {
            int y = g->succ[x][s];
            if (y != j && !bs_has(&m, y)) { bs_set(&m, y); stack[sp++] = y; }
        }
    }
    return m;
}

/* varying registers and divergent blocks, together, to a fixed point (GPU.md section 6) */
static void uniformity(G *g) {
    const OscFunc *f = g->f;
    memset(g->varying, 0, sizeof g->varying);
    memset(g->divergent, 0, sizeof g->divergent);
    for (bool ch = true; ch;) {
        ch = false;
        for (int b = 0; b < g->nb; b++)
            for (uint32_t i = f->blocks[b].first; i < f->blocks[b].first + f->blocks[b].count; i++) {
                const OscInsn *in = &f->insns[i];
                int d = insn_def(in);
                if (d < 0 || g->varying[d]) continue;
                bool v = g->divergent[b] || in->op == OSC_I_GLOAD || in->op == OSC_I_LOAD || in->op == OSC_I_SHFL ||
                         (in->op == OSC_I_GPU && in->sub == OSC_G_THREAD);
                int us[4], n = insn_uses(in, us);
                for (int k = 0; k < n && !v; k++) v = g->varying[us[k]];
                if (v) { g->varying[d] = true; ch = true; }
            }
        for (int b = 0; b < g->nb; b++) {
            const OscInsn *t = term(f, b);
            if (t->op != OSC_I_CBR || !g->varying[t->a]) continue;
            Bset m = reach_from(g, b, g->ipdom[b] < 0 ? g->nb : g->ipdom[b]);
            for (int x = 0; x < g->nb; x++) if (bs_has(&m, x) && !g->divergent[x]) { g->divergent[x] = true; ch = true; }
        }
    }
}

static int regions(G *g) {
    const OscFunc *f = g->f;
    g->nreg = 0;
    Bset noregion_reach; memset(&noregion_reach, 0, sizeof noregion_reach);
    for (int b = 0; b < g->nb; b++) {
        const OscInsn *t = term(f, b);
        if (t->op != OSC_I_CBR || !g->varying[t->a]) continue;
        int j = g->ipdom[b];
        if (j < 0) return fail(g, "block %d: a branch with no post-dominator", b);
        Bset m = reach_from(g, b, j);
        if (j == g->nb) { for (int w = 0; w < NBW; w++) noregion_reach.w[w] |= m.w[w]; continue; }
        bs_set(&m, b);
        int dup = -1;
        for (int r = 0; r < g->nreg; r++) if (bs_eq(&g->reg[r].m, &m)) dup = r;
        if (dup >= 0) {
            if (g->reg[dup].join != j) return fail(g, "block %d: two joins for one region", b);
            continue;
        }
        if (g->nreg >= MAX_REGIONS) return fail(g, "more than %d reconvergence regions", MAX_REGIONS);
        /* the head: the one block every edge from outside the members enters */
        int head = -1;
        for (int u = 0; u < g->nb; u++) {
            if (bs_has(&m, u)) continue;
            for (int s = 0; s < g->nsucc[u]; s++) {
                int v = g->succ[u][s];
                if (!bs_has(&m, v)) continue;
                if (head >= 0 && head != v) return fail(g, "block %d: a varying region entered at two blocks (%d, %d)", b, head, v);
                head = v;
            }
        }
        if (bs_has(&m, 0)) { /* the prologue enters block 0 */
            if (head >= 0 && head != 0) return fail(g, "block %d: a varying region entered at two blocks (%d, 0)", b, head);
            head = 0;
        }
        if (head < 0) return fail(g, "block %d: a varying region nothing enters", b);
        Region *r = &g->reg[g->nreg++];
        memset(r, 0, sizeof *r);
        r->m = m; r->head = head; r->join = j; r->parent = -1; r->bssy_at = r->bsync_at = -1;
    }
    /* a branch whose paths meet only at the exit must have no region after it */
    for (int r = 0; r < g->nreg; r++)
        if (bs_has(&noregion_reach, g->reg[r].head))
            return fail(g, "a varying region (head block %d) can run after a varying ret; v1 refuses it", g->reg[r].head);
    /* nesting: members nest or are disjoint */
    for (int a = 0; a < g->nreg; a++) {
        int best = -1;
        for (int b = 0; b < g->nreg; b++) {
            if (a == b) continue;
            const Bset *ma = &g->reg[a].m, *mb = &g->reg[b].m;
            bool ab = bs_sub(ma, mb), ba = bs_sub(mb, ma);
            if (!ab && !ba && bs_meet(ma, mb)) return fail(g, "two varying regions overlap without nesting");
            if (ab && !ba && (best < 0 || bs_count(mb) < bs_count(&g->reg[best].m))) best = b;
        }
        g->reg[a].parent = best;
    }
    /* barrier registers: greedy, conflicting regions differ */
    uint32_t used_all = 0;
    for (int a = 0; a < g->nreg; a++) {
        uint32_t busy = 0;
        for (int b = 0; b < a; b++) {
            bool conflict = false;
            const Bset *ma = &g->reg[a].m, *mb = &g->reg[b].m;
            if (bs_sub(ma, mb) || bs_sub(mb, ma)) conflict = true;
            else for (int c = 0; c < g->nreg && !conflict; c++)
                if (c != a && c != b && bs_sub(ma, &g->reg[c].m) && bs_sub(mb, &g->reg[c].m)) conflict = true;
            if (conflict) busy |= 1u << g->reg[b].bar;
        }
        int bar = 0;
        while (bar < BW_RECONV_MAX_BAR && (busy >> bar & 1u)) bar++;
        if (bar >= BW_RECONV_MAX_BAR) return fail(g, "more than %d reconvergence barriers needed at once", BW_RECONV_MAX_BAR);
        g->reg[a].bar = bar;
        used_all |= 1u << bar;
    }
    g->k->regions = (uint32_t)g->nreg;
    g->k->region_bars = (uint32_t)__builtin_popcount(used_all);
    /* arms per head, outermost first */
    memset(g->narms, 0, sizeof g->narms);
    for (int r = 0; r < g->nreg; r++) {
        int h = g->reg[r].head;
        if (g->narms[h] >= BW_RECONV_MAX_BAR) return fail(g, "too many regions start at block %d", h);
        int at = g->narms[h]++;
        while (at > 0 && bs_count(&g->reg[g->arms[h][at - 1]].m) < bs_count(&g->reg[r].m)) { g->arms[h][at] = g->arms[h][at - 1]; at--; }
        g->arms[h][at] = r;
    }
    return 0;
}

static void liveness(G *g) {
    const OscFunc *f = g->f;
    Vset use[OSC_MAX_BLOCKS], def[OSC_MAX_BLOCKS];
    for (int b = 0; b < g->nb; b++) {
        memset(&use[b], 0, sizeof use[b]); memset(&def[b], 0, sizeof def[b]);
        for (uint32_t i = f->blocks[b].first; i < f->blocks[b].first + f->blocks[b].count; i++) {
            int us[4], n = insn_uses(&f->insns[i], us);
            for (int k = 0; k < n; k++) if (!vs_has(&def[b], us[k])) vs_set(&use[b], us[k]);
            int d = insn_def(&f->insns[i]);
            if (d >= 0) vs_set(&def[b], d);
        }
        memset(&g->live_in[b], 0, sizeof g->live_in[b]); memset(&g->live_out[b], 0, sizeof g->live_out[b]);
    }
    for (bool ch = true; ch;) {
        ch = false;
        for (int b = g->nb - 1; b >= 0; b--) {
            Vset out; memset(&out, 0, sizeof out);
            for (int s = 0; s < g->nsucc[b]; s++) for (int w = 0; w < NVW; w++) out.w[w] |= g->live_in[g->succ[b][s]].w[w];
            Vset in;
            for (int w = 0; w < NVW; w++) in.w[w] = use[b].w[w] | (out.w[w] & ~def[b].w[w]);
            if (memcmp(&in, &g->live_in[b], sizeof in) || memcmp(&out, &g->live_out[b], sizeof out)) {
                g->live_in[b] = in; g->live_out[b] = out; ch = true;
            }
        }
    }
}

/* ------------------------------------------------------------------ emission helpers */

/* pred: 0 always, 1 @P0, -1 @!P0 */
static void E(G *g, BlackwellIRInsn in, int kind, int pred) {
    if (pred) { in.predicate_p0 = true; in.predicate_not = pred < 0; }
    emit(&g->e, in, kind);
}
static void Mov(G *g, int d, int a, int pred) { BlackwellIRInsn i = I0(BW_IR_LOP3_LUT); i.dst_vreg = d; i.src1_vreg = a; i.imm = 0xf0; E(g, i, K_FIXED, pred); }
static void Movi(G *g, int d, uint32_t imm, int pred) { BlackwellIRInsn i = I0(BW_IR_MOV_IMM); i.dst_vreg = d; i.imm = imm; E(g, i, K_FIXED, pred); }
static void Lop(G *g, int d, int a, int b, uint32_t lut) { BlackwellIRInsn i = I0(BW_IR_LOP3_LUT); i.dst_vreg = d; i.src1_vreg = a; i.src2_vreg = b; i.imm = lut; E(g, i, K_FIXED, 0); }
static void Imad(G *g, int d, int a, int b, int c) { BlackwellIRInsn i = I0(BW_IR_IMAD); i.dst_vreg = d; i.src1_vreg = a; i.src2_vreg = b; i.src3_vreg = c; E(g, i, K_FIXED, 0); }
static void Imadi(G *g, int d, int a, uint32_t imm, int c) { BlackwellIRInsn i = I0(BW_IR_IMAD); i.dst_vreg = d; i.src1_vreg = a; i.imm = imm; i.src3_vreg = c; E(g, i, K_FIXED, 0); }
static void Imadhi(G *g, int d, int a, int b) { BlackwellIRInsn i = I0(BW_IR_IMAD_HI_U32); i.dst_vreg = d; i.src1_vreg = a; i.src2_vreg = b; E(g, i, K_FIXED, 0); }
static void Iadd(G *g, int d, int a, int b) { BlackwellIRInsn i = I0(BW_IR_IADD3); i.dst_vreg = d; i.src1_vreg = a; i.src2_vreg = b; E(g, i, K_FIXED, 0); }
static void Shr(G *g, int d, int a, uint32_t n) { if (n == 0) { Mov(g, d, a, 0); return; } BlackwellIRInsn i = I0(BW_IR_SHF_R); i.dst_vreg = d; i.src1_vreg = a; i.imm = n; E(g, i, K_FIXED, 0); }
static void Shl(G *g, int d, int a, int b, int pred) { BlackwellIRInsn i = I0(BW_IR_SHF_L_U32); i.dst_vreg = d; i.src1_vreg = a; i.src2_vreg = b; E(g, i, K_FIXED, pred); }
static void Ge(G *g, int a, int b) { BlackwellIRInsn i = I0(BW_IR_ISETP_GE_U32); i.src1_vreg = a; i.src2_vreg = b; E(g, i, K_PRED, 0); } /* P0 = a >= b (u32) */
static void Fsetp(G *g, int a, int b, unsigned code) { BlackwellIRInsn i = I0(BW_IR_FSETP); i.src1_vreg = a; i.src2_vreg = b; i.imm = 0x03f00000u | (code << 12); E(g, i, K_PRED, 0); }
static void F2(G *g, BlackwellIROpcode op, int d, int a, int b) { BlackwellIRInsn i = I0(op); i.dst_vreg = d; i.src1_vreg = a; i.src2_vreg = b; E(g, i, K_FIXED, 0); }
static void Var1(G *g, BlackwellIROpcode op, int d, int a) { BlackwellIRInsn i = I0(op); i.dst_vreg = d; i.src1_vreg = a; E(g, i, K_VAR, 0); }
/* bool d = P0 (pred 1) or !P0 (pred -1) */
static void Bool(G *g, int d, int pred) { Movi(g, d, 0, 0); Movi(g, d, 1, pred); }
/* under pred: status word `code` = 1, poison = 1, and (when d >= 0) d = 0 */
static void Trap(G *g, int code, int pred, int d) {
    BlackwellIRInsn s = I0(BW_IR_STG_E); s.src1_vreg = g->st; s.src2_vreg = g->one; s.imm = 4u * (uint32_t)code; s.ureg = g->udesc;
    E(g, s, K_STORE, pred);
    Movi(g, g->poison, 1, pred);
    if (d >= 0) Movi(g, d, 0, pred);
}
static int branch(G *g, int pred) { int at = here(&g->e); BlackwellIRInsn i = I0(BW_IR_BRA); i.imm = 1; E(g, i, K_BRA, pred); return at; }
static void addfix(G *g, int at, int kind, int a, int b) {
    if (g->nfix >= MAX_FIX) { fail(g, "too many branches"); return; }
    g->fix[g->nfix++] = (Fix){ at, kind, a, b };
}

/* ------------------------------------------------------------------ division by a number */

static unsigned ceil_log2(uint32_t d) { unsigned l = 0; while (l < 32 && (1ull << l) < d) l++; return l; }

/* Granlund and Montgomery, "Division by invariant integers using multiplication" (1994), figure 4.1,
 * N = 32: for 1 < d < 2^32 not a power of two, l = ceil(log2 d), m = floor(2^32 (2^l - d) / d) + 1,
 * t = mulhi(m, n), q = (t + ((n - t) >> 1)) >> (l - 1); exact for every 32-bit n. */
static uint32_t gm_magic(uint32_t d, unsigned *l) {
    *l = ceil_log2(d);
    return (uint32_t)((((uint64_t)1 << 32) * (((uint64_t)1 << *l) - d)) / d + 1);
}
uint32_t osc_gpu_udiv_model(uint32_t n, uint32_t d) {
    if (d == 0) return 0;
    if ((d & (d - 1)) == 0) return n >> ceil_log2(d);
    unsigned l; uint32_t m = gm_magic(d, &l);
    uint32_t t = (uint32_t)(((uint64_t)m * n) >> 32);
    return (t + ((n - t) >> 1)) >> (l - 1);
}
/* q = n / d into q (q may equal n's register only through the temporaries; T4, T5 are used) */
static void udiv_const(G *g, int q, int n, uint32_t d) {
    if ((d & (d - 1)) == 0) { Shr(g, q, n, ceil_log2(d)); return; }
    unsigned l; uint32_t m = gm_magic(d, &l);
    int tm = g->t[4], t = g->t[5];
    Movi(g, tm, m, 0);
    Imadhi(g, t, n, tm);               /* t = mulhi(m, n) */
    Imadi(g, tm, t, 0xffffffffu, n);   /* n - t */
    Shr(g, tm, tm, 1);
    Iadd(g, tm, tm, t);
    Shr(g, q, tm, l - 1);
}

/* ------------------------------------------------------------------ one instruction */

static int R(G *g, int v) { return g->map[v]; }
static OscScalar ty(const G *g, int v) { return g->f->vtype[v].s; }

static int lower(G *g, const OscInsn *in) {
    const OscFunc *f = g->f;
    int *T = g->t;
    switch (in->op) {
    case OSC_I_CONST:
        Movi(g, R(g, in->dst), (uint32_t)in->imm, 0);
        return 0;
    case OSC_I_MOV:
        if (ty(g, in->dst) != OSC_T_U32 && ty(g, in->dst) != OSC_T_F32 && ty(g, in->dst) != OSC_T_BOOL) return fail(g, "mov of a %s", osc_scalar_name(ty(g, in->dst)));
        Mov(g, R(g, in->dst), R(g, in->a), 0);
        return 0;
    case OSC_I_GPU: {
        int d = R(g, in->dst);
        switch (in->sub) {
        case OSC_G_THREAD: s2r(&g->e, d, BW_SR_TID_X); return 0;
        case OSC_G_BLOCK: s2r(&g->e, d, BW_SR_CTAID_X); g->k->reads_block = true; return 0;
        case OSC_G_BATCH: s2r(&g->e, d, BW_SR_CTAID_Y); g->k->reads_batch = true; return 0;
        case OSC_G_BLOCKS: ldc32(&g->e, d, OSC_GPU_ARG_BASE + g->k->blocks_off); return 0;
        case OSC_G_BATCHES: ldc32(&g->e, d, OSC_GPU_ARG_BASE + g->k->batches_off); return 0;
        default: return fail(g, "unknown launch value %d", in->sub);
        }
    }
    case OSC_I_SHARED: return 0; /* zeroed in the prologue */
    case OSC_I_SYNC: bar_sync(&g->e); return 0;
    case OSC_I_TRAP:
        if (in->imm == 0 || in->imm >= OSC_GPU_STATUS_WORDS) return fail(g, "trap code %llu outside 1..15", (unsigned long long)in->imm);
        Trap(g, (int)in->imm, 0, -1);
        return 0;
    case OSC_I_SHFL: {
        uint32_t dist;
        if (in->sub != OSC_SH_DOWN || single_const(f, in->b, &dist) != 0 || dist < 1 || dist > 31) return fail(g, "down needs a number 1 to 31");
        shfl_down(&g->e, R(g, in->dst), R(g, in->a), dist);
        return 0;
    }
    case OSC_I_MAD: {
        BlackwellIRInsn i = I0(BW_IR_FFMA); i.dst_vreg = R(g, in->dst); i.src1_vreg = R(g, in->a); i.src2_vreg = R(g, in->b); i.src3_vreg = R(g, in->c);
        E(g, i, K_FIXED, 0);
        return 0;
    }
    case OSC_I_GLOAD: case OSC_I_GSTORE: {
        int ptr = R(g, in->a), len = R(g, in->a + 1), idx = R(g, in->b);
        Ge(g, idx, len);                       /* P0: out of bounds */
        Trap(g, OSC_TRAP_BOUNDS, 1, -1);
        imadw(&g->e, g->a0, idx, 4, ptr);      /* address = ptr + 4 * idx */
        if (in->op == OSC_I_GLOAD) {
            int d = R(g, in->dst);
            BlackwellIRInsn l = I0(BW_IR_LDG_E); l.dst_vreg = d; l.src1_vreg = g->a0; l.ureg = g->udesc;
            E(g, l, K_VAR, -1);                /* @!P0 LDG */
            Movi(g, d, 0, 1);                  /* @P0 d = 0 */
        } else {
            Ge(g, g->poison, g->one);          /* P0: this thread has trapped */
            BlackwellIRInsn s = I0(BW_IR_STG_E); s.src1_vreg = g->a0; s.src2_vreg = R(g, in->c); s.ureg = g->udesc;
            E(g, s, K_STORE, -1);
        }
        return 0;
    }
    case OSC_I_LOAD: case OSC_I_STORE: {
        if (f->vtype[in->a].ref != OSC_REF_BLOCK) return fail(g, "load/store on a non-shared array");
        int idx = R(g, in->b);
        Movi(g, T[0], g->shlen[in->a], 0);
        Ge(g, idx, T[0]);
        Trap(g, OSC_TRAP_BOUNDS, 1, -1);
        Movi(g, T[1], g->shoff[in->a], 0);
        Imadi(g, T[1], idx, 4, T[1]);          /* shared byte address */
        if (in->op == OSC_I_LOAD) {
            int d = R(g, in->dst);
            BlackwellIRInsn l = I0(BW_IR_LDS32); l.dst_vreg = d; l.src1_vreg = T[1];
            E(g, l, K_VAR, -1);
            Movi(g, d, 0, 1);
        } else {
            Ge(g, g->poison, g->one);
            BlackwellIRInsn s = I0(BW_IR_STS32); s.src1_vreg = T[1]; s.src2_vreg = R(g, in->c);
            E(g, s, K_STORE, -1);
        }
        return 0;
    }
    case OSC_I_CAST: {
        OscScalar from = ty(g, in->a), to = ty(g, in->dst);
        int d = R(g, in->dst), a = R(g, in->a);
        if (from == OSC_T_U32 && to == OSC_T_F32) { BlackwellIRInsn i = I0(BW_IR_I2FP_U32); i.dst_vreg = d; i.src1_vreg = a; E(g, i, K_FIXED, 0); return 0; }
        if (from == OSC_T_F32 && to == OSC_T_U32) {
            /* ok when -1 < a < 2^32 (both compares are false for NaN) */
            Movi(g, T[0], 0xbf800000u, 0);     /* -1.0 */
            Fsetp(g, a, T[0], 0x4);            /* GT */
            Bool(g, T[1], 1);
            Movi(g, T[0], 0x4f800000u, 0);     /* 2^32 */
            Fsetp(g, a, T[0], 0x1);            /* LT */
            Bool(g, T[2], 1);
            Lop(g, T[1], T[1], T[2], 0xc0);    /* ok */
            Var1(g, BW_IR_F2I_U32, T[3], a);
            Mov(g, d, T[3], 0);
            Ge(g, T[1], g->one);               /* P0: ok */
            Trap(g, OSC_TRAP_CAST, -1, d);
            return 0;
        }
        return fail(g, "cast %s to %s", osc_scalar_name(from), osc_scalar_name(to));
    }
    case OSC_I_UN: {
        OscScalar t = ty(g, in->a);
        int d = R(g, in->dst), a = R(g, in->a);
        switch (in->sub) {
        case OSC_U_NEG:
            if (t == OSC_T_U32) { Imadi(g, d, a, 0xffffffffu, -1); return 0; }
            if (t == OSC_T_F32) { Movi(g, T[0], 0x80000000u, 0); Lop(g, d, a, T[0], 0x3c); return 0; }
            break;
        case OSC_U_BNOT: if (t == OSC_T_U32) { Lop(g, d, a, -1, 0x0f); return 0; } break;
        case OSC_U_LNOT: if (t == OSC_T_BOOL) { Lop(g, d, a, g->one, 0x3c); return 0; } break;
        case OSC_U_EXP:
            if (t != OSC_T_F32) break;
            Movi(g, T[0], 0x3fb8aa3bu, 0);
            F2(g, BW_IR_FMUL, T[0], a, T[0]);
            Var1(g, BW_IR_MUFU_EX2, d, T[0]);
            return 0;
        case OSC_U_INV: if (t == OSC_T_F32) { Var1(g, BW_IR_MUFU_RCP, d, a); return 0; } break;
        case OSC_U_RSQ: if (t == OSC_T_F32) { Var1(g, BW_IR_MUFU_RSQ, d, a); return 0; } break;
        case OSC_U_SQRT:
            if (t != OSC_T_F32) break;
            Var1(g, BW_IR_MUFU_RSQ, T[0], a);
            Var1(g, BW_IR_MUFU_RCP, d, T[0]);
            return 0;
        default: break;
        }
        return fail(g, "unary op %d on %s", in->sub, osc_scalar_name(t));
    }
    case OSC_I_CMP: {
        OscScalar t = ty(g, in->a);
        int d = R(g, in->dst), a = R(g, in->a), b = R(g, in->b);
        if (t == OSC_T_F32) {
            static const unsigned code[] = { [OSC_C_EQ] = 0x2, [OSC_C_NE] = 0xd, [OSC_C_LT] = 0x1, [OSC_C_LE] = 0x3, [OSC_C_GT] = 0x4, [OSC_C_GE] = 0x6 };
            if (in->sub < OSC_C_EQ || in->sub > OSC_C_GE) return fail(g, "compare %d", in->sub);
            Fsetp(g, a, b, code[in->sub]);
            Bool(g, d, 1);
            return 0;
        }
        if (t != OSC_T_U32 && t != OSC_T_BOOL) return fail(g, "compare on %s", osc_scalar_name(t));
        switch (in->sub) {
        case OSC_C_LT: Ge(g, a, b); Bool(g, d, -1); return 0;
        case OSC_C_GE: Ge(g, a, b); Bool(g, d, 1); return 0;
        case OSC_C_GT: Ge(g, b, a); Bool(g, d, -1); return 0;
        case OSC_C_LE: Ge(g, b, a); Bool(g, d, 1); return 0;
        case OSC_C_EQ: case OSC_C_NE:
            Lop(g, T[0], a, b, 0x3c);
            Ge(g, T[0], g->one);
            Bool(g, d, in->sub == OSC_C_NE ? 1 : -1);
            return 0;
        default: return fail(g, "compare %d", in->sub);
        }
    }
    case OSC_I_BIN: {
        OscScalar t = ty(g, in->a);
        int d = R(g, in->dst), a = R(g, in->a), b = R(g, in->b);
        if (t == OSC_T_F32) {
            switch (in->sub) {
            case OSC_B_ADD: F2(g, BW_IR_FADD, d, a, b); return 0;
            case OSC_B_SUB: F2(g, BW_IR_FSUB, d, a, b); return 0;
            case OSC_B_MUL: F2(g, BW_IR_FMUL, d, a, b); return 0;
            case OSC_B_MIN: F2(g, BW_IR_FMNMX_MIN, d, a, b); return 0;
            case OSC_B_MAX: F2(g, BW_IR_FMNMX_MAX, d, a, b); return 0;
            default: return fail(g, "float op %d", in->sub);
            }
        }
        if (t == OSC_T_BOOL) {
            uint32_t lut = in->sub == OSC_B_AND ? 0xc0 : in->sub == OSC_B_OR ? 0xfc : in->sub == OSC_B_XOR ? 0x3c : 0;
            if (!lut) return fail(g, "bool op %d", in->sub);
            Lop(g, d, a, b, lut);
            return 0;
        }
        if (t != OSC_T_U32) return fail(g, "binary op on %s", osc_scalar_name(t));
        switch (in->sub) {
        case OSC_B_AND: Lop(g, d, a, b, 0xc0); return 0;
        case OSC_B_OR: Lop(g, d, a, b, 0xfc); return 0;
        case OSC_B_XOR: Lop(g, d, a, b, 0x3c); return 0;
        case OSC_B_ADD:                         /* overflow when the sum is below a */
            Iadd(g, T[0], a, b); Ge(g, T[0], a); Mov(g, d, T[0], 0); Trap(g, OSC_TRAP_OVERFLOW, -1, d); return 0;
        case OSC_B_SUB:                         /* overflow when a < b */
            Imadi(g, T[0], b, 0xffffffffu, a); Ge(g, a, b); Mov(g, d, T[0], 0); Trap(g, OSC_TRAP_OVERFLOW, -1, d); return 0;
        case OSC_B_MUL:                         /* overflow when the high word is not 0 */
            Imadhi(g, T[1], a, b); Imad(g, T[0], a, b, -1); Ge(g, T[1], g->one); Mov(g, d, T[0], 0); Trap(g, OSC_TRAP_OVERFLOW, 1, d); return 0;
        case OSC_B_MIN: Ge(g, a, b); Mov(g, T[0], b, 0); Mov(g, T[0], a, -1); Mov(g, d, T[0], 0); return 0;
        case OSC_B_MAX: Ge(g, a, b); Mov(g, T[0], a, 0); Mov(g, T[0], b, -1); Mov(g, d, T[0], 0); return 0;
        case OSC_B_DIV: case OSC_B_REM: {
            uint32_t dv;
            if (single_const(f, in->b, &dv) != 0 || dv == 0) return fail(g, "div/rem need a number other than 0 as the divisor");
            udiv_const(g, T[0], a, dv);
            if (in->sub == OSC_B_DIV) Mov(g, d, T[0], 0);
            else Imadi(g, d, T[0], (uint32_t)(0u - dv), a);  /* a - q * dv */
            return 0;
        }
        case OSC_B_SHL: case OSC_B_SHR:
            Movi(g, T[1], 32, 0);
            Ge(g, b, T[1]);                     /* P0: amount >= 32, trap 7 */
            if (in->sub == OSC_B_SHL) {
                Shl(g, T[0], a, b, -1);
            } else {
                /* a >> n = mulhi(a, 2^(32 - n)) for 1 <= n <= 31; a for n = 0 */
                Imadi(g, T[2], b, 0xffffffffu, T[1]);   /* 32 - n */
                andi(&g->e, T[2], T[2], 0, 31);           /* an amount over 31 traps anyway; keep the shift in range */
                Ge(g, b, g->one);                         /* P0: n >= 1 */
                Mov(g, T[0], a, -1);
                Shl(g, T[3], g->one, T[2], 1);            /* n >= 1: 2^(32 - n) */
                BlackwellIRInsn h = I0(BW_IR_IMAD_HI_U32); h.dst_vreg = T[0]; h.src1_vreg = a; h.src2_vreg = T[3];
                E(g, h, K_FIXED, 1);
                Ge(g, b, T[1]);
            }
            Mov(g, d, T[0], 0);
            Trap(g, OSC_TRAP_SHIFT, 1, d);
            return 0;
        default: return fail(g, "uint op %d", in->sub);
        }
    }
    default:
        return fail(g, "instruction %d is not allowed in a kernel", in->op);
    }
}

/* ------------------------------------------------------------------ layout */

static bool in_region(const G *g, int r, int u) { return u >= 0 && bs_has(&g->reg[r].m, u); }

/* the instruction an edge u -> v lands on (u = -1: the prologue) */
static int edge_target(G *g, int u, int v) {
    int inner = -1;
    for (int r = 0; r < g->nreg; r++)
        if (g->reg[r].join == v && in_region(g, r, u) && (inner < 0 || bs_count(&g->reg[r].m) < bs_count(&g->reg[inner].m))) inner = r;
    int arm = g->narms[v];
    for (int i = 0; i < g->narms[v]; i++) if (!in_region(g, g->arms[v][i], u)) { arm = i; break; }
    if (inner >= 0) {
        if (arm != 0 && g->narms[v] > 0) { fail(g, "edge %d -> %d leaves one region and is inside the next", u, v); return 0; }
        return g->reg[inner].bsync_at;
    }
    return arm < g->narms[v] ? g->arms_at[v] + arm : g->body_at[v];
}

static int emit_block(G *g, int b) {
    const OscFunc *f = g->f;
    g->blk_first[b] = here(&g->e);
    /* joins: regions with join b, innermost first; each continues into its parent's join or the arms */
    int js[MAX_REGIONS], nj = 0;
    for (int r = 0; r < g->nreg; r++) if (g->reg[r].join == b) js[nj++] = r;
    for (int i = 1; i < nj; i++) for (int k = i; k > 0 && bs_count(&g->reg[js[k]].m) < bs_count(&g->reg[js[k - 1]].m); k--) { int t = js[k]; js[k] = js[k - 1]; js[k - 1] = t; }
    for (int i = 0; i < nj; i++) {
        Region *r = &g->reg[js[i]];
        r->bsync_at = here(&g->e);
        BlackwellIRInsn s = I0(BW_IR_BSYNC); s.bar_reg = (uint8_t)r->bar; E(g, s, K_BSYNC, 0);
        int par = r->parent >= 0 && g->reg[r->parent].join == b ? r->parent : -1;
        bool next_is_par = i + 1 < nj && js[i + 1] == par;
        if (par >= 0 && !next_is_par) addfix(g, branch(g, 0), FX_BSYNC, par, 0);
        else if (par < 0 && i + 1 < nj) addfix(g, branch(g, 0), FX_ARMS, b, 0);
    }
    /* arms: regions with head b, outermost first */
    g->arms_at[b] = here(&g->e);
    for (int i = 0; i < g->narms[b]; i++) {
        Region *r = &g->reg[g->arms[b][i]];
        r->bssy_at = here(&g->e);
        BlackwellIRInsn s = I0(BW_IR_BSSY); s.bar_reg = (uint8_t)r->bar; s.imm = 2; E(g, s, K_BSSY, 0);
    }
    g->body_at[b] = here(&g->e);
    for (uint32_t i = f->blocks[b].first; i + 1 < f->blocks[b].first + f->blocks[b].count; i++)
        if (lower(g, &f->insns[i]) != 0) return -1;
    const OscInsn *t = term(f, b);
    if (t->op == OSC_I_BR) addfix(g, branch(g, 0), FX_EDGE, b, t->blk_t);
    else if (t->op == OSC_I_CBR) {
        Ge(g, R(g, t->a), g->one);
        addfix(g, branch(g, 1), FX_EDGE, b, t->blk_t);
        addfix(g, branch(g, 0), FX_EDGE, b, t->blk_f);
    } else {
        emit(&g->e, I0(BW_IR_EXIT), K_EXIT);
    }
    g->blk_last[b] = here(&g->e) - 1;
    return g->failed ? -1 : 0;
}

/* ------------------------------------------------------------------ the kernel */

static int params_and_layout(G *g) {
    const OscFunc *f = g->f;
    OscGpuKernel *k = g->k;
    uint32_t off = 0;
    k->nparams = 0;
    for (int v = 0; v < f->nparams; v++) {
        const OscType *t = &f->vtype[v];
        OscGpuParam *p = &k->params[k->nparams];
        if (t->s == OSC_T_GVIEW || t->s == OSC_T_GEDIT) {
            if (v + 1 >= f->nparams || f->vtype[v + 1].s != OSC_T_U32) return fail(g, "buffer parameter %d without its length", v);
            off = (off + 7u) & ~7u;
            p->kind = t->s == OSC_T_GVIEW ? OSC_GPU_P_VIEW : OSC_GPU_P_EDIT; p->elem = (uint8_t)t->elem; p->off = (uint16_t)off;
            if (t->elem != OSC_T_U32 && t->elem != OSC_T_F32) return fail(g, "buffer of %s", osc_scalar_name(t->elem));
            g->argoff[v] = (uint16_t)off; g->argoff[v + 1] = (uint16_t)(off + 8);
            off += 12; v++;
        } else if (t->s == OSC_T_U32 || t->s == OSC_T_F32 || t->s == OSC_T_BOOL) {
            p->kind = t->s == OSC_T_U32 ? OSC_GPU_P_U32 : t->s == OSC_T_F32 ? OSC_GPU_P_F32 : OSC_GPU_P_BOOL; p->elem = 0; p->off = (uint16_t)off;
            g->argoff[v] = (uint16_t)off;
            off += 4;
        } else return fail(g, "parameter %d of type %s", v, osc_scalar_name(t->s));
        k->nparams++;
    }
    off = (off + 7u) & ~7u;
    k->status_off = (uint16_t)off; k->blocks_off = (uint16_t)(off + 8); k->batches_off = (uint16_t)(off + 12);
    off += 16;
    if (off / 4 > OSC_GPU_MAX_ARG_WORDS) return fail(g, "arguments need %u words; the launcher passes %u", off / 4, OSC_GPU_MAX_ARG_WORDS);
    k->arg_words = (uint16_t)(off / 4);
    /* shared arrays and register map */
    uint32_t sh = 0;
    for (int v = 0; v < f->nvregs; v++) {
        const OscType *t = &f->vtype[v];
        g->map[v] = -1;
        if (t->s == OSC_T_REF) {
            if (t->ref != OSC_REF_BLOCK) return fail(g, "register %d: only shared arrays may be references in a kernel", v);
            g->shoff[v] = sh; g->shlen[v] = t->len; sh += 4u * t->len;
            continue;
        }
        if (t->s == OSC_T_GVIEW || t->s == OSC_T_GEDIT) g->map[v] = V64(&g->e);
        else if (t->s == OSC_T_U32 || t->s == OSC_T_F32 || t->s == OSC_T_BOOL) g->map[v] = V(&g->e);
        else return fail(g, "register %d of type %s", v, osc_scalar_name(t->s));
    }
    if (sh > OSC_MAX_SHARED_BYTES) return fail(g, "shared arrays need %u bytes", sh);
    k->shared_bytes = sh;
    return g->e.err ? fail(g, "more registers than the builder holds") : 0;
}

static void prologue(G *g) {
    const OscFunc *f = g->f;
    g->udesc = UV64(&g->e);
    g->st = V64(&g->e); g->a0 = V64(&g->e);
    g->one = V(&g->e); g->zero = V(&g->e); g->poison = V(&g->e);
    for (int i = 0; i < NTMP; i++) g->t[i] = V(&g->e);
    ldcu64(&g->e, g->udesc, 0x358);
    for (int v = 0; v < f->nparams; v++) {
        OscScalar s = f->vtype[v].s;
        if (s == OSC_T_GVIEW || s == OSC_T_GEDIT) ldc64(&g->e, R(g, v), OSC_GPU_ARG_BASE + g->argoff[v]);
        else ldc32(&g->e, R(g, v), OSC_GPU_ARG_BASE + g->argoff[v]);
    }
    ldc64(&g->e, g->st, OSC_GPU_ARG_BASE + g->k->status_off);
    Movi(g, g->one, 1, 0); Movi(g, g->zero, 0, 0); Movi(g, g->poison, 0, 0);
    if (g->k->shared_bytes) {
        /* every thread zeroes words thread, thread + threads, ...: a loop whose trip count differs per lane, in region B0 */
        int *T = g->t;
        s2r(&g->e, T[0], BW_SR_TID_X);
        Imadi(g, T[0], T[0], 4, -1);
        Movi(g, T[1], g->k->shared_bytes, 0);
        Movi(g, T[2], 4u * f->threads, 0);
        int bssy = here(&g->e);
        BlackwellIRInsn s = I0(BW_IR_BSSY); s.bar_reg = 0; s.imm = 2; E(g, s, K_BSSY, 0);
        loop_begin(&g->e);
        int top = here(&g->e);
        Ge(g, T[0], T[1]);
        int out = branch(g, 1);
        BlackwellIRInsn z = I0(BW_IR_STS32); z.src1_vreg = T[0]; z.src2_vreg = g->zero; E(g, z, K_STORE, 0);
        Iadd(g, T[0], T[0], T[2]);
        bra(&g->e, top);
        loop_end(&g->e);
        int join = here(&g->e);
        BlackwellIRInsn y = I0(BW_IR_BSYNC); y.bar_reg = 0; E(g, y, K_BSYNC, 0);
        patch_bra(&g->e, out, join);
        if (!g->e.err) g->p->insns[bssy].imm = (uint32_t)(join + 1 - bssy);
        bar_sync(&g->e);
    }
    addfix(g, branch(g, 0), FX_EDGE, -1, 0);
}

static int finish(G *g) {
    const OscFunc *f = g->f;
    BlackwellIRProgram *p = g->p;
    for (int i = 0; i < g->nfix && !g->failed; i++) {
        Fix *x = &g->fix[i];
        int to = x->kind == FX_EDGE ? edge_target(g, x->a, x->b) : x->kind == FX_BSYNC ? g->reg[x->a].bsync_at : g->arms_at[x->a];
        if (to < 0) return fail(g, "a branch target was never laid out");
        p->insns[x->at].imm = (uint32_t)(to - x->at);
        if (to == x->at) return fail(g, "a branch to itself");
    }
    for (int r = 0; r < g->nreg && !g->failed; r++) {
        Region *rg = &g->reg[r];
        if (rg->bssy_at < 0 || rg->bsync_at < 0 || rg->bsync_at < rg->bssy_at) return fail(g, "region at block %d: its join is laid out before its head", rg->head);
        p->insns[rg->bssy_at].imm = (uint32_t)(rg->bsync_at + 1 - rg->bssy_at);
    }
    if (g->failed) return -1;
    /* live intervals over the layout: an OSC register live into or out of a block spans the block's ends */
    OmegaRegAlloc *ra = &p->regalloc;
    int last = (int)p->count - 1;
    for (int v = 0; v < f->nvregs; v++) {
        int bv = g->map[v];
        if (bv < 0) continue;
        OmegaLiveInterval *iv = &ra->intervals[bv];
        int lo = iv->first_def, hi = iv->last_use;
        for (int b = 0; b < g->nb; b++) {
            if (vs_has(&g->live_in[b], v)) { if (lo < 0 || g->blk_first[b] < lo) lo = g->blk_first[b]; if (g->blk_first[b] > hi) hi = g->blk_first[b]; }
            if (vs_has(&g->live_out[b], v)) { if (lo < 0 || g->blk_last[b] < lo) lo = g->blk_last[b]; if (g->blk_last[b] > hi) hi = g->blk_last[b]; }
        }
        iv->first_def = lo; iv->last_use = hi;
    }
    /* values made in the prologue and read anywhere: to the end */
    int whole[] = { g->st, g->one, g->zero, g->poison };
    for (unsigned i = 0; i < sizeof whole / sizeof whole[0]; i++) ra->intervals[whole[i]].last_use = last;
    ra->uintervals[g->udesc].last_use = last;
    if (omega_bw_regalloc_solve(p) != 0) return fail(g, "the register allocator has no room (v1 does not spill)");
    uint32_t gpr = (ra->peak_gpr_usage + 7u) & ~7u;
    uint32_t cap = 65536u / f->threads; if (cap > GPR_LIMIT) cap = GPR_LIMIT;
    if (gpr > cap) return fail(g, "needs %u registers per thread; %u threads allow %u", gpr, f->threads, cap);
    OscGpuKernel *k = g->k;
    k->code = malloc((size_t)BW_MAX_IR_INSNS * 16u);
    if (!k->code) return fail(g, "out of memory");
    size_t n = 0;
    if (omega_bw_encode_program(p, k->code, (size_t)BW_MAX_IR_INSNS * 16u, &n) != 0) return fail(g, "the encoder refused an instruction");
    k->code_size = n; k->insn_count = (uint32_t)(n / 16u); k->gpr_count = gpr;
    sha256_hash(k->code, k->code_size, k->code_sha256);
    return 0;
}

int osc_gpu_build(const OscUnit *u, unsigned fn, OscGpuKernel *k, char *err, size_t errn) {
    if (err && errn) err[0] = 0;
    if (!u || !k || fn >= u->nfuncs) { if (err && errn) snprintf(err, errn, "no such function"); return -1; }
    memset(k, 0, sizeof *k);
    G *g = calloc(1, sizeof *g);
    BlackwellIRProgram *p = calloc(1, sizeof *p);
    char dummy[8];
    if (!g || !p) { free(g); free(p); if (err && errn) snprintf(err, errn, "out of memory"); return -1; }
    g->u = u; g->f = &u->funcs[fn]; g->k = k; g->p = p;
    g->err = err && errn ? err : dummy; g->errn = err && errn ? errn : sizeof dummy;
    omega_bw_ir_init(p);
    g->e.p = p;
    const OscFunc *f = g->f;
    snprintf(k->name, sizeof k->name, "%s", f->name);
    k->threads = f->threads;
    int rc = -1;
    if (f->kind != OSC_FN_KERNEL) fail(g, "not a kernel");
    else if (f->threads < OSC_KERNEL_MIN_THREADS || f->threads > OSC_KERNEL_MAX_THREADS || f->threads % OSC_WARP) fail(g, "bad thread count %u", f->threads);
    else if (cfg(g) == 0 && params_and_layout(g) == 0) {
        uniformity(g);
        liveness(g);
        if (regions(g) == 0) {
            prologue(g);
            for (int b = 0; b < g->nb && !g->failed; b++) emit_block(g, b);
            if (!g->failed && g->e.err) fail(g, "the program is longer than the builder holds (%d instructions)", BW_MAX_IR_INSNS);
            if (!g->failed) {
                tail(&g->e);
                if (g->e.err) fail(g, "the program is longer than the builder holds");
                else if (finish(g) == 0) rc = 0;
            }
        }
    }
    if (rc == 0) k->prog = p;
    else { free(p); free(k->code); k->code = NULL; }
    free(g);
    return rc;
}

void osc_gpu_free(OscGpuKernel *k) {
    if (!k) return;
    free(k->code); free(k->prog);
    k->code = NULL; k->prog = NULL;
}
