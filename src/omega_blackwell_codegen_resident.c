/* omega_blackwell_codegen_resident.c: resident FP8 GEMV chain kernel with an on-chip grid barrier (OWN-30B W-L2f).
 * See the header for the contract. The emitter helpers and the scheduling rules are copied from
 * omega_blackwell_codegen_moe.c (static there): every instruction waits on everything still pending, variable-latency
 * results through scoreboard 0, loads set the read barrier 1, uniform loads scoreboard 2, fixed-latency producers stall 6,
 * ISETP stalls 14 before a predicated branch. The GEMV body is variant 1's depth-0 body with rows = 1, instruction for
 * instruction, except that k, the step count, the row-group count and the scale row stride come from the stage table
 * (row offset rg * k and scale offset srg * kb4 are 32-bit IMADs plus a widening add instead of an IMAD.WIDE immediate:
 * same addresses).
 */
#include "omega_blackwell_codegen_resident.h"
#include "omega_blackwell_codegen_moe.h" /* OMEGA_FP8_BLOCK */
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
    BlackwellIRInsn in; memset(&in, 0, sizeof in);
    in.op = op; in.dst_vreg = NOREG; in.src1_vreg = NOREG; in.src2_vreg = NOREG; in.src3_vreg = NOREG; in.ureg = NOREG;
    return in;
}
static int V(Em *e) { int v = omega_bw_ir_alloc_vreg(e->p); if (v < 0) e->err = -1; return v; }
static int V64(Em *e) { int v = omega_bw_ir_alloc_vreg64(e->p); if (v < 0) e->err = -1; return v; }
static int V128(Em *e) { int v = omega_bw_ir_alloc_vreg128(e->p); if (v < 0) e->err = -1; return v; }
static int UV64(Em *e) { int v = omega_bw_ir_alloc_uvreg64(e->p); if (v < 0) e->err = -1; return v; }
static int here(const Em *e) { return (int)e->p->count; }

static void ldcu64(Em *e, int ud, uint32_t off) { BlackwellIRInsn i = I0(BW_IR_LDCU64); i.dst_vreg = ud; i.imm = off; i.is_uniform = true; emit(e, i, K_UVAR); }
static void ldc64(Em *e, int d, uint32_t off) { BlackwellIRInsn i = I0(BW_IR_LDC64); i.dst_vreg = d; i.imm = off; emit(e, i, K_VAR); }
static void ldc32(Em *e, int d, uint32_t off) { BlackwellIRInsn i = I0(BW_IR_LDC); i.dst_vreg = d; i.imm = off; emit(e, i, K_VAR); }
static void s2r(Em *e, int d, uint32_t sr) { BlackwellIRInsn i = I0(BW_IR_S2R); i.dst_vreg = d; i.imm = sr; emit(e, i, K_VAR); }
static void movi(Em *e, int d, uint32_t imm) { BlackwellIRInsn i = I0(BW_IR_MOV_IMM); i.dst_vreg = d; i.imm = imm; emit(e, i, K_FIXED); }
static void movrz(Em *e, int d) { BlackwellIRInsn i = I0(BW_IR_MOV_RZ); i.dst_vreg = d; emit(e, i, K_FIXED); }
static void imadi(Em *e, int d, int a, unsigned asub, uint32_t imm, int c) { BlackwellIRInsn i = I0(BW_IR_IMAD); i.dst_vreg = d; i.src1_vreg = a; i.src1_subreg = (uint8_t)asub; i.imm = imm; i.src3_vreg = c; emit(e, i, K_FIXED); }
static void imadr(Em *e, int d, int a, int b, int c) { BlackwellIRInsn i = I0(BW_IR_IMAD); i.dst_vreg = d; i.src1_vreg = a; i.src2_vreg = b; i.src3_vreg = c; emit(e, i, K_FIXED); }
static void imadw(Em *e, int d64, int a, uint32_t imm, int c64) { BlackwellIRInsn i = I0(BW_IR_IMAD_WIDE); i.dst_vreg = d64; i.src1_vreg = a; i.imm = imm; i.src3_vreg = c64; emit(e, i, K_FIXED); }
static void shr(Em *e, int d, int a, unsigned asub, uint32_t imm) { BlackwellIRInsn i = I0(BW_IR_SHF_R); i.dst_vreg = d; i.src1_vreg = a; i.src1_subreg = (uint8_t)asub; i.imm = imm; emit(e, i, K_FIXED); }
static void andi(Em *e, int d, int a, unsigned asub, uint32_t imm) { BlackwellIRInsn i = I0(BW_IR_LOP3_AND); i.dst_vreg = d; i.src1_vreg = a; i.src1_subreg = (uint8_t)asub; i.imm = imm; emit(e, i, K_FIXED); }
static void ldg_wide(Em *e, BlackwellIROpcode op, int d, int addr64, uint32_t imm, int udesc, int first) {
    BlackwellIRInsn i = I0(op); i.dst_vreg = d; i.src1_vreg = addr64; i.imm = imm; i.ureg = udesc; emit(e, i, first ? K_VAR : K_VAR_MORE);
}
static void ldg32(Em *e, int d, int addr64, int udesc, int first) { BlackwellIRInsn i = I0(BW_IR_LDG_E); i.dst_vreg = d; i.src1_vreg = addr64; i.ureg = udesc; emit(e, i, first ? K_VAR : K_VAR_MORE); }
static void ldg_sys(Em *e, int d, int addr64, int udesc) { BlackwellIRInsn i = I0(BW_IR_LDG_STRONG_SYS); i.dst_vreg = d; i.src1_vreg = addr64; i.ureg = udesc; emit(e, i, K_VAR); }
static void atom_add(Em *e, int d, int addr64, int val, int udesc) { BlackwellIRInsn i = I0(BW_IR_ATOMG_ADD_STRONG_SYS); i.dst_vreg = d; i.src1_vreg = addr64; i.src2_vreg = val; i.ureg = udesc; emit(e, i, K_VAR); }
static void membar(Em *e) { emit(e, I0(BW_IR_MEMBAR_ALL_SYS), K_FIXED); }
static void stg(Em *e, int addr64, uint32_t imm, int val, int udesc) { BlackwellIRInsn i = I0(BW_IR_STG_E); i.src1_vreg = addr64; i.src2_vreg = val; i.imm = imm; i.ureg = udesc; emit(e, i, K_STORE); }
static void lds32(Em *e, int d, int addr) { BlackwellIRInsn i = I0(BW_IR_LDS32); i.dst_vreg = d; i.src1_vreg = addr; emit(e, i, K_VAR); }
static void sts32(Em *e, int addr, int val) { BlackwellIRInsn i = I0(BW_IR_STS32); i.src1_vreg = addr; i.src2_vreg = val; emit(e, i, K_STORE); }
static void bar_sync(Em *e) { emit(e, I0(BW_IR_BAR_SYNC), K_SYNC); }
static void fadd(Em *e, int d, int a, int b) { BlackwellIRInsn i = I0(BW_IR_FADD); i.dst_vreg = d; i.src1_vreg = a; i.src2_vreg = b; emit(e, i, K_FIXED); }
static void ffma(Em *e, int d, int a, int b, unsigned bsub, int c) { BlackwellIRInsn i = I0(BW_IR_FFMA); i.dst_vreg = d; i.src1_vreg = a; i.src2_vreg = b; i.src2_subreg = (uint8_t)bsub; i.src3_vreg = c; emit(e, i, K_FIXED); }
static void shfl_down(Em *e, int d, int a, uint32_t off) { BlackwellIRInsn i = I0(BW_IR_SHFL_DOWN); i.dst_vreg = d; i.src1_vreg = a; i.imm = off; emit(e, i, K_VAR); }
static void isetp_ge_u32(Em *e, int a, int b) { BlackwellIRInsn i = I0(BW_IR_ISETP_GE_U32); i.src1_vreg = a; i.src2_vreg = b; emit(e, i, K_PRED); }
static void bra_if_p0(Em *e, int target) { BlackwellIRInsn i = I0(BW_IR_BRA); i.predicate_p0 = true; i.imm = (uint32_t)(target - here(e)); emit(e, i, K_BRA); }
static void bra(Em *e, int target) { BlackwellIRInsn i = I0(BW_IR_BRA); i.imm = (uint32_t)(target - here(e)); emit(e, i, K_BRA); }
static int bra_if_p0_fwd(Em *e) { int at = here(e); BlackwellIRInsn i = I0(BW_IR_BRA); i.predicate_p0 = true; i.imm = 1; emit(e, i, K_BRA); return at; }
static int bra_fwd(Em *e) { int at = here(e); BlackwellIRInsn i = I0(BW_IR_BRA); i.imm = 1; emit(e, i, K_BRA); return at; }
static void patch_bra(Em *e, int at, int target) { if (!e->err) e->p->insns[at].imm = (uint32_t)(target - at); }
static int bssy_fwd(Em *e) { int at = here(e); BlackwellIRInsn i = I0(BW_IR_BSSY); i.bar_reg = 0; i.imm = 2; emit(e, i, K_BSSY); return at; }
static void bsync(Em *e) { BlackwellIRInsn i = I0(BW_IR_BSYNC); i.bar_reg = 0; emit(e, i, K_BSYNC); }
static void tail(Em *e) {
    emit(e, I0(BW_IR_EXIT), K_EXIT);
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
static int log2u(uint32_t v) { int l = 0; while ((1u << l) < v) l++; return (1u << l) == v ? l : -1; }

static int spec_ok(const OmegaResidentSpec *s) {
    if (!s) return 0;
    if (s->lane_bytes != 8 && s->lane_bytes != 16) return 0;
    if (s->rows != 1) return 0; /* rows > 1 would need the runtime row offset r * k as an LDG immediate */
    if (s->warps < 2 || s->warps > 32) return 0;
    if (s->mutant < 0 || s->mutant > 1) return 0;
    return 1;
}

/* e4m3 byte i of word w -> f32 bits (value * 2^-120): the variant 1 decode, verbatim */
static void decode_byte(Em *e, int f, int wq, unsigned word, unsigned i, int u, int a, int s) {
    const uint32_t M = 0x07F00000u;
    if (i < 3) imadi(e, u, wq, word, 1u << (20u - 8u * i), NOREG);
    else shr(e, u, wq, word, 4);
    andi(e, a, u, 0, M);
    andi(e, s, wq, word, 0x80u << (8u * i));
    if (i < 3) imadi(e, f, s, 0, 1u << (24u - 8u * i), a);
    else imadi(e, f, s, 0, 1u, a);
}

int omega_blackwell_codegen_resident_prog(const OmegaResidentSpec *spec, BlackwellIRProgram *prog) {
    if (!spec_ok(spec) || !prog) return -1;
    const uint32_t LB = spec->lane_bytes, W = spec->warps;
    const uint32_t BPS = 32u * LB / OMEGA_FP8_BLOCK, LPB = OMEGA_FP8_BLOCK / LB, WORDS = LB / 4u;
    const int log2lpb = log2u(LPB), log2rpb = log2u(OMEGA_FP8_BLOCK); /* rows = 1 */
    const BlackwellIROpcode ldw = LB == 16 ? BW_IR_LDG_E_128 : BW_IR_LDG_E_64;
    if (log2lpb < 0) return -1;
    omega_bw_ir_init(prog);
    Em em; memset(&em, 0, sizeof em); em.p = prog;
    Em *e = &em;

    /* ---- prologue: values live for the whole kernel ---- */
    int udesc = UV64(e), pctl = V64(e), pdesc = V64(e), stride = V(e);
    int tid = V(e), cx = V(e), warp = V(e), lane = V(e), one = V(e);
    int wlane = V(e), slane = V(e), xlo = V(e), rg0 = V(e), st = V(e), target = V(e);
    if (e->err) return -1;
    ldcu64(e, udesc, 0x358);
    ldc64(e, pctl, OMEGA_RES_ARG_CTL); ldc64(e, pdesc, OMEGA_RES_ARG_TABLE);
    ldc32(e, stride, OMEGA_RES_ARG_STRIDE);
    s2r(e, tid, BW_SR_TID_X); s2r(e, cx, BW_SR_CTAID_X);
    movi(e, one, 1);
    shr(e, warp, tid, 0, 5);
    andi(e, lane, tid, 0, 31);
    imadi(e, wlane, lane, 0, LB, NOREG);
    shr(e, slane, lane, 0, (uint32_t)log2lpb); imadi(e, slane, slane, 0, 4, NOREG);
    imadi(e, xlo, lane, 0, LB * 4u, NOREG);
    imadi(e, rg0, cx, 0, W, warp);
    movrz(e, st); movrz(e, target);

    /* per-stage values */
    int wv = V64(e), sv = V64(e), xv = V64(e), yv = V64(e), kks = V64(e), rgkb = V64(e);
    int kk = V(e), ks = V(e), rgn = V(e), kb4 = V(e), xlane = V64(e), rg = V(e), off = V(e), srg = V(e);
    int pw = V64(e), ps = V64(e), pxc = V64(e), pyr = V64(e), acc = V(e), cnt = V(e), wq = LB == 16 ? V128(e) : V64(e);
    int xq[4], sc = V(e), dot = V(e), u = V(e), a = V(e), sg = V(e), f = V(e), t = V(e);
    for (uint32_t q = 0; q < WORDS; q++) xq[q] = V128(e);
    if (e->err) return -1;

    /* ---- stage loop ---- */
    const int L_st = here(e);
    loop_begin(e);
    ldg_wide(e, BW_IR_LDG_E_64, wv, pdesc, 0, udesc, 1);
    ldg_wide(e, BW_IR_LDG_E_64, sv, pdesc, 8, udesc, 0);
    ldg_wide(e, BW_IR_LDG_E_64, xv, pdesc, 16, udesc, 0);
    ldg_wide(e, BW_IR_LDG_E_64, yv, pdesc, 24, udesc, 0);
    ldg_wide(e, BW_IR_LDG_E_64, kks, pdesc, 32, udesc, 0);
    ldg_wide(e, BW_IR_LDG_E_64, rgkb, pdesc, 40, udesc, 0);
    imadi(e, kk, kks, 0, 1, NOREG);
    imadi(e, rgn, rgkb, 0, 1, NOREG);
    imadi(e, kb4, rgkb, 1, 1, NOREG);
    imadi(e, ks, kks, 1, 1, NOREG);
    imadw(e, xlane, xlo, 1, xv);
    imadi(e, rg, rg0, 0, 1, NOREG);

    /* ---- row-group loop (variant 1, depth 0, rows 1) ---- */
    const int L_rg = here(e);
    loop_begin(e);
    isetp_ge_u32(e, rg, rgn);
    const int bra_rg_end = bra_if_p0_fwd(e);
    imadr(e, off, rg, kk, NOREG); imadw(e, pw, off, 1, wv); imadw(e, pw, wlane, 1, pw);
    shr(e, srg, rg, 0, (uint32_t)log2rpb);
    imadr(e, off, srg, kb4, NOREG); imadw(e, ps, off, 1, sv); imadw(e, ps, slane, 1, ps);
    imadw(e, pxc, one, 0, xlane);
    movrz(e, acc);
    imadi(e, cnt, ks, 0, 1, NOREG);

    const int L_k = here(e);
    loop_begin(e);
    {
        int first = 1;
        for (uint32_t q = 0; q < WORDS; q++) { ldg_wide(e, BW_IR_LDG_E_128, xq[q], pxc, q * 16u, udesc, first); first = 0; }
        ldg_wide(e, ldw, wq, pw, 0, udesc, 0);
        ldg32(e, sc, ps, udesc, 0);
    }
    movrz(e, dot);
    for (uint32_t word = 0; word < WORDS; word++)
        for (unsigned i = 0; i < 4; i++) { decode_byte(e, f, wq, word, i, u, a, sg); ffma(e, dot, f, xq[word], i, dot); }
    ffma(e, acc, dot, sc, 0, acc);
    imadw(e, pw, one, 32u * LB, pw);
    imadw(e, pxc, one, 32u * LB * 4u, pxc);
    imadw(e, ps, one, BPS * 4u, ps);
    imadi(e, cnt, one, 0, 0xffffffffu, cnt);
    isetp_ge_u32(e, cnt, one);
    bra_if_p0(e, L_k);
    loop_end(e);

    for (uint32_t o = 16; o >= 1; o /= 2) { shfl_down(e, t, acc, o); fadd(e, acc, acc, t); }
    imadw(e, pyr, rg, 4u, yv);
    isetp_ge_u32(e, lane, one);
    {
        const int at_bssy = bssy_fwd(e);
        const int bra_skip = bra_if_p0_fwd(e);
        stg(e, pyr, 0, acc, udesc);
        const int at_bsync = here(e);
        bsync(e);
        patch_bra(e, bra_skip, at_bsync);
        patch_bra(e, at_bssy, at_bsync + 1);
    }
    imadi(e, rg, stride, 0, 1, rg);
    bra(e, L_rg);
    loop_end(e);
    patch_bra(e, bra_rg_end, here(e));

    /* ---- next stage? ---- */
    imadi(e, st, one, 0, 1, st);
    { int nst = V(e); ldc32(e, nst, OMEGA_RES_ARG_NSTAGES); isetp_ge_u32(e, st, nst); }
    const int bra_done = bra_if_p0_fwd(e);
    imadw(e, pdesc, one, OMEGA_RES_DESC_BYTES, pdesc);

    if (spec->mutant != 1) {
        /* ---- grid barrier ---- */
        int v = V(e), old = V(e), spins = V(e), verdict = V(e), smaddr = V(e), w0 = V(e), lim = V(e), pa = V64(e);
        if (e->err) return -1;
        membar(e);
        ldc32(e, lim, OMEGA_RES_ARG_GRID);
        imadi(e, target, lim, 0, 1, target);
        bar_sync(e);
        s2r(e, w0, BW_SR_TID_X); shr(e, w0, w0, 0, 5);
        isetp_ge_u32(e, w0, one);
        const int bra_not_w0 = bra_if_p0_fwd(e); /* warp-uniform: warps 1.. skip */
        movi(e, verdict, 1);
        imadw(e, pa, one, OMEGA_RES_CTL_ERROR, pctl);
        ldg_sys(e, v, pa, udesc);
        isetp_ge_u32(e, v, one);
        const int bra_abort1 = bra_if_p0_fwd(e);
        imadw(e, pa, one, OMEGA_RES_CTL_CANCEL, pctl);
        ldg_sys(e, v, pa, udesc);
        isetp_ge_u32(e, v, one);
        const int bra_abort2 = bra_if_p0_fwd(e);
        imadw(e, pa, one, OMEGA_RES_CTL_ARRIVE, pctl);
        isetp_ge_u32(e, lane, one);
        {
            const int at_bssy = bssy_fwd(e);
            const int bra_skip = bra_if_p0_fwd(e);
            atom_add(e, old, pa, one, udesc);
            const int at_bsync = here(e);
            bsync(e);
            patch_bra(e, bra_skip, at_bsync);
            patch_bra(e, at_bssy, at_bsync + 1);
        }
        movrz(e, spins);
        ldc32(e, lim, OMEGA_RES_ARG_SPINMAX);
        const int L_sp = here(e);
        loop_begin(e);
        ldg_sys(e, v, pa, udesc);
        isetp_ge_u32(e, v, target);
        const int bra_arrived = bra_if_p0_fwd(e);
        imadi(e, spins, one, 0, 1, spins);
        isetp_ge_u32(e, spins, lim);
        const int bra_tmo = bra_if_p0_fwd(e);
        bra(e, L_sp);
        loop_end(e);
        patch_bra(e, bra_tmo, here(e));
        imadw(e, pa, one, OMEGA_RES_CTL_ERROR, pctl);
        atom_add(e, old, pa, one, udesc); /* every lane of warp 0: error word counts 32 per timed-out CTA (nonzero is what matters) */
        const int bra_to_abort = bra_fwd(e);
        patch_bra(e, bra_arrived, here(e));
        movrz(e, verdict);
        const int at_abort = here(e);
        patch_bra(e, bra_abort1, at_abort);
        patch_bra(e, bra_abort2, at_abort);
        patch_bra(e, bra_to_abort, at_abort);
        movrz(e, smaddr);
        sts32(e, smaddr, verdict);
        patch_bra(e, bra_not_w0, here(e));
        bar_sync(e);
        movrz(e, smaddr);
        lds32(e, v, smaddr);
        isetp_ge_u32(e, v, one);
        const int bra_exit = bra_if_p0_fwd(e);
        emit(e, I0(BW_IR_CCTL_IVALL), K_FIXED);
        bra(e, L_st);
        loop_end(e);
        const int L_end = here(e);
        patch_bra(e, bra_exit, L_end);
        patch_bra(e, bra_done, L_end);
    } else {
        bra(e, L_st);
        loop_end(e);
        patch_bra(e, bra_done, here(e));
    }
    tail(e);
    if (e->err) return -1;
    return omega_bw_regalloc_solve(prog);
}

int omega_resident_prog_stats(const OmegaResidentSpec *spec, uint32_t *insns, uint32_t *gprs) {
    static BlackwellIRProgram prog;
    if (omega_blackwell_codegen_resident_prog(spec, &prog) != 0) return -1;
    if (insns) *insns = (uint32_t)prog.count;
    if (gprs) *gprs = prog.regalloc.peak_gpr_usage;
    return 0;
}
