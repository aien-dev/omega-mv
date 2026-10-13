/* omega_blackwell_codegen_fp4.c: FP4 (e2m1 + block-16 e4m3 scale + tensor scale) decode GEMV kernel (OWN-30B cut L3).
 * See the header for the contract and thread mapping. Scheduling follows omega_blackwell_codegen_moe.c (the FP8 kernel):
 * every instruction waits on everything still pending, variable-latency results go through scoreboard 0, stores and
 * loads set the read barrier 1, fixed-latency producers stall 6, ISETP stalls 14 before a predicated branch. */
#include "omega_blackwell_codegen_fp4.h"
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
/* d = a[asub] * imm + c (c NOREG = RZ) */
static void imadi(Em *e, int d, int a, unsigned asub, uint32_t imm, int c) { BlackwellIRInsn i = I0(BW_IR_IMAD); i.dst_vreg = d; i.src1_vreg = a; i.src1_subreg = (uint8_t)asub; i.imm = imm; i.src3_vreg = c; emit(e, i, K_FIXED); }
/* d = a[asub] * b[bsub] + c */
static void imadr(Em *e, int d, int a, unsigned asub, int b, unsigned bsub, int c) { BlackwellIRInsn i = I0(BW_IR_IMAD); i.dst_vreg = d; i.src1_vreg = a; i.src1_subreg = (uint8_t)asub; i.src2_vreg = b; i.src2_subreg = (uint8_t)bsub; i.src3_vreg = c; emit(e, i, K_FIXED); }
/* d64 = a * imm + c64 */
static void imadw(Em *e, int d64, int a, uint32_t imm, int c64) { BlackwellIRInsn i = I0(BW_IR_IMAD_WIDE); i.dst_vreg = d64; i.src1_vreg = a; i.imm = imm; i.src3_vreg = c64; emit(e, i, K_FIXED); }
static void shr(Em *e, int d, int a, unsigned asub, uint32_t imm) { BlackwellIRInsn i = I0(BW_IR_SHF_R); i.dst_vreg = d; i.src1_vreg = a; i.src1_subreg = (uint8_t)asub; i.imm = imm; emit(e, i, K_FIXED); }
static void shl_r(Em *e, int d, int a, int n) { BlackwellIRInsn i = I0(BW_IR_SHF_L_U32); i.dst_vreg = d; i.src1_vreg = a; i.src2_vreg = n; emit(e, i, K_FIXED); }
static void andi(Em *e, int d, int a, unsigned asub, uint32_t imm) { BlackwellIRInsn i = I0(BW_IR_LOP3_AND); i.dst_vreg = d; i.src1_vreg = a; i.src1_subreg = (uint8_t)asub; i.imm = imm; emit(e, i, K_FIXED); }
static void xorr(Em *e, int d, int a, int b) { BlackwellIRInsn i = I0(BW_IR_LOP3_XOR); i.dst_vreg = d; i.src1_vreg = a; i.src2_vreg = b; emit(e, i, K_FIXED); }
static void ldg_wide(Em *e, BlackwellIROpcode op, int d, int addr64, uint32_t imm, int udesc, int first) {
    BlackwellIRInsn i = I0(op); i.dst_vreg = d; i.src1_vreg = addr64; i.imm = imm; i.ureg = udesc; emit(e, i, first ? K_VAR : K_VAR_MORE);
}
static void ldg32(Em *e, int d, int addr64, int udesc, int first) { BlackwellIRInsn i = I0(BW_IR_LDG_E); i.dst_vreg = d; i.src1_vreg = addr64; i.ureg = udesc; emit(e, i, first ? K_VAR : K_VAR_MORE); }
static void stg(Em *e, int addr64, uint32_t imm, int val, int udesc) { BlackwellIRInsn i = I0(BW_IR_STG_E); i.src1_vreg = addr64; i.src2_vreg = val; i.imm = imm; i.ureg = udesc; emit(e, i, K_STORE); }
static void lds128(Em *e, int d128, int addr, uint32_t imm, int first) { BlackwellIRInsn i = I0(BW_IR_LDS128); i.dst_vreg = d128; i.src1_vreg = addr; i.imm = imm; emit(e, i, first ? K_VAR : K_VAR_MORE); }
static void sts32(Em *e, int addr, int val) { BlackwellIRInsn i = I0(BW_IR_STS32); i.src1_vreg = addr; i.src2_vreg = val; emit(e, i, K_STORE); }
static void bar_sync(Em *e) { emit(e, I0(BW_IR_BAR_SYNC), K_SYNC); }
static void fadd(Em *e, int d, int a, int b) { BlackwellIRInsn i = I0(BW_IR_FADD); i.dst_vreg = d; i.src1_vreg = a; i.src2_vreg = b; emit(e, i, K_FIXED); }
static void fmul(Em *e, int d, int a, int b) { BlackwellIRInsn i = I0(BW_IR_FMUL); i.dst_vreg = d; i.src1_vreg = a; i.src2_vreg = b; emit(e, i, K_FIXED); }
/* d = a * b[bsub] + c */
static void ffma(Em *e, int d, int a, int b, unsigned bsub, int c) { BlackwellIRInsn i = I0(BW_IR_FFMA); i.dst_vreg = d; i.src1_vreg = a; i.src2_vreg = b; i.src2_subreg = (uint8_t)bsub; i.src3_vreg = c; emit(e, i, K_FIXED); }
static void shfl_down(Em *e, int d, int a, uint32_t off) { BlackwellIRInsn i = I0(BW_IR_SHFL_DOWN); i.dst_vreg = d; i.src1_vreg = a; i.imm = off; emit(e, i, K_VAR); }
static void isetp_ge_u32(Em *e, int a, int b) { BlackwellIRInsn i = I0(BW_IR_ISETP_GE_U32); i.src1_vreg = a; i.src2_vreg = b; emit(e, i, K_PRED); }
static void bra(Em *e, int target) { BlackwellIRInsn i = I0(BW_IR_BRA); i.imm = (uint32_t)(target - here(e)); emit(e, i, K_BRA); }
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

static int spec_ok(const OmegaFp4GemvSpec *s) {
    if (!s) return 0;
    if (s->lpa != 16 && s->lpa != 32) return 0;
    if (s->rows != 1 && s->rows != 2 && s->rows != 4) return 0;
    if (s->warps < 1 || s->warps > 32) return 0;
    if (s->k == 0 || s->k > OMEGA_FP4_MAX_K || s->k % (OMEGA_FP4_BLOCK * s->lpa) || s->k % (32u * s->warps)) return 0;
    if ((s->k / (32u * s->warps)) > 16u) return 0; /* staging rounds */
    if (s->k / (OMEGA_FP4_BLOCK * s->lpa) > 8u) return 0;
    if (s->mutant < 0 || s->mutant > 4) return 0;
    /* immediates: weight and scale offsets of a step fit the 24-bit signed LDG immediate */
    if ((uint64_t)s->rows * (s->k / 2u) >= (1u << 23)) return 0;
    return 1;
}

uint32_t omega_fp4_gemv_steps(const OmegaFp4GemvSpec *s) { return spec_ok(s) ? s->k / (OMEGA_FP4_BLOCK * s->lpa) : 0; }
uint32_t omega_fp4_gemv_rows_per_warp(const OmegaFp4GemvSpec *s) { return spec_ok(s) ? (32u / s->lpa) * s->rows : 0; }
uint32_t omega_fp4_gemv_shared_bytes(const OmegaFp4GemvSpec *s) { return spec_ok(s) ? s->k * 4u : 0u; }

/* One nibble of a u32 word (word register `w`, sub-register `wsub`) to the f32 bits value * 2^-126.
 * lo = 1: nibbles 0..5 straight from the word; the caller passes t16 = w >> 16 for nibbles 6 and 7. */
static void decode_nibble(Em *e, int f, int w, unsigned wsub, int t16, unsigned n, int u) {
    int src = w; unsigned sub = wsub; unsigned p = 4u * n;
    if (n >= 6u) { src = t16; sub = 0; p = 4u * (n - 4u); }
    andi(e, u, src, sub, 0xFu << p);
    imadi(e, u, u, 0, (1u << (22u - p)) + (1u << (28u - p)), NOREG);
    andi(e, f, u, 0, 0x81C00000u);
}

int omega_blackwell_codegen_fp4_gemv_prog(const OmegaFp4GemvSpec *spec, BlackwellIRProgram *prog) {
    if (!spec_ok(spec) || !prog) return -1;
    const uint32_t K = spec->k, LPA = spec->lpa, R = spec->rows, W = spec->warps;
    const uint32_t S = K / (OMEGA_FP4_BLOCK * LPA), SL = 32u / LPA, RPW = SL * R, RB = K / 2u, SB = K / 16u;
    const int log2lpa = log2u(LPA);
    if (log2lpa < 0) return -1;
    omega_bw_ir_init(prog);
    Em em; memset(&em, 0, sizeof em); em.p = prog;
    Em *e = &em;

    /* ---- prologue ---- */
    int udesc = UV64(e), ptab = V64(e), px = V64(e), py = V64(e);
    int rgn = V(e), xs = V(e), ys = V(e), stride = V(e);
    int tid = V(e), cx = V(e), cy = V(e), warp = V(e), lane = V(e), one = V(e), three = V(e), gidx = V(e), c62 = V(e);
    int tent = V64(e), wbase = V64(e), sbase = V64(e), tsc = V64(e), off = V(e), xrow = V64(e), yrow = V64(e);
    int lr = V(e), qv = V(e), lanew = V(e), lanes = V(e), lcs = V(e), x3 = V(e), sh = V(e), msel = V(e), nmsel = V(e), laney = V(e), xl = V(e), rg = V(e), tq = V(e);
    if (e->err) return -1;
    ldcu64(e, udesc, 0x358);
    ldc64(e, ptab, OMEGA_FP4_ARG_TAB); ldc64(e, px, OMEGA_FP4_ARG_X); ldc64(e, py, OMEGA_FP4_ARG_Y);
    ldc32(e, rgn, OMEGA_FP4_ARG_RG); ldc32(e, xs, OMEGA_FP4_ARG_XSTRIDE); ldc32(e, ys, OMEGA_FP4_ARG_YSTRIDE); ldc32(e, stride, OMEGA_FP4_ARG_STRIDE);
    s2r(e, tid, BW_SR_TID_X); s2r(e, cx, BW_SR_CTAID_X); s2r(e, cy, BW_SR_CTAID_Y);
    movi(e, one, 1); movi(e, three, 3); movi(e, c62, 0x5E800000u /* 2^62 */);
    shr(e, warp, tid, 0, 5);
    andi(e, lane, tid, 0, 31);
    if (spec->mutant == 4) xorr(e, gidx, cy, one); else imadi(e, gidx, cy, 0, 1, NOREG);
    imadw(e, tent, gidx, OMEGA_FP4_TAB_STRIDE, ptab);
    ldg_wide(e, BW_IR_LDG_E_64, wbase, tent, 0, udesc, 1);
    ldg_wide(e, BW_IR_LDG_E_64, sbase, tent, 8, udesc, 0);
    ldg_wide(e, BW_IR_LDG_E_64, tsc, tent, 16, udesc, 0); /* sub 0: st'', sub 1: pad */
    imadr(e, off, cy, 0, xs, 0, NOREG); imadw(e, xrow, off, 1, px);
    imadr(e, off, cy, 0, ys, 0, NOREG); imadw(e, yrow, off, 1, py);
    andi(e, lr, lane, 0, LPA - 1u);
    shr(e, qv, lane, 0, (uint32_t)log2lpa);                                 /* slot: 0, or 0/1 with 16 lanes per row */
    imadi(e, lanew, qv, 0, R * RB, NOREG); imadi(e, lanew, lr, 0, 8, lanew); /* byte offset of this lane's block-0 codes */
    andi(e, tq, lr, 0, 0xFFFFFFF8u); imadi(e, lanes, qv, 0, R * SB, tq);     /* byte offset of the lane's 8-scale-byte window */
    andi(e, lcs, lr, 0, spec->mutant == 3 ? 2u : 3u);                         /* byte within the word (mutant 3: even bytes only) */
    xorr(e, x3, lcs, three); imadi(e, sh, x3, 0, 8, NOREG);                   /* shift that brings the byte to bits 24..31 */
    shr(e, msel, lr, 0, 2); andi(e, msel, msel, 0, 1);                         /* which word of the 8 bytes */
    xorr(e, nmsel, msel, one);
    imadi(e, laney, qv, 0, R * 4u, NOREG);
    imadi(e, xl, lr, 0, 64, NOREG);                                           /* shared byte offset of the lane's block 0 */
    imadi(e, rg, cx, 0, W, warp);                                             /* first row group of this warp */

    /* ---- stage this group's x' row in shared memory (natural order, 4 k bytes) ---- */
    {
        const uint32_t rounds = K / (32u * W);
        int pxt = V64(e), sa0 = V(e), sa = V(e), pxs[16], xst[16];
        for (uint32_t j = 0; j < rounds; j++) { pxs[j] = V64(e); xst[j] = V(e); }
        if (e->err) return -1;
        imadw(e, pxt, tid, 4, xrow);
        imadi(e, sa0, tid, 0, 4, NOREG);
        for (uint32_t j = 0; j < rounds; j++) imadw(e, pxs[j], one, j * 128u * W, pxt);
        for (uint32_t j = 0; j < rounds; j++) ldg32(e, xst[j], pxs[j], udesc, j == 0);
        for (uint32_t j = 0; j < rounds; j++) { imadi(e, sa, one, 0, j * 128u * W, sa0); sts32(e, sa, xst[j]); }
        bar_sync(e);
    }

    int pw = V64(e), ps = V64(e), pyr = V64(e), pya = V64(e);
    int acc[4], wq[4][8], spr[4][8], xq[4];
    for (uint32_t r = 0; r < R; r++) { acc[r] = V(e); for (uint32_t j = 0; j < S; j++) { wq[r][j] = V64(e); spr[r][j] = V64(e); } }
    for (int q = 0; q < 4; q++) xq[q] = V128(e);
    int dot = V(e), u = V(e), f = V(e), t16 = V(e), sa_ = V(e), sb = V(e), s1 = V(e), t = V(e), accf = V(e);
    if (e->err) return -1;

    /* ---- row-group loop ---- */
    const int L_rg = here(e);
    loop_begin(e);
    isetp_ge_u32(e, rg, rgn);
    const int bra_end = bra_if_p0_fwd(e);
    imadw(e, pw, rg, RPW * RB, wbase); imadw(e, pw, lanew, 1, pw);
    imadw(e, ps, rg, RPW * SB, sbase); imadw(e, ps, lanes, 1, ps);
    {
        int first = 1;
        for (uint32_t r = 0; r < R; r++) for (uint32_t j = 0; j < S; j++) { ldg_wide(e, BW_IR_LDG_E_64, wq[r][j], pw, r * RB + j * LPA * 8u, udesc, first); first = 0; }
        for (uint32_t r = 0; r < R; r++) for (uint32_t j = 0; j < S; j++) ldg_wide(e, BW_IR_LDG_E_64, spr[r][j], ps, r * SB + j * LPA, udesc, 0);
    }
    for (uint32_t r = 0; r < R; r++) movrz(e, acc[r]);
    for (uint32_t j = 0; j < S; j++) {
        { int first = 1; for (uint32_t q = 0; q < 4; q++) { lds128(e, xq[q], xl, j * LPA * 64u + 16u * q, first); first = 0; } }
        for (uint32_t r = 0; r < R; r++) {
            movrz(e, dot);
            for (uint32_t word = 0; word < 2; word++) {
                shr(e, t16, wq[r][j], word, 16);
                for (unsigned i = 0; i < 8; i++) {
                    unsigned nib = spec->mutant == 2 ? (i ^ 1u) : i;
                    decode_nibble(e, f, wq[r][j], word, t16, nib, u);
                    ffma(e, dot, f, xq[(word * 8 + i) / 4], (word * 8 + i) % 4, dot);
                }
            }
            if (spec->mutant == 1) fadd(e, acc[r], acc[r], dot);
            else {
                /* scale byte of this lane's block: pick the word, bring the byte to the top, to f32 bits (* 2^-120), times st'' */
                imadr(e, t, spr[r][j], 0, nmsel, 0, NOREG);
                imadr(e, sa_, spr[r][j], 1, msel, 0, t);
                shl_r(e, sb, sa_, sh);
                shr(e, sb, sb, 0, 4);
                andi(e, sb, sb, 0, 0x07F00000u);
                fmul(e, s1, sb, tsc);
                ffma(e, acc[r], dot, s1, 0, acc[r]);
            }
        }
    }

    /* ---- reduction inside the LPA lanes of a slot, lane 0 of each slot stores ---- */
    for (uint32_t r = 0; r < R; r++)
        for (uint32_t o = LPA / 2; o >= 1; o /= 2) { shfl_down(e, t, acc[r], o); fadd(e, acc[r], acc[r], t); }
    imadw(e, pyr, rg, RPW * 4u, yrow); imadw(e, pyr, laney, 1, pyr);
    isetp_ge_u32(e, lr, one);
    const int at_bssy = bssy_fwd(e);
    const int bra_skip = bra_if_p0_fwd(e);
    for (uint32_t r = 0; r < R; r++) { fmul(e, accf, acc[r], c62); imadw(e, pya, one, r * 4u, pyr); stg(e, pya, 0, accf, udesc); }
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

int omega_fp4_gemv_prog_stats(const OmegaFp4GemvSpec *spec, uint32_t *insns, uint32_t *gprs) {
    static BlackwellIRProgram prog;
    if (omega_blackwell_codegen_fp4_gemv_prog(spec, &prog) != 0) return -1;
    if (insns) *insns = (uint32_t)prog.count;
    if (gprs) *gprs = prog.regalloc.peak_gpr_usage;
    return 0;
}

#ifndef OMEGA_GPU_NO_POSIX
int omega_blackwell_codegen_fp4_gemv(const OmegaFp4GemvSpec *spec, OmegaBlackwellKernel *kernel) {
    if (!kernel) return -1;
    BlackwellIRProgram *prog = calloc(1, sizeof *prog);
    if (!prog) return -1;
    int rc = omega_blackwell_codegen_fp4_gemv_prog(spec, prog);
    size_t need = rc == 0 ? omega_bw_program_code_bytes(prog) : 0;
    uint8_t *buf = need ? malloc(need) : NULL;
    if (!buf) { free(prog); return -1; }
    rc = omega_blackwell_kernel_encode_into(prog, buf, need, kernel);
    free(prog);
    if (rc != 0) { free(buf); return -1; }
    return 0;
}
#endif
