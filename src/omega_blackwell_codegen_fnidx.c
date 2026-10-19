/*
 * omega_blackwell_codegen_fnidx.c: the Flash-Next QSA indexer phases (see the header). Emitter helpers copied from omega_blackwell_codegen_fnhc.c. The
 * emitter helpers are the ones of omega_blackwell_codegen_attn.c (control words, read barrier on every load, loop
 * live ranges); the phases are new.
 */
#include "omega_blackwell_codegen_fnidx.h"
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


/* d = LUT(a, b, c) with the LOP3 truth table (a = 0xf0, b = 0xcc, c = 0xaa); vreg -1 encodes RZ (omega_blackwell_codegen_router_fn.c) */
static void lop3(Em *e, int d, int a, int b, int c, uint32_t lut) { BlackwellIRInsn i = I0(BW_IR_LOP3_LUT); i.dst_vreg = d; i.src1_vreg = a; i.src2_vreg = b; i.src3_vreg = c; i.imm = lut; emit(e, i, K_FIXED); }
#define MUT spec->mutant
static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

uint32_t omega_fnidx_grid(uint32_t phase, uint32_t nb) {
    if (phase == OMEGA_FNIDX_SCORE) return (nb + OMEGA_FNIDX_THREADS - 1u) / OMEGA_FNIDX_THREADS;
    return phase == OMEGA_FNIDX_SELECT ? 1u : 0u;
}

/* d = (a < b) as 0/1, unsigned: the borrow of a - b = ((~a & b) | (~(a ^ b) & (a - b))) >> 31 ; t is scratch */
static void lt_u32(Em *e, int d, int a, int b, int t) {
    imadi(e, R(t), R(b), 0xffffffffu, a);   /* a - b */
    lop3(e, d, a, b, t, 0x8eu);
    shr(e, d, d, 31);
}
/* d = (a < imm) as 0/1 for a register a and an immediate that is already in register `bi` (callers keep constants in registers) */

static int gen_score(const OmegaFnIdxSpec *spec, BlackwellIRProgram *prog) {
    omega_bw_ir_init(prog);
    Em em; memset(&em, 0, sizeof em); em.p = prog; Em *e = &em;
    const int MUTP = spec->mutant;
    int udesc = UV64(e);
    int pq = V64(e), pk = V64(e), ps = V64(e), nb = V(e), scale = V(e);
    int tid = V(e), cx = V(e), one = V(e), b = V(e), t1 = V(e), t2 = V(e), ad = V64(e), qaddr = V(e), kaddr = V64(e);
    int kq = V128(e), q4 = V128(e), a0 = V(e), a1 = V(e), a2 = V(e), a3 = V(e), zero = V(e), sc = V(e), nbm1 = V(e), over = V(e), bc = V(e);
    int qv = V(e), cnt = V(e);
    if (e->err) return -1;
    ldcu64(e, udesc, 0x358);
    ldc64(e, pq, OMEGA_FNIDX_ARG_A); ldc64(e, pk, OMEGA_FNIDX_ARG_B); ldc64(e, ps, OMEGA_FNIDX_ARG_C);
    ldc32(e, nb, OMEGA_FNIDX_ARG_NB); ldc32(e, scale, OMEGA_FNIDX_ARG_SCALE);
    s2r(e, tid, BW_SR_TID_X); s2r(e, cx, BW_SR_CTAID_X);
    movi(e, one, 1); movrz(e, R(zero));
    /* stage q [512] into shared memory: thread t copies words t and t + 256 */
    for (int h = 0; h < 2; h++) {
        imadi(e, R(t1), R(tid), 4u, NOREG); imadi(e, R(t1), R(one), 1024u * (uint32_t)h, t1);
        imadw(e, ad, t1, 1u, pq); ldg32(e, BW_IR_LDG_E, qv, ad, udesc, 1);
        sts32(e, t1, R(qv));
    }
    bar_sync(e);
    /* this thread's block, clamped to nb - 1 for the loads (the store of an out-of-range block is skipped) */
    imadi(e, R(b), R(cx), 256u, tid);
    imadi(e, R(nbm1), R(one), 0xffffffffu, nb);
    lt_u32(e, over, nbm1, b, t1);                          /* b > nb - 1 */
    imadi(e, R(t2), R(b), 0xffffffffu, nbm1);              /* nb - 1 - b (wraps when over) */
    imadr(e, t2, over, t2, NOREG);                         /* over * (nb - 1 - b) */
    iadd(e, bc, b, t2);                                    /* b + over * (nb - 1 - b) = the clamped block */
    imadi(e, R(t1), R(bc), 512u, NOREG);
    imadw(e, kaddr, t1, 1u, pk);
    movrz(e, R(a0)); movrz(e, R(a1)); movrz(e, R(a2)); movrz(e, R(a3));
    movrz(e, R(qaddr)); movrz(e, R(cnt));
    movi(e, sc, 32);
    const int L_d = here(e);
    loop_begin(e);
    isetp_ge_u32(e, cnt, sc);
    const int bra_end = bra_if_p0_fwd(e);
    ldg_wide(e, BW_IR_LDG_E_128, kq, kaddr, 0, udesc, 1);
    int accs[4] = { a0, a1, a2, a3 };
    for (int h = 0; h < 4; h++) {
        lds128(e, q4, qaddr, (uint32_t)h * 512u, 1);
        for (unsigned q = 0; q < 4; q++) ffma(e, R(accs[h]), S(kq, q), S(q4, q), R(accs[h]));
    }
    imadw(e, kaddr, one, 16u, kaddr);
    imadi(e, R(qaddr), R(one), 16u, qaddr);
    imadi(e, R(cnt), R(one), 1u, cnt);
    bra(e, L_d);
    loop_end(e);
    patch_bra(e, bra_end, here(e));
    /* relu, sum over the heads in order, scale */
    int z = V(e); if (e->err) return -1;
    movrz(e, R(z));
    if (MUTP != 1) for (int h = 0; h < 4; h++) fmaxx(e, R(accs[h]), R(accs[h]), R(z));
    fadd(e, R(a0), R(a0), R(a1));
    fadd(e, R(a0), R(a0), R(a2));
    if (MUTP != 2) fadd(e, R(a0), R(a0), R(a3));
    if (MUTP != 3) fmul(e, R(a0), R(a0), R(scale));
    /* store when b < nb */
    isetp_ge_u32(e, b, nb);
    { Region rg = region_skip_if_p0(e); imadw(e, ad, b, 4u, ps); stg(e, BW_IR_STG_E, ad, R(a0), udesc); region_end(e, rg); }
    (void)zero;
    tail(e);
    if (e->err) return -1;
    return omega_bw_regalloc_solve(prog);
}

/* block-wide integer sum of v (every thread gets the total): warp reduce, per-warp words at sm[0..7], 8 loads; two barriers so
 * the words can be reused by the next call. tmp registers are scratch. */
static void block_sum_u32(Em *e, int v, int tmp, int warp, int lane, int one, int zero, int pv[8], int sa, int tot) {
    for (uint32_t o = 16; o >= 1; o /= 2) { shfl_down(e, tmp, v, o); iadd(e, v, v, tmp); }
    isetp_ge_u32(e, lane, one);
    { Region r = region_skip_if_p0(e); imadi(e, R(sa), R(warp), 4u, NOREG); sts32(e, sa, R(v)); region_end(e, r); }
    bar_sync(e);
    for (int q = 0; q < 8; q++) { imadi(e, R(sa), R(one), 4u * (uint32_t)q, zero); lds32(e, R(pv[q]), sa, q == 0); }
    iadd(e, tot, pv[0], pv[1]);
    for (int q = 2; q < 8; q++) iadd(e, tot, tot, pv[q]);
    bar_sync(e);
}

#define SM_RED 0u      /* 8 words */
#define SM_GT 64u      /* 256 words: per-chunk count of keys > T */
#define SM_EQ 1088u    /* 256 words: per-chunk count of keys == T */

static int gen_select(const OmegaFnIdxSpec *spec, BlackwellIRProgram *prog) {
    omega_bw_ir_init(prog);
    Em em; memset(&em, 0, sizeof em); em.p = prog; Em *e = &em;
    const int MUTP = spec->mutant;
    int udesc = UV64(e);
    int ps = V64(e), po = V64(e), nb = V(e), K = V(e), vis = V(e);
    int tid = V(e), one = V(e), zero = V(e), warp = V(e), lane = V(e), c256 = V(e);
    int T = V(e), mask = V(e), cand = V(e), local = V(e), i = V(e), b = V(e), bc = V(e), key = V(e), lt = V(e), over = V(e), nbm1 = V(e), valid = V(e);
    int t1 = V(e), t2 = V(e), t3 = V(e), tmp = V(e), ad = V64(e), sa = V(e), tot = V(e), cge = V(e), okb = V(e), iters = V(e), ncount = V(e);
    int pv[8]; for (int q = 0; q < 8; q++) pv[q] = V(e);
    if (e->err) return -1;
    ldcu64(e, udesc, 0x358);
    ldc64(e, ps, OMEGA_FNIDX_ARG_A); ldc64(e, po, OMEGA_FNIDX_ARG_B);
    ldc32(e, nb, OMEGA_FNIDX_ARG_NB); ldc32(e, K, OMEGA_FNIDX_ARG_K); ldc32(e, vis, OMEGA_FNIDX_ARG_VIS);
    s2r(e, tid, BW_SR_TID_X);
    movi(e, one, 1); movrz(e, R(zero)); movi(e, c256, 256);
    shr(e, warp, tid, 5); andi(e, R(lane), R(tid), 31u);
    imadi(e, R(nbm1), R(one), 0xffffffffu, nb);
    /* iters = ceil(nb / 256) */
    imadi(e, R(iters), R(one), 255u, nb); shr(e, iters, iters, 8);

    /* (1) T = the K-th largest key: bits 30 .. 0 (scores are >= 0, bit 31 is clear) */
    movrz(e, R(T)); movi(e, mask, 0x40000000u);
    const int L_bits = here(e);
    loop_begin(e);
    isetp_ge_u32(e, MUTP == 5 ? one : zero, mask);        /* mask == 0: done (mutant 5: also when mask == 1, bit 0 never tried) */
    const int bra_bits = bra_if_p0_fwd(e);
    iadd(e, cand, T, mask);
    movrz(e, R(local)); movrz(e, R(i));
    const int L_cnt = here(e);
    loop_begin(e);
    isetp_ge_u32(e, i, iters);
    const int bra_cnt = bra_if_p0_fwd(e);
    imadi(e, R(b), R(i), 256u, tid);                       /* block t + 256 i, clamped for the load, counted only when valid */
    lt_u32(e, over, nbm1, b, t1);
    imadi(e, R(t2), R(b), 0xffffffffu, nbm1); imadr(e, t2, over, t2, NOREG); iadd(e, bc, b, t2);
    imadw(e, ad, bc, 4u, ps); ldg32(e, BW_IR_LDG_E, key, ad, udesc, 1);
    lt_u32(e, lt, key, cand, t1);                          /* key < cand */
    imadi(e, R(valid), R(over), 0xffffffffu, one);         /* 1 - over */
    imadr(e, lt, lt, valid, NOREG);
    iadd(e, local, local, lt);
    imadi(e, R(i), R(one), 1u, i);
    bra(e, L_cnt);
    loop_end(e);
    patch_bra(e, bra_cnt, here(e));
    block_sum_u32(e, local, tmp, warp, lane, one, zero, pv, sa, tot);
    /* a clamped-out thread block counted 0 above, so tot = number of valid keys below cand */
    imadi(e, R(cge), R(tot), 0xffffffffu, nb);             /* nb - tot = keys >= cand */
    lt_u32(e, okb, cge, K, t1);                            /* cge < K */
    imadi(e, R(okb), R(okb), 0xffffffffu, one);            /* 1 - lt: cge >= K */
    imadi(e, R(t2), R(okb), 0xffffffffu, NOREG);           /* -ok */
    lop3(e, T, T, cand, t2, 0xf8u);                        /* T | (cand & -ok) */
    shr(e, mask, mask, 1);
    bra(e, L_bits);
    loop_end(e);
    patch_bra(e, bra_bits, here(e));

    /* (2) chunk counts: thread t owns blocks t * C .. t * C + C - 1, C = iters */
    int gtc = V(e), eqc = V(e), ge1 = V(e), gt1 = V(e), eq1 = V(e);
    if (e->err) return -1;
    movrz(e, R(gtc)); movrz(e, R(eqc)); movrz(e, R(i));
    imadr(e, t3, tid, iters, NOREG);                       /* first block of the chunk */
    const int L_c2 = here(e);
    loop_begin(e);
    isetp_ge_u32(e, i, iters);
    const int bra_c2 = bra_if_p0_fwd(e);
    iadd(e, b, t3, i);
    lt_u32(e, over, nbm1, b, t1);
    imadi(e, R(t2), R(b), 0xffffffffu, nbm1); imadr(e, t2, over, t2, NOREG); iadd(e, bc, b, t2);
    imadw(e, ad, bc, 4u, ps); ldg32(e, BW_IR_LDG_E, key, ad, udesc, 1);
    imadi(e, R(valid), R(over), 0xffffffffu, one);
    lt_u32(e, gt1, T, key, t1);                            /* key > T */
    lt_u32(e, ge1, key, T, t1); imadi(e, R(ge1), R(ge1), 0xffffffffu, one);   /* key >= T */
    imadi(e, R(eq1), R(gt1), 0xffffffffu, ge1);            /* ge - gt */
    imadr(e, gt1, gt1, valid, NOREG); imadr(e, eq1, eq1, valid, NOREG);
    iadd(e, gtc, gtc, gt1); iadd(e, eqc, eqc, eq1);
    imadi(e, R(i), R(one), 1u, i);
    bra(e, L_c2);
    loop_end(e);
    patch_bra(e, bra_c2, here(e));
    imadi(e, R(sa), R(tid), 4u, NOREG); imadi(e, R(t1), R(one), SM_GT, sa); sts32(e, t1, R(gtc));
    imadi(e, R(t1), R(one), SM_EQ, sa); sts32(e, t1, R(eqc));
    bar_sync(e);
    /* totals and prefixes: every thread walks the 256 chunk counts */
    int gtot = V(e), eneed = V(e), eqpre = V(e), selpre = V(e), taket = V(e), j = V(e), gj = V(e), ej = V(e), room = V(e), take = V(e), before = V(e), after = V(e), selfm = V(e), neg = V(e);
    if (e->err) return -1;
    movrz(e, R(gtot)); movrz(e, R(j));
    const int L_t1 = here(e);
    loop_begin(e);
    isetp_ge_u32(e, j, c256);
    const int bra_t1 = bra_if_p0_fwd(e);
    imadi(e, R(sa), R(j), 4u, NOREG); imadi(e, R(t1), R(one), SM_GT, sa); lds32(e, R(gj), t1, 1);
    iadd(e, gtot, gtot, gj);
    imadi(e, R(j), R(one), 1u, j);
    bra(e, L_t1);
    loop_end(e);
    patch_bra(e, bra_t1, here(e));
    imadi(e, R(eneed), R(gtot), 0xffffffffu, K);           /* K - count(> T): equal-key blocks to take */
    movrz(e, R(eqpre)); movrz(e, R(selpre)); movrz(e, R(taket)); movrz(e, R(j));
    const int L_t2 = here(e);
    loop_begin(e);
    isetp_ge_u32(e, j, c256);
    const int bra_t2 = bra_if_p0_fwd(e);
    imadi(e, R(sa), R(j), 4u, NOREG);
    imadi(e, R(t1), R(one), SM_GT, sa); lds32(e, R(gj), t1, 1);
    imadi(e, R(t1), R(one), SM_EQ, sa); lds32(e, R(ej), t1, 0);
    imadi(e, R(room), R(eqpre), 0xffffffffu, eneed);       /* eneed - eqpre (never negative: eqpre <= eneed once the room is used up is clamped below) */
    shr(e, neg, room, 31);                                 /* 1 when negative */
    imadi(e, R(neg), R(neg), 0xffffffffu, one);            /* 1 - neg */
    imadr(e, room, room, neg, NOREG);                      /* max(room, 0) */
    lt_u32(e, t2, ej, room, t1);                           /* ej < room */
    imadi(e, R(t3), R(room), 0xffffffffu, ej);             /* ej - room */
    imadr(e, t3, t2, t3, NOREG);
    iadd(e, take, room, t3);                               /* min(ej, room) = room + (ej < room) * (ej - room) */
    lt_u32(e, before, j, tid, t1);                         /* j < t */
    lt_u32(e, after, tid, j, t1);                          /* t < j */
    iadd(e, t2, before, after); imadi(e, R(selfm), R(t2), 0xffffffffu, one);   /* 1 - before - after: j == t */
    if (MUTP == 7) { /* mutant: chunk order reversed in the slot prefix */ iadd(e, before, after, zero); }
    iadd(e, t2, gj, take);
    imadr(e, selpre, before, t2, selpre);                  /* selpre += before * (gj + take) */
    imadr(e, taket, selfm, take, taket);                   /* taket += self * take */
    iadd(e, eqpre, eqpre, ej);
    imadi(e, R(j), R(one), 1u, j);
    bra(e, L_t2);
    loop_end(e);
    patch_bra(e, bra_t2, here(e));
    imadr(e, t3, tid, iters, NOREG);                       /* first block of the chunk (recomputed: t3 was reused) */

    /* (3) write this chunk's selected blocks ascending */
    int seen = V(e), sel = V(e), eqr = V(e), q4 = V(e), pos = V(e);
    if (e->err) return -1;
    movrz(e, R(seen)); movrz(e, R(i));
    const int L_w = here(e);
    loop_begin(e);
    isetp_ge_u32(e, i, iters);
    const int bra_w = bra_if_p0_fwd(e);
    iadd(e, b, t3, i);
    lt_u32(e, over, nbm1, b, t1);
    imadi(e, R(t2), R(b), 0xffffffffu, nbm1); imadr(e, t2, over, t2, NOREG); iadd(e, bc, b, t2);
    imadw(e, ad, bc, 4u, ps); ldg32(e, BW_IR_LDG_E, key, ad, udesc, 1);
    imadi(e, R(valid), R(over), 0xffffffffu, one);
    lt_u32(e, gt1, T, key, t1);
    lt_u32(e, ge1, key, T, t1); imadi(e, R(ge1), R(ge1), 0xffffffffu, one);
    imadi(e, R(eq1), R(gt1), 0xffffffffu, ge1);
    lt_u32(e, room, seen, taket, t1);                      /* seen < take: an equal-key block is still wanted */
    if (MUTP == 4) { /* ties go to the higher index: want when seen >= taket is impossible; invert the room test */ imadi(e, R(room), R(room), 0xffffffffu, one); }
    imadr(e, eqr, eq1, room, NOREG);                       /* take this equal-key block */
    iadd(e, sel, gt1, eqr);
    imadr(e, sel, sel, valid, NOREG);
    imadr(e, eqr, eqr, valid, NOREG);
    iadd(e, seen, seen, eqr);
    /* write when sel: 4 positions b * 4 + q at slot selpre * 4 + q */
    isetp_ge_u32(e, zero, sel);                            /* sel == 0: skip */
    {
        Region rg = region_skip_if_p0(e);
        imadi(e, R(pos), R(b), 4u, NOREG);
        imadw(e, ad, selpre, 16u, po);
        for (unsigned q = 0; q < 4; q++) {
            imadi(e, R(t2), R(one), q, pos);
            stg(e, BW_IR_STG_E, ad, R(t2), udesc);
            imadw(e, ad, one, 4u, ad);
        }
        region_end(e, rg);
    }
    iadd(e, selpre, selpre, sel);
    imadi(e, R(i), R(one), 1u, i);
    bra(e, L_w);
    loop_end(e);
    patch_bra(e, bra_w, here(e));
    (void)q4; (void)ncount;

    /* (4) the tail: positions nb * 4 + t for t < vis - nb * 4 (at most 3), after the 4 K block positions */
    int tl = V(e), tc = V(e), c3 = V(e);
    if (e->err) return -1;
    imadi(e, R(t1), R(nb), 0xfffffffcu /* -4 */, vis);     /* vis - 4 nb */
    movi(e, c3, 3);
    isetp_ge_u32(e, tid, t1);                              /* t >= tail count: skip */
    {
        Region rg = region_skip_if_p0(e);
        imadi(e, R(tl), R(nb), 4u, tid);
        imadi(e, R(tc), R(K), 4u, tid);
        imadw(e, ad, tc, 4u, po);
        if (MUTP != 6) stg(e, BW_IR_STG_E, ad, R(tl), udesc);
        region_end(e, rg);
    }
    (void)c3; (void)tmp;
    tail(e);
    if (e->err) return -1;
    return omega_bw_regalloc_solve(prog);
}

int omega_blackwell_codegen_fnidx_prog(const OmegaFnIdxSpec *spec, BlackwellIRProgram *prog) {
    if (!spec || !prog || spec->phase >= OMEGA_FNIDX_PHASES || spec->mutant < 0 || spec->mutant > 7) return -1;
    static const int mphase[8] = { -1, OMEGA_FNIDX_SCORE, OMEGA_FNIDX_SCORE, OMEGA_FNIDX_SCORE, OMEGA_FNIDX_SELECT, OMEGA_FNIDX_SELECT, OMEGA_FNIDX_SELECT, OMEGA_FNIDX_SELECT };
    if (spec->mutant && (int)spec->phase != mphase[spec->mutant]) return -1;
    return spec->phase == OMEGA_FNIDX_SCORE ? gen_score(spec, prog) : gen_select(spec, prog);
}

int omega_fnidx_prog_stats(const OmegaFnIdxSpec *spec, uint32_t *insns, uint32_t *gprs) {
    static BlackwellIRProgram prog;
    if (omega_blackwell_codegen_fnidx_prog(spec, &prog) != 0) return -1;
    if (insns) *insns = (uint32_t)prog.count;
    if (gprs) *gprs = prog.regalloc.peak_gpr_usage;
    return 0;
}
