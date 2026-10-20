/* omega_blackwell_codegen_fx1.c: FP8 e4m3 GEMV over FX1 lossless-coded weights (L274). See the header.
 * Emitter helpers and scheduling rules are copied from omega_blackwell_codegen_moe.c (same chip-learned control
 * words: every instruction waits on everything pending, loads join scoreboard 0 and set the read barrier). */
#include "omega_blackwell_codegen_fx1.h"
#ifndef OMEGA_GPU_NO_POSIX
#include <stdlib.h>
#endif
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
    /* Loads read their address operands late (the read barrier SB_RD): a later instruction must not overwrite an
     * in-flight load's address register. Every load sets SB_RD and the next load of a batch waits on it (cheap:
     * the operand read, not the data). Found on the chip: the deep k=2048 kernel hung when the allocator reused a
     * dead scale address register as the next load's destination (evidence OWN30B-FP8-GEMV-c4006391). */
    case K_VAR:   in.control = CW(4, SB_GPR, SB_RD, wait) | CW_YIELD; e->pending = (1u << SB_GPR) | (1u << SB_RD); break;
    /* a further load of the same group: waits on nothing, joins the counting scoreboard */
    case K_VAR_MORE: in.control = CW(4, SB_GPR, SB_RD, (e->pending & (1u << SB_RD))) | CW_YIELD; e->pending |= (1u << SB_GPR) | (1u << SB_RD); break;
    case K_UVAR:  in.control = CW(4, SB_UGPR, SB_NONE, wait) | CW_YIELD; e->pending = 1u << SB_UGPR; break;
    case K_STORE: in.control = CW(4, SB_NONE, SB_RD, wait) | CW_YIELD; e->pending = 1u << SB_RD; break;
    case K_SYNC:  in.control = CW(6, SB_NONE, SB_NONE, wait); e->pending = 0; break; /* BAR.SYNC after the STS: the attention kernel's chip-proven pattern */
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
/* d = a * imm + c (c NOREG = RZ); a may be a sub-register of a quad */
static void imadi(Em *e, int d, int a, unsigned asub, uint32_t imm, int c) { BlackwellIRInsn i = I0(BW_IR_IMAD); i.dst_vreg = d; i.src1_vreg = a; i.src1_subreg = (uint8_t)asub; i.imm = imm; i.src3_vreg = c; emit(e, i, K_FIXED); }
/* d = a * b + c (registers) */
/* d64 = a * imm + c64 */
static void imadw(Em *e, int d64, int a, uint32_t imm, int c64) { BlackwellIRInsn i = I0(BW_IR_IMAD_WIDE); i.dst_vreg = d64; i.src1_vreg = a; i.imm = imm; i.src3_vreg = c64; emit(e, i, K_FIXED); }
static void shr(Em *e, int d, int a, unsigned asub, uint32_t imm) { BlackwellIRInsn i = I0(BW_IR_SHF_R); i.dst_vreg = d; i.src1_vreg = a; i.src1_subreg = (uint8_t)asub; i.imm = imm; emit(e, i, K_FIXED); }
static void andi(Em *e, int d, int a, unsigned asub, uint32_t imm) { BlackwellIRInsn i = I0(BW_IR_LOP3_AND); i.dst_vreg = d; i.src1_vreg = a; i.src1_subreg = (uint8_t)asub; i.imm = imm; emit(e, i, K_FIXED); }
static void ldg_wide(Em *e, BlackwellIROpcode op, int d, int addr64, uint32_t imm, int udesc, int first) {
    BlackwellIRInsn i = I0(op); i.dst_vreg = d; i.src1_vreg = addr64; i.imm = imm; i.ureg = udesc; emit(e, i, first ? K_VAR : K_VAR_MORE);
}
static void ldg32(Em *e, int d, int addr64, int udesc, int first) { BlackwellIRInsn i = I0(BW_IR_LDG_E); i.dst_vreg = d; i.src1_vreg = addr64; i.ureg = udesc; emit(e, i, first ? K_VAR : K_VAR_MORE); }
static void stg(Em *e, int addr64, uint32_t imm, int val, int udesc) { BlackwellIRInsn i = I0(BW_IR_STG_E); i.src1_vreg = addr64; i.src2_vreg = val; i.imm = imm; i.ureg = udesc; emit(e, i, K_STORE); }
static void sts32(Em *e, int addr, int val) { BlackwellIRInsn i = I0(BW_IR_STS32); i.src1_vreg = addr; i.src2_vreg = val; emit(e, i, K_STORE); }
static void bar_sync(Em *e) { emit(e, I0(BW_IR_BAR_SYNC), K_SYNC); }
static void fadd(Em *e, int d, int a, int b) { BlackwellIRInsn i = I0(BW_IR_FADD); i.dst_vreg = d; i.src1_vreg = a; i.src2_vreg = b; emit(e, i, K_FIXED); }
/* d = a * b[bsub] + c */
static void ffma(Em *e, int d, int a, int b, unsigned bsub, int c) { BlackwellIRInsn i = I0(BW_IR_FFMA); i.dst_vreg = d; i.src1_vreg = a; i.src2_vreg = b; i.src2_subreg = (uint8_t)bsub; i.src3_vreg = c; emit(e, i, K_FIXED); }
static void shfl_down(Em *e, int d, int a, uint32_t off) { BlackwellIRInsn i = I0(BW_IR_SHFL_DOWN); i.dst_vreg = d; i.src1_vreg = a; i.imm = off; emit(e, i, K_VAR); }
static void isetp_ge_u32(Em *e, int a, int b) { BlackwellIRInsn i = I0(BW_IR_ISETP_GE_U32); i.src1_vreg = a; i.src2_vreg = b; emit(e, i, K_PRED); }
static void bra_if_p0(Em *e, int target) { BlackwellIRInsn i = I0(BW_IR_BRA); i.predicate_p0 = true; i.imm = (uint32_t)(target - here(e)); emit(e, i, K_BRA); }
static void bra(Em *e, int target) { BlackwellIRInsn i = I0(BW_IR_BRA); i.imm = (uint32_t)(target - here(e)); emit(e, i, K_BRA); }
/* a forward branch whose target is patched when known: returns the instruction index */
static int bra_if_p0_fwd(Em *e) { int at = here(e); BlackwellIRInsn i = I0(BW_IR_BRA); i.predicate_p0 = true; i.imm = 1; emit(e, i, K_BRA); return at; }
static void patch_bra(Em *e, int at, int target) { if (!e->err) e->p->insns[at].imm = (uint32_t)(target - at); }
static int bssy_fwd(Em *e) { int at = here(e); BlackwellIRInsn i = I0(BW_IR_BSSY); i.bar_reg = 0; i.imm = 2; emit(e, i, K_BSSY); return at; }
static void bsync(Em *e) { BlackwellIRInsn i = I0(BW_IR_BSYNC); i.bar_reg = 0; emit(e, i, K_BSYNC); }
static void tail(Em *e) {
    emit(e, I0(BW_IR_EXIT), K_EXIT);
    BlackwellIRInsn self = I0(BW_IR_BRA);
    self.control = 0x000fc000u;
    if (omega_bw_ir_append(e->p, &self) < 0) e->err = -1;
}

/* Loops: a value defined before the loop and read inside it stays live until the back-branch
 * (the linear-scan allocator knows no back edges; same rule as the elementwise emitter). */
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

static void lds32(Em *e, int d, int addr) { BlackwellIRInsn i = I0(BW_IR_LDS32); i.dst_vreg = d; i.src1_vreg = addr; emit(e, i, K_VAR); }
static void shl_r(Em *e, int d, int a, int n) { BlackwellIRInsn i = I0(BW_IR_SHF_L_U32); i.dst_vreg = d; i.src1_vreg = a; i.src2_vreg = n; emit(e, i, K_FIXED); }
static void imadhi(Em *e, int d, int a, int b) { BlackwellIRInsn i = I0(BW_IR_IMAD_HI_U32); i.dst_vreg = d; i.src1_vreg = a; i.src2_vreg = b; emit(e, i, K_FIXED); }

#define FX_LUT_MIN 256u

uint32_t omega_fx1_lut_words(uint32_t warps) {
    const uint32_t t = 32u * (warps ? warps : 1u);
    return (FX_LUT_MIN + t - 1u) / t * t;
}

static int spec_ok(const OmegaFx1GemvSpec *s) {
    if (!s) return 0;
    if (s->rows != 1 && s->rows != 2) return 0;
    if (s->warps < 1 || s->warps > 32) return 0;
    if (s->k == 0 || s->k > 32768u || s->k % 512u) return 0;
    if (s->mutant < 0 || s->mutant > 2) return 0;
    if ((uint64_t)(s->rows - 1) * (s->k / 2u) + 8u >= (1u << 23)) return 0; /* nibble row offsets fit the LDG immediate */
    return 1;
}

uint32_t omega_fx1_shared_bytes(const OmegaFx1GemvSpec *spec) { return spec_ok(spec) ? omega_fx1_lut_words(spec->warps) * 4u : 0u; }

int omega_blackwell_codegen_fx1_gemv_prog(const OmegaFx1GemvSpec *spec, BlackwellIRProgram *prog) {
    if (!spec_ok(spec) || !prog) return -1;
    const uint32_t K = spec->k, R = spec->rows, W = spec->warps, LB = OMEGA_FX1_LANE_BYTES;
    const uint32_t KS = K / (32u * LB), BPS = 32u * LB / 128u, KB = K / 128u;
    const uint32_t LUTW = omega_fx1_lut_words(W), ROUNDS = LUTW / (32u * W);
    const int log2rows_per_block = R == 1 ? 7 : 6;
    omega_bw_ir_init(prog);
    Em em; memset(&em, 0, sizeof em); em.p = prog;
    Em *e = &em;

    /* ---- prologue ---- */
    int udesc = UV64(e), pnib = V64(e), pexp = V64(e), prow = V64(e), plut = V64(e), psc = V64(e), px = V64(e), py = V64(e);
    int rgn = V(e), stride = V(e), tid = V(e), cx = V(e), warp = V(e), lane = V(e), one = V(e);
    int xlane = V64(e), nlane = V64(e), elane = V64(e), slane = V(e), rg = V(e), t = V(e);
    if (e->err) return -1;
    ldcu64(e, udesc, 0x358);
    ldc64(e, pnib, OMEGA_FX1_ARG_NIB); ldc64(e, pexp, OMEGA_FX1_ARG_EXP); ldc64(e, prow, OMEGA_FX1_ARG_ROWS);
    ldc64(e, plut, OMEGA_FX1_ARG_LUT); ldc64(e, psc, OMEGA_FX1_ARG_S); ldc64(e, px, OMEGA_FX1_ARG_X); ldc64(e, py, OMEGA_FX1_ARG_Y);
    ldc32(e, rgn, OMEGA_FX1_ARG_RG); ldc32(e, stride, OMEGA_FX1_ARG_STRIDE);
    s2r(e, tid, BW_SR_TID_X); s2r(e, cx, BW_SR_CTAID_X);
    movi(e, one, 1);
    shr(e, warp, tid, 0, 5);
    andi(e, lane, tid, 0, 31);

    /* ---- LUT -> shared memory [0, LUTW * 4): ROUNDS rounds of one word per thread, then BAR.SYNC ---- */
    {
        int la[8], lv[8], sa = V(e);
        if (ROUNDS > 8) return -1;
        for (uint32_t j = 0; j < ROUNDS; j++) { la[j] = V64(e); lv[j] = V(e); }
        if (e->err) return -1;
        for (uint32_t j = 0; j < ROUNDS; j++) { imadi(e, t, one, 0, j * 32u * W, tid); imadw(e, la[j], t, 4, plut); }
        for (uint32_t j = 0; j < ROUNDS; j++) ldg32(e, lv[j], la[j], udesc, j == 0);
        for (uint32_t j = 0; j < ROUNDS; j++) { imadi(e, t, one, 0, j * 32u * W, tid); imadi(e, sa, t, 0, 4, NOREG); sts32(e, sa, lv[j]); }
        bar_sync(e);
    }
    imadw(e, xlane, lane, LB * 4u, px);          /* this lane's 16 x' floats of step 0 */
    imadw(e, nlane, lane, LB / 2u, pnib);        /* this lane's 8 nibble bytes of step 0, row 0 of the tensor */
    imadw(e, elane, lane, 4, pexp);              /* this lane's word 0 of a region at offset 0 */
    shr(e, slane, lane, 0, 3); imadi(e, slane, slane, 0, 4, NOREG); /* 128 / 16 = 8 lanes share a scale word */
    imadi(e, rg, cx, 0, W, warp);

    int pn = V64(e), ps = V64(e), pxc = V64(e), pyr = V64(e), pya = V64(e), srg = V(e), cnt = V(e);
    int acc[2], p[2], rb[2], rofs[2], ra[2], nib[2], w0[2], w1[2], o[2], a0[2], a1[2];
    int xq[4], sc = V(e), dot = V(e), win = V(e), pw2 = V(e), hi = V(e), pk = V(e), la = V(e), ent = V(e), len = V(e), eb = V(e);
    int u = V(e), sg = V(e), f = V(e), iw = V(e);
    for (uint32_t r = 0; r < R; r++) {
        acc[r] = V(e); p[r] = V(e); rb[r] = V64(e); rofs[r] = V(e); ra[r] = V64(e); nib[r] = V64(e);
        w0[r] = V(e); w1[r] = V(e); o[r] = V(e); a0[r] = V64(e); a1[r] = V64(e);
    }
    for (uint32_t q = 0; q < 4; q++) xq[q] = V128(e);
    if (e->err) return -1;

    /* ---- row-group loop ---- */
    const int L_rg = here(e);
    loop_begin(e);
    isetp_ge_u32(e, rg, rgn);
    const int bra_end = bra_if_p0_fwd(e);
    imadw(e, pn, rg, R * (K / 2u), nlane);
    for (uint32_t r = 0; r < R; r++) { imadi(e, t, rg, 0, R, NOREG); imadi(e, t, one, 0, r, t); imadw(e, ra[r], t, 4, prow); }
    for (uint32_t r = 0; r < R; r++) ldg32(e, rofs[r], ra[r], udesc, r == 0);
    for (uint32_t r = 0; r < R; r++) { imadw(e, rb[r], rofs[r], 4, elane); movrz(e, p[r]); movrz(e, acc[r]); }
    shr(e, srg, rg, 0, (uint32_t)log2rows_per_block);
    imadw(e, ps, srg, KB * 4u, psc); imadw(e, ps, slane, 1, ps);
    imadw(e, pxc, one, 0, xlane);
    movi(e, cnt, KS);

    /* ---- K loop: one step (16 weights per lane per row) per iteration ---- */
    const int L_k = here(e);
    loop_begin(e);
    for (uint32_t r = 0; r < R; r++) { /* group 0 words: addresses from the step's start position */
        shr(e, iw, p[r], 0, 5); andi(e, o[r], p[r], 0, 31);
        imadw(e, a0[r], iw, 128, rb[r]); imadw(e, a1[r], one, 128, a0[r]);
    }
    {
        int first = 1;
        for (uint32_t q = 0; q < 4; q++) { ldg_wide(e, BW_IR_LDG_E_128, xq[q], pxc, q * 16u, udesc, first); first = 0; }
        for (uint32_t r = 0; r < R; r++) ldg_wide(e, BW_IR_LDG_E_64, nib[r], pn, r * (K / 2u), udesc, 0);
        ldg32(e, sc, ps, udesc, 0);
        for (uint32_t r = 0; r < R; r++) { ldg32(e, w0[r], a0[r], udesc, 0); ldg32(e, w1[r], a1[r], udesc, 0); }
    }
    for (uint32_t r = 0; r < R; r++) {
        movrz(e, dot);
        for (uint32_t g = 0; g < 4; g++) {
            if (g > 0) {
                shr(e, iw, p[r], 0, 5); andi(e, o[r], p[r], 0, 31);
                imadw(e, a0[r], iw, 128, rb[r]); imadw(e, a1[r], one, 128, a0[r]);
                ldg32(e, w0[r], a0[r], udesc, 1); ldg32(e, w1[r], a1[r], udesc, 0);
            }
            shl_r(e, win, w0[r], o[r]);
            shl_r(e, pw2, one, o[r]);
            imadhi(e, hi, w1[r], pw2);
            imadi(e, win, hi, 0, 1, win);              /* window = w0 << o | w1 >> (32 - o): disjoint bits */
            for (uint32_t b = 4 * g; b < 4 * g + 4; b++) {
                shr(e, pk, win, 0, 32u - OMEGA_FX1_LMAX);
                imadi(e, la, pk, 0, 4, NOREG);
                lds32(e, ent, la);
                andi(e, len, ent, 0, 0xffu);
                if (spec->mutant == 1) andi(e, eb, ent, 0, 0u); else andi(e, eb, ent, 0, 0x07800000u);
                shl_r(e, win, win, len);
                if (spec->mutant != 2) imadi(e, p[r], len, 0, 1, p[r]);
                /* sign and mantissa of nibble i of word b / 8 to their f32 places, plus the exponent field */
                const unsigned word = b / 8u, i4 = b % 8u;
                if (i4 <= 5) imadi(e, u, nib[r], word, 1u << (20u - 4u * i4), NOREG);
                else shr(e, u, nib[r], word, 4u * i4 - 20u);
                andi(e, u, u, 0, 0x00700000u);
                andi(e, sg, nib[r], word, 1u << (4u * i4 + 3u));
                imadi(e, f, sg, 0, 1u << (28u - 4u * i4), u);
                imadi(e, f, f, 0, 1, eb);
                ffma(e, dot, f, xq[b / 4u], b % 4u, dot);
            }
        }
        ffma(e, acc[r], dot, sc, 0, acc[r]);
    }
    imadw(e, pn, one, 32u * LB / 2u, pn);
    imadw(e, pxc, one, 32u * LB * 4u, pxc);
    imadw(e, ps, one, BPS * 4u, ps);
    imadi(e, cnt, one, 0, 0xffffffffu, cnt);
    isetp_ge_u32(e, cnt, one);
    bra_if_p0(e, L_k);
    loop_end(e);

    /* ---- warp reduction, lane 0 stores (as the moe kernel) ---- */
    for (uint32_t r = 0; r < R; r++)
        for (uint32_t off = 16; off >= 1; off /= 2) { shfl_down(e, t, acc[r], off); fadd(e, acc[r], acc[r], t); }
    imadw(e, pyr, rg, R * 4u, py);
    isetp_ge_u32(e, lane, one);
    const int at_bssy = bssy_fwd(e);
    const int bra_skip = bra_if_p0_fwd(e);
    for (uint32_t r = 0; r < R; r++) { imadw(e, pya, one, r * 4u, pyr); stg(e, pya, 0, acc[r], udesc); }
    const int at_bsync = here(e);
    bsync(e);
    patch_bra(e, bra_skip, at_bsync);
    patch_bra(e, at_bssy, at_bsync + 1);
    imadi(e, rg, stride, 0, 1, rg);
    bra(e, L_rg);
    loop_end(e);
    const int L_end = here(e);
    patch_bra(e, bra_end, L_end);
    tail(e);
    if (e->err) return -1;
    return omega_bw_regalloc_solve(prog);
}

int omega_fx1_gemv_prog_stats(const OmegaFx1GemvSpec *spec, uint32_t *insns, uint32_t *gprs) {
    static BlackwellIRProgram prog;
    if (omega_blackwell_codegen_fx1_gemv_prog(spec, &prog) != 0) return -1;
    if (insns) *insns = (uint32_t)prog.count;
    if (gprs) *gprs = prog.regalloc.peak_gpr_usage;
    return 0;
}
