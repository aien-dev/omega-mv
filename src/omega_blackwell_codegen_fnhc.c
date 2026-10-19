/*
 * omega_blackwell_codegen_fnhc.c: the Flash-Next 4-lane gated residual phases (see the header). Emitter helpers copied from omega_blackwell_codegen_fnattn.c. The
 * emitter helpers are the ones of omega_blackwell_codegen_attn.c (control words, read barrier on every load, loop
 * live ranges); the phases are new.
 */
#include "omega_blackwell_codegen_fnhc.h"
#pragma GCC diagnostic ignored "-Wunused-function"
#include <string.h>

#define CW(stall, wbar, rbar, wait) \
    (((uint32_t)(stall) << 9) | ((uint32_t)(wbar) << 14) | ((uint32_t)(rbar) << 17) | ((uint32_t)(wait) << 20))
#define CW_YIELD 0x2000u
#define SB_NONE 7u
#define SB_GPR 0u
#define SB_RD 1u
#define SB_UGPR 2u
#define NOREG (-1)
#define MAX_LOOPS 4

enum { K_FIXED, K_PRED, K_VAR, K_VAR_MORE, K_UVAR, K_STORE, K_SYNC, K_EXIT, K_BRA, K_BSSY, K_BSYNC };

typedef struct {
    BlackwellIRProgram *p;
    uint32_t pending;
    int err;
    int open_start[MAX_LOOPS];
    int depth;
} Em;

static void emit(Em *e, BlackwellIRInsn in, int kind) {
    uint32_t wait = e->pending;
    switch (kind) {
    case K_FIXED: in.control = CW(6, SB_NONE, SB_NONE, wait); e->pending = 0; break;
    case K_PRED:  in.control = CW(14, SB_NONE, SB_NONE, wait); e->pending = 0; break;
    case K_VAR:   in.control = CW(4, SB_GPR, SB_RD, wait) | CW_YIELD; e->pending = (1u << SB_GPR) | (1u << SB_RD); break;
    case K_VAR_MORE: in.control = CW(4, SB_GPR, SB_RD, (e->pending & (1u << SB_RD))) | CW_YIELD; e->pending |= (1u << SB_GPR) | (1u << SB_RD); break;
    case K_UVAR:  in.control = CW(4, SB_UGPR, SB_NONE, wait) | CW_YIELD; e->pending = 1u << SB_UGPR; break;
    case K_STORE: in.control = CW(4, SB_NONE, SB_RD, wait) | CW_YIELD; e->pending = 1u << SB_RD; break;
    case K_SYNC:  in.control = CW(6, SB_NONE, SB_NONE, wait); e->pending = 0; break;
    case K_EXIT:  in.control = CW(5, SB_NONE, SB_NONE, wait); e->pending = in.predicate_p0 ? wait : 0; break;
    case K_BRA:   in.control = CW(5, SB_NONE, SB_NONE, wait); e->pending = in.predicate_p0 ? wait : 0; break;
    case K_BSSY:  in.control = CW(1, SB_NONE, SB_NONE, wait) | CW_YIELD; e->pending = 0; break;
    case K_BSYNC: in.control = CW(5, SB_NONE, SB_NONE, wait) | CW_YIELD; e->pending = 0; break;
    default: e->err = -1; return;
    }
    if (omega_bw_ir_append(e->p, &in) < 0) e->err = -1;
}

static BlackwellIRInsn I0(BlackwellIROpcode op) {
    BlackwellIRInsn i; memset(&i, 0, sizeof i);
    i.op = op; i.dst_vreg = NOREG; i.src1_vreg = NOREG; i.src2_vreg = NOREG; i.src3_vreg = NOREG; i.ureg = NOREG;
    return i;
}
static int V(Em *e) { int v = omega_bw_ir_alloc_vreg(e->p); if (v < 0) e->err = -1; return v; }
static int V64(Em *e) { int v = omega_bw_ir_alloc_vreg64(e->p); if (v < 0) e->err = -1; return v; }
static int V128(Em *e) { int v = omega_bw_ir_alloc_vreg128(e->p); if (v < 0) e->err = -1; return v; }
static int UV64(Em *e) { int v = omega_bw_ir_alloc_uvreg64(e->p); if (v < 0) e->err = -1; return v; }
static int here(const Em *e) { return (int)e->p->count; }

/* operand with subregister */
typedef struct { int v; unsigned s; } Op;
static Op R(int v) { Op o = { v, 0 }; return o; }
static Op S(int v, unsigned s) { Op o = { v, s }; return o; }

static void ldcu64(Em *e, int ud, uint32_t off) { BlackwellIRInsn i = I0(BW_IR_LDCU64); i.dst_vreg = ud; i.imm = off; i.is_uniform = true; emit(e, i, K_UVAR); }
static void ldc64(Em *e, int d, uint32_t off) { BlackwellIRInsn i = I0(BW_IR_LDC64); i.dst_vreg = d; i.imm = off; emit(e, i, K_VAR); }
static void ldc32(Em *e, int d, uint32_t off) { BlackwellIRInsn i = I0(BW_IR_LDC); i.dst_vreg = d; i.imm = off; emit(e, i, K_VAR); }
static void s2r(Em *e, int d, uint32_t sr) { BlackwellIRInsn i = I0(BW_IR_S2R); i.dst_vreg = d; i.imm = sr; emit(e, i, K_VAR); }
static void movi(Em *e, int d, uint32_t imm) { BlackwellIRInsn i = I0(BW_IR_MOV_IMM); i.dst_vreg = d; i.imm = imm; emit(e, i, K_FIXED); }
static void movrz(Em *e, Op d) { BlackwellIRInsn i = I0(BW_IR_MOV_RZ); i.dst_vreg = d.v; i.dst_subreg = (uint8_t)d.s; emit(e, i, K_FIXED); }
/* d = a * imm + c */
static void imadi(Em *e, Op d, Op a, uint32_t imm, int c) { BlackwellIRInsn i = I0(BW_IR_IMAD); i.dst_vreg = d.v; i.dst_subreg = (uint8_t)d.s; i.src1_vreg = a.v; i.src1_subreg = (uint8_t)a.s; i.imm = imm; i.src3_vreg = c; emit(e, i, K_FIXED); }
/* d = a * b + c (registers) */
static void imadr(Em *e, int d, int a, int b, int c) { BlackwellIRInsn i = I0(BW_IR_IMAD); i.dst_vreg = d; i.src1_vreg = a; i.src2_vreg = b; i.src3_vreg = c; emit(e, i, K_FIXED); }
/* d64 = a * imm + c64 */
static void imadw(Em *e, int d64, int a, uint32_t imm, int c64) { BlackwellIRInsn i = I0(BW_IR_IMAD_WIDE); i.dst_vreg = d64; i.src1_vreg = a; i.imm = imm; i.src3_vreg = c64; emit(e, i, K_FIXED); }
static void mov(Em *e, Op d, Op a) { imadi(e, d, a, 1u, NOREG); }
static void shrs(Em *e, int d, Op a, uint32_t imm) { BlackwellIRInsn i = I0(BW_IR_SHF_R); i.dst_vreg = d; i.src1_vreg = a.v; i.src1_subreg = (uint8_t)a.s; i.imm = imm; emit(e, i, K_FIXED); }
static void shr(Em *e, int d, int a, uint32_t imm) { shrs(e, d, R(a), imm); }
static void andi(Em *e, Op d, Op a, uint32_t imm) { BlackwellIRInsn i = I0(BW_IR_LOP3_AND); i.dst_vreg = d.v; i.dst_subreg = (uint8_t)d.s; i.src1_vreg = a.v; i.src1_subreg = (uint8_t)a.s; i.imm = imm; emit(e, i, K_FIXED); }
static void xorr(Em *e, Op d, Op a, Op b) { BlackwellIRInsn i = I0(BW_IR_LOP3_XOR); i.dst_vreg = d.v; i.dst_subreg = (uint8_t)d.s; i.src1_vreg = a.v; i.src1_subreg = (uint8_t)a.s; i.src2_vreg = b.v; i.src2_subreg = (uint8_t)b.s; emit(e, i, K_FIXED); }
static void iadd(Em *e, int d, int a, int b) { BlackwellIRInsn i = I0(BW_IR_IADD3); i.dst_vreg = d; i.src1_vreg = a; i.src2_vreg = b; emit(e, i, K_FIXED); }
static void ldg_wide(Em *e, BlackwellIROpcode op, int d, int addr64, uint32_t imm, int udesc, int first) {
    BlackwellIRInsn i = I0(op); i.dst_vreg = d; i.src1_vreg = addr64; i.imm = imm; i.ureg = udesc; emit(e, i, first ? K_VAR : K_VAR_MORE);
}
static void ldg32(Em *e, BlackwellIROpcode op, int d, int addr64, int udesc, int first) { BlackwellIRInsn i = I0(op); i.dst_vreg = d; i.src1_vreg = addr64; i.ureg = udesc; emit(e, i, first ? K_VAR : K_VAR_MORE); }
static void stg(Em *e, BlackwellIROpcode op, int addr64, Op val, int udesc) { BlackwellIRInsn i = I0(op); i.src1_vreg = addr64; i.src2_vreg = val.v; i.src2_subreg = (uint8_t)val.s; i.ureg = udesc; emit(e, i, K_STORE); }
static void atom_add(Em *e, int d, int addr64, int val, int udesc) { BlackwellIRInsn i = I0(BW_IR_ATOMG_ADD_STRONG_SYS); i.dst_vreg = d; i.src1_vreg = addr64; i.src2_vreg = val; i.ureg = udesc; emit(e, i, K_VAR); }
static void membar(Em *e) { emit(e, I0(BW_IR_MEMBAR_ALL_SYS), K_FIXED); }
static void lds128(Em *e, int d128, int addr, uint32_t imm, int first) { BlackwellIRInsn i = I0(BW_IR_LDS128); i.dst_vreg = d128; i.src1_vreg = addr; i.imm = imm; emit(e, i, first ? K_VAR : K_VAR_MORE); }
static void lds32(Em *e, Op d, int addr, int first) { BlackwellIRInsn i = I0(BW_IR_LDS32); i.dst_vreg = d.v; i.dst_subreg = (uint8_t)d.s; i.src1_vreg = addr; emit(e, i, first ? K_VAR : K_VAR_MORE); }
static void sts32(Em *e, int addr, Op val) { BlackwellIRInsn i = I0(BW_IR_STS32); i.src1_vreg = addr; i.src2_vreg = val.v; i.src2_subreg = (uint8_t)val.s; emit(e, i, K_STORE); }
static void bar_sync(Em *e) { emit(e, I0(BW_IR_BAR_SYNC), K_SYNC); }
static void fop(Em *e, BlackwellIROpcode op, Op d, Op a, Op b, Op c) {
    BlackwellIRInsn i = I0(op); i.dst_vreg = d.v; i.dst_subreg = (uint8_t)d.s; i.src1_vreg = a.v; i.src1_subreg = (uint8_t)a.s;
    i.src2_vreg = b.v; i.src2_subreg = (uint8_t)b.s; i.src3_vreg = c.v; i.src3_subreg = (uint8_t)c.s; emit(e, i, K_FIXED);
}
static void fadd(Em *e, Op d, Op a, Op b) { fop(e, BW_IR_FADD, d, a, b, R(NOREG)); }
static void fsub(Em *e, Op d, Op a, Op b) { fop(e, BW_IR_FSUB, d, a, b, R(NOREG)); }
static void fmul(Em *e, Op d, Op a, Op b) { fop(e, BW_IR_FMUL, d, a, b, R(NOREG)); }
static void ffma(Em *e, Op d, Op a, Op b, Op c) { fop(e, BW_IR_FFMA, d, a, b, c); }
static void fmaxx(Em *e, Op d, Op a, Op b) { fop(e, BW_IR_FMNMX_MAX, d, a, b, R(NOREG)); }
static void mufu(Em *e, BlackwellIROpcode op, Op d, Op a) { BlackwellIRInsn i = I0(op); i.dst_vreg = d.v; i.dst_subreg = (uint8_t)d.s; i.src1_vreg = a.v; i.src1_subreg = (uint8_t)a.s; emit(e, i, K_VAR); }
static void shfl_down(Em *e, int d, int a, uint32_t off) { BlackwellIRInsn i = I0(BW_IR_SHFL_DOWN); i.dst_vreg = d; i.src1_vreg = a; i.imm = off; emit(e, i, K_VAR); }
static void isetp_ge_u32(Em *e, int a, int b) { BlackwellIRInsn i = I0(BW_IR_ISETP_GE_U32); i.src1_vreg = a; i.src2_vreg = b; emit(e, i, K_PRED); }
static void bra(Em *e, int target) { BlackwellIRInsn i = I0(BW_IR_BRA); i.imm = (uint32_t)(target - here(e)); emit(e, i, K_BRA); }
static int bra_if_p0_fwd(Em *e) { int at = here(e); BlackwellIRInsn i = I0(BW_IR_BRA); i.predicate_p0 = true; i.imm = 1; emit(e, i, K_BRA); return at; }
static void patch_bra(Em *e, int at, int target) { if (!e->err) e->p->insns[at].imm = (uint32_t)(target - at); }
static int bssy_fwd(Em *e) { int at = here(e); BlackwellIRInsn i = I0(BW_IR_BSSY); i.bar_reg = 0; i.imm = 2; emit(e, i, K_BSSY); return at; }
static void bsync(Em *e) { BlackwellIRInsn i = I0(BW_IR_BSYNC); i.bar_reg = 0; emit(e, i, K_BSYNC); }
static void exit_k(Em *e) { emit(e, I0(BW_IR_EXIT), K_EXIT); }
static void tail(Em *e) {
    exit_k(e);
    BlackwellIRInsn self = I0(BW_IR_BRA);
    self.control = 0x000fc000u;
    if (omega_bw_ir_append(e->p, &self) < 0) e->err = -1;
}
static void loop_begin(Em *e) { if (e->depth >= MAX_LOOPS) { e->err = -1; return; } e->open_start[e->depth++] = here(e); }
static void loop_end(Em *e) {
    if (e->depth <= 0) { e->err = -1; return; }
    int start = e->open_start[--e->depth], end = here(e) - 1;
    OmegaRegAlloc *ra = &e->p->regalloc;
    for (int v = 0; v < ra->num_vregs; v++) {
        OmegaLiveInterval *iv = &ra->intervals[v];
        if (iv->first_def >= 0 && iv->first_def < start && iv->last_use >= start && iv->last_use < end) iv->last_use = end;
    }
}

/* A region only some lanes of a warp execute: BSSY ... @P0 BRA join ... BSYNC. P0 true = skip. */
typedef struct { int bssy, bra; } Region;
static Region region_skip_if_p0(Em *e) { Region r; r.bssy = bssy_fwd(e); r.bra = bra_if_p0_fwd(e); return r; }
static void region_end(Em *e, Region r) { int at = here(e); bsync(e); patch_bra(e, r.bra, at); patch_bra(e, r.bssy, at + 1); }

/* bf16 round-to-nearest-even of f32 x into the low 16 bits of d: r = x + 0x7fff + ((x >> 16) & 1); d = r >> 16 */
static void bf16_of(Em *e, int d, Op x, int c7fff, int t) {
    shrs(e, t, x, 16);
    andi(e, R(t), R(t), 1u);
    iadd(e, t, t, c7fff);
    fop(e, BW_IR_IADD3, R(d), x, R(t), R(NOREG)); /* IADD3 d = x + t */
    shr(e, d, d, 16);
}


#define MUT spec->mutant
static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

uint32_t omega_fnhc_grid(uint32_t phase) {
    switch (phase) { case OMEGA_FNHC_NORM: return 4; case OMEGA_FNHC_ACT: return 2; case OMEGA_FNHC_COMBINE: return 10; case OMEGA_FNHC_INJECT: return 40; default: return 0; }
}

/* d = 1 / (1 + EX2(x * -log2e)); t is a scratch register; c_nl2e and c_one hold the constants */
static void sigm(Em *e, int d, int x, int t, int c_nl2e, int c_one) {
    fmul(e, R(t), R(x), R(c_nl2e));
    mufu(e, BW_IR_MUFU_EX2, R(t), R(t));
    fadd(e, R(t), R(t), R(c_one));
    mufu(e, BW_IR_MUFU_RCP, R(d), R(t));
}

int omega_blackwell_codegen_fnhc_prog(const OmegaFnHcSpec *spec, BlackwellIRProgram *prog) {
    if (!spec || !prog || spec->phase >= OMEGA_FNHC_PHASES || spec->mutant < 0 || spec->mutant > 6) return -1;
    /* a mutant belongs to one phase; asking for it on another is a caller error (it would be an equivalent mutant) */
    static const int mphase[7] = { -1, OMEGA_FNHC_NORM, OMEGA_FNHC_ACT, OMEGA_FNHC_COMBINE, OMEGA_FNHC_COMBINE, OMEGA_FNHC_INJECT, OMEGA_FNHC_NORM };
    if (spec->mutant && (int)spec->phase != mphase[spec->mutant]) return -1;
    omega_bw_ir_init(prog);
    Em em; memset(&em, 0, sizeof em); em.p = prog; Em *e = &em;
    int udesc = UV64(e);
    int pa = V64(e), pb = V64(e), pc = V64(e), pd = V64(e), pe = V64(e), eps = V(e);
    int tid = V(e), cx = V(e), one = V(e), zero = V(e), warp = V(e), lane = V(e);
    int c_nl2e = V(e), c_one = V(e), t1 = V(e), t2 = V(e), t3 = V(e), ad = V64(e);
    if (e->err) return -1;
    ldcu64(e, udesc, 0x358);
    ldc64(e, pa, OMEGA_FNHC_ARG_A); ldc64(e, pb, OMEGA_FNHC_ARG_B); ldc64(e, pc, OMEGA_FNHC_ARG_C); ldc64(e, pd, OMEGA_FNHC_ARG_D); ldc64(e, pe, OMEGA_FNHC_ARG_E);
    ldc32(e, eps, OMEGA_FNHC_ARG_EPS);
    s2r(e, tid, BW_SR_TID_X); s2r(e, cx, BW_SR_CTAID_X);
    movi(e, one, 1); movrz(e, R(zero));
    shr(e, warp, tid, 5); andi(e, R(lane), R(tid), 31u);
    movi(e, c_nl2e, fbits(-1.4426950408889634f)); movi(e, c_one, fbits(1.0f));

    switch (spec->phase) {
    case OMEGA_FNHC_NORM: {
        /* thread t of CTA l: elements l * 2560 + t + 256 i, i < 10 */
        int x[10], w[10], off = V(e), sq = V(e), ts = V(e), rs = V(e), sa = V(e), y = V(e), c_inv = V(e);
        for (int i = 0; i < 10; i++) { x[i] = V(e); w[i] = V(e); }
        if (e->err) return -1;
        imadi(e, R(off), R(cx), OMEGA_FNHC_H * 4u, NOREG); imadi(e, R(off), R(tid), 4u, off);
        for (int i = 0; i < 10; i++) { imadi(e, R(t1), R(one), 1024u * (uint32_t)i, off); imadw(e, ad, t1, 1u, pa); ldg32(e, BW_IR_LDG_E, x[i], ad, udesc, i == 0); }
        fmul(e, R(sq), R(x[0]), R(x[0]));
        for (int i = 1; i < 10; i++) ffma(e, R(sq), R(x[i]), R(x[i]), R(sq));
        for (uint32_t o = 16; o >= 1; o /= 2) { shfl_down(e, ts, sq, o); fadd(e, R(sq), R(sq), R(ts)); }
        isetp_ge_u32(e, lane, one);
        { Region r = region_skip_if_p0(e); imadi(e, R(sa), R(warp), 4u, NOREG); sts32(e, sa, R(sq)); region_end(e, r); }
        bar_sync(e);
        movrz(e, R(sa));
        /* sum of the 8 warp partials in a fixed order (every thread computes the same value) */
        int pv[8]; for (int q = 0; q < 8; q++) pv[q] = V(e);
        if (e->err) return -1;
        for (int q = 0; q < 8; q++) { imadi(e, R(t1), R(one), 4u * (uint32_t)q, sa); lds32(e, R(pv[q]), t1, q == 0); }
        fadd(e, R(sq), R(pv[0]), R(pv[1]));
        for (int q = 2; q < 8; q++) fadd(e, R(sq), R(sq), R(pv[q]));
        movi(e, c_inv, fbits(MUT == 6 ? 1.0f / 2561.0f : 1.0f / 2560.0f));
        ffma(e, R(rs), R(sq), R(c_inv), R(eps));
        mufu(e, BW_IR_MUFU_RSQ, R(rs), R(rs));
        for (int i = 0; i < 10; i++) { imadi(e, R(t1), R(one), 1024u * (uint32_t)i, off); imadw(e, ad, t1, 1u, pb); ldg32(e, BW_IR_LDG_E, w[i], ad, udesc, 0); }
        for (int i = 0; i < 10; i++) {
            fmul(e, R(y), R(x[i]), R(rs));
            if (MUT != 1) fmul(e, R(y), R(y), R(w[i]));
            imadi(e, R(t1), R(one), 1024u * (uint32_t)i, off); imadw(e, ad, t1, 1u, pc);
            stg(e, BW_IR_STG_E, ad, R(y), udesc);
        }
        break;
    }
    case OMEGA_FNHC_ACT: {
        int idx = V(e), d = V(e), c320 = V(e), q = V(e), r = V(e);
        if (e->err) return -1;
        imadi(e, R(idx), R(cx), 256u, tid);
        movi(e, c320, OMEGA_FNHC_RANK);
        isetp_ge_u32(e, idx, c320);
        {
            Region rg = region_skip_if_p0(e);
            imadw(e, ad, idx, 4u, pa);
            ldg32(e, BW_IR_LDG_E, d, ad, udesc, 1);
            movi(e, t1, fbits(0.25f));
            if (MUT == 2) movi(e, t1, fbits(1.0f));
            fmul(e, R(q), R(d), R(t1));                    /* d / 4 */
            sigm(e, r, q, t2, c_nl2e, c_one);              /* sigmoid(d / 4) */
            fmul(e, R(q), R(q), R(r));                     /* silu = x * sigmoid(x) = x / (1 + e^-x) */
            imadw(e, ad, idx, 4u, pb);
            stg(e, BW_IR_STG_E, ad, R(q), udesc);
            region_end(e, rg);
        }
        break;
    }
    case OMEGA_FNHC_COMBINE: {
        int j = V(e), acc = V(e), u = V(e), n = V(e), m = V(e), c4 = V(e), g = V(e), inj = V(e), c025 = V(e);
        if (e->err) return -1;
        imadi(e, R(j), R(cx), 256u, tid);
        movrz(e, R(acc));
        for (int l = 0; l < 4; l++) {
            imadi(e, R(t1), R(j), 4u, NOREG); imadi(e, R(t1), R(one), (OMEGA_FNHC_H * 4u) * (uint32_t)l, t1);
            imadw(e, ad, t1, 1u, pa); ldg32(e, BW_IR_LDG_E, u, ad, udesc, 1);
            imadw(e, ad, t1, 1u, pb); ldg32(e, BW_IR_LDG_E, n, ad, udesc, 0);
            if (MUT == 3) mov(e, R(m), R(u)); else sigm(e, m, u, t2, c_nl2e, c_one);
            fmul(e, R(m), R(m), R(n));
            fadd(e, R(acc), R(acc), R(m));
        }
        movi(e, c025, fbits(0.25f));
        fmul(e, R(acc), R(acc), R(c025));
        imadw(e, ad, j, 4u, pd);
        stg(e, BW_IR_STG_E, ad, R(acc), udesc);
        /* CTA 0, threads 0..3: inj[l] = 2 * sigmoid(g[l] / 4) */
        movi(e, c4, 4u);
        isetp_ge_u32(e, j, c4);
        {
            Region rg = region_skip_if_p0(e);
            imadw(e, ad, j, 4u, pc);
            ldg32(e, BW_IR_LDG_E, g, ad, udesc, 1);
            if (MUT != 4) fmul(e, R(g), R(g), R(c025));
            sigm(e, inj, g, t3, c_nl2e, c_one);
            movi(e, t3, fbits(2.0f));
            fmul(e, R(inj), R(inj), R(t3));
            imadw(e, ad, j, 4u, pe);
            stg(e, BW_IR_STG_E, ad, R(inj), udesc);
            region_end(e, rg);
        }
        break;
    }
    case OMEGA_FNHC_INJECT: {
        int idx = V(e), l = V(e), j = V(e), lv = V(e), bv = V(e), iv = V(e);
        if (e->err) return -1;
        imadi(e, R(idx), R(cx), 256u, tid);                /* lane l * 2560 + j == cx * 256 + tid */
        imadi(e, R(l), R(cx), 103u, NOREG); shr(e, l, l, 10);   /* l = cx / 10 for cx < 40 */
        if (MUT == 5) movrz(e, R(l));
        imadi(e, R(t1), R(l), 0xfffff600u /* -2560 */, idx);      /* j = idx - 2560 l */
        mov(e, R(j), R(t1));
        imadw(e, ad, idx, 4u, pa); ldg32(e, BW_IR_LDG_E, lv, ad, udesc, 1);
        imadw(e, ad, j, 4u, pb);   ldg32(e, BW_IR_LDG_E, bv, ad, udesc, 0);
        imadw(e, ad, l, 4u, pc);   ldg32(e, BW_IR_LDG_E, iv, ad, udesc, 0);
        fmul(e, R(bv), R(bv), R(iv));
        fadd(e, R(lv), R(lv), R(bv));
        imadw(e, ad, idx, 4u, pa);
        stg(e, BW_IR_STG_E, ad, R(lv), udesc);
        break;
    }
    }
    (void)zero; (void)t2;
    tail(e);
    if (e->err) return -1;
    return omega_bw_regalloc_solve(prog);
}

int omega_fnhc_prog_stats(const OmegaFnHcSpec *spec, uint32_t *insns, uint32_t *gprs) {
    static BlackwellIRProgram prog;
    if (omega_blackwell_codegen_fnhc_prog(spec, &prog) != 0) return -1;
    if (insns) *insns = (uint32_t)prog.count;
    if (gprs) *gprs = prog.regalloc.peak_gpr_usage;
    return 0;
}
