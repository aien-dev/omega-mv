/* omega_blackwell_codegen_resident2.c: resident kernel with decode stage kinds (OWN-30B W-L2h). See the header.
 * Emitter helpers and scheduling rules: copied from omega_blackwell_codegen_resident.c / omega_blackwell_codegen_dec.c (static
 * there): every instruction waits on everything still pending, variable-latency results through scoreboard 0, loads set the
 * read barrier 1, uniform loads scoreboard 2, fixed-latency producers stall 6, ISETP stalls 14 before a predicated branch.
 * The GEMV bodies are the W-L2f kernel's (variant 1, depth 0, rows 1); SWIGLU and RESNORM are gen_swiglu and gen_res
 * (COMBINE | NORM) of omega_blackwell_codegen_dec.c, same operations in the same order (operands that came from the constant
 * bank there come from the stage descriptor here: same values). */
#include "omega_blackwell_codegen_resident2.h"
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

typedef struct { int v; unsigned s; } Op;
static Op R(int v) { Op o = { v, 0 }; return o; }
static Op S(int v, unsigned s) { Op o = { v, s }; return o; }

static void ldcu64(Em *e, int ud, uint32_t off) { BlackwellIRInsn i = I0(BW_IR_LDCU64); i.dst_vreg = ud; i.imm = off; i.is_uniform = true; emit(e, i, K_UVAR); }
static void ldc64(Em *e, int d, uint32_t off) { BlackwellIRInsn i = I0(BW_IR_LDC64); i.dst_vreg = d; i.imm = off; emit(e, i, K_VAR); }
static void ldc32(Em *e, int d, uint32_t off) { BlackwellIRInsn i = I0(BW_IR_LDC); i.dst_vreg = d; i.imm = off; emit(e, i, K_VAR); }
static void s2r(Em *e, int d, uint32_t sr) { BlackwellIRInsn i = I0(BW_IR_S2R); i.dst_vreg = d; i.imm = sr; emit(e, i, K_VAR); }
static void movi(Em *e, int d, uint32_t imm) { BlackwellIRInsn i = I0(BW_IR_MOV_IMM); i.dst_vreg = d; i.imm = imm; emit(e, i, K_FIXED); }
static void movrz(Em *e, int d) { BlackwellIRInsn i = I0(BW_IR_MOV_RZ); i.dst_vreg = d; emit(e, i, K_FIXED); }
/* d = a[asub] * imm + c */
static void imadi(Em *e, int d, int a, unsigned asub, uint32_t imm, int c) { BlackwellIRInsn i = I0(BW_IR_IMAD); i.dst_vreg = d; i.src1_vreg = a; i.src1_subreg = (uint8_t)asub; i.imm = imm; i.src3_vreg = c; emit(e, i, K_FIXED); }
/* Op form (dec's imadi): d = a * imm + c */
static void imadio(Em *e, Op d, Op a, uint32_t imm, int c) { BlackwellIRInsn i = I0(BW_IR_IMAD); i.dst_vreg = d.v; i.dst_subreg = (uint8_t)d.s; i.src1_vreg = a.v; i.src1_subreg = (uint8_t)a.s; i.imm = imm; i.src3_vreg = c; emit(e, i, K_FIXED); }
static void imadr(Em *e, int d, int a, int b, int c) { BlackwellIRInsn i = I0(BW_IR_IMAD); i.dst_vreg = d; i.src1_vreg = a; i.src2_vreg = b; i.src3_vreg = c; emit(e, i, K_FIXED); }
static void imadrs(Em *e, int d, int a, unsigned asub, int b, unsigned bsub, int c) { BlackwellIRInsn i = I0(BW_IR_IMAD); i.dst_vreg = d; i.src1_vreg = a; i.src1_subreg = (uint8_t)asub; i.src2_vreg = b; i.src2_subreg = (uint8_t)bsub; i.src3_vreg = c; emit(e, i, K_FIXED); }
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
static void stgo(Em *e, int addr64, Op val, int udesc) { BlackwellIRInsn i = I0(BW_IR_STG_E); i.src1_vreg = addr64; i.src2_vreg = val.v; i.src2_subreg = (uint8_t)val.s; i.ureg = udesc; emit(e, i, K_STORE); }
static void lds32(Em *e, int d, int addr) { BlackwellIRInsn i = I0(BW_IR_LDS32); i.dst_vreg = d; i.src1_vreg = addr; emit(e, i, K_VAR); }
static void lds128(Em *e, int d128, int addr, uint32_t imm, int first) { BlackwellIRInsn i = I0(BW_IR_LDS128); i.dst_vreg = d128; i.src1_vreg = addr; i.imm = imm; emit(e, i, first ? K_VAR : K_VAR_MORE); }
static void sts32(Em *e, int addr, int val) { BlackwellIRInsn i = I0(BW_IR_STS32); i.src1_vreg = addr; i.src2_vreg = val; emit(e, i, K_STORE); }
static void sts32o(Em *e, int addr, Op val) { BlackwellIRInsn i = I0(BW_IR_STS32); i.src1_vreg = addr; i.src2_vreg = val.v; i.src2_subreg = (uint8_t)val.s; emit(e, i, K_STORE); }
static void bar_sync(Em *e) { emit(e, I0(BW_IR_BAR_SYNC), K_SYNC); }
static void fop(Em *e, BlackwellIROpcode op, Op d, Op a, Op b, Op c) {
    BlackwellIRInsn i = I0(op); i.dst_vreg = d.v; i.dst_subreg = (uint8_t)d.s; i.src1_vreg = a.v; i.src1_subreg = (uint8_t)a.s;
    i.src2_vreg = b.v; i.src2_subreg = (uint8_t)b.s; i.src3_vreg = c.v; i.src3_subreg = (uint8_t)c.s; emit(e, i, K_FIXED);
}
static void fadd(Em *e, Op d, Op a, Op b) { fop(e, BW_IR_FADD, d, a, b, R(NOREG)); }
static void fmul(Em *e, Op d, Op a, Op b) { fop(e, BW_IR_FMUL, d, a, b, R(NOREG)); }
static void ffmao(Em *e, Op d, Op a, Op b, Op c) { fop(e, BW_IR_FFMA, d, a, b, c); }
/* the GEMV's form (resident.c): d = a * b[bsub] + c */
static void ffma(Em *e, int d, int a, int b, unsigned bsub, int c) { BlackwellIRInsn i = I0(BW_IR_FFMA); i.dst_vreg = d; i.src1_vreg = a; i.src2_vreg = b; i.src2_subreg = (uint8_t)bsub; i.src3_vreg = c; emit(e, i, K_FIXED); }
static void mufu(Em *e, BlackwellIROpcode op, Op d, Op a) { BlackwellIRInsn i = I0(op); i.dst_vreg = d.v; i.dst_subreg = (uint8_t)d.s; i.src1_vreg = a.v; i.src1_subreg = (uint8_t)a.s; emit(e, i, K_VAR); }
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
typedef struct { int bssy, bra; } Region;
static Region region_skip_if_p0(Em *e) { Region r; r.bssy = bssy_fwd(e); r.bra = bra_if_p0_fwd(e); return r; }
static void region_end(Em *e, Region r) { int at = here(e); bsync(e); patch_bra(e, r.bra, at); patch_bra(e, r.bssy, at + 1); }
static int log2u(uint32_t v) { int l = 0; while ((1u << l) < v) l++; return (1u << l) == v ? l : -1; }

static void mark(OmegaResidentPatches *pt, int at, uint32_t kind) {
    if (!pt || !kind) return;
    if (pt->n < OMEGA_RES_MAX_PATCHES) { pt->idx[pt->n] = (uint32_t)at; pt->kind[pt->n] = kind; }
    pt->n++;
}

/* values every block may use (live for the whole kernel) */
typedef struct { int udesc, pdesc, tid, cx, warp, lane, one, gw; } Ctx;

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

/* GEMV stage (kinds 0, 3, 4). Returns the forward branch to patch to the stage end, or -1. */
static int gemv_block(Em *e, const Ctx *c, uint32_t LB, int add) {
    const uint32_t BPS = 32u * LB / OMEGA_FP8_BLOCK, LPB = OMEGA_FP8_BLOCK / LB, WORDS = LB / 4u;
    const int log2lpb = log2u(LPB), log2rpb = log2u(OMEGA_FP8_BLOCK);
    const BlackwellIROpcode ldw = LB == 16 ? BW_IR_LDG_E_128 : BW_IR_LDG_E_64;
    if (log2lpb < 0) { e->err = -1; return -1; }
    int tb = V64(e), xv = V64(e), yv = V64(e), av = add ? V64(e) : NOREG, kks = V64(e), rgkb = V64(e), kg = V64(e), xw = V64(e);
    int wv = V64(e), sv = V64(e), ent = V64(e), g = V(e), t = V(e), nwpg = V(e), goff = V(e);
    int kk = V(e), ks = V(e), rgn = V(e), kb4 = V(e), wpg = V(e), xlane = V64(e), rg = V(e), off = V(e), srg = V(e);
    int wlane = V(e), slane = V(e), xlo = V(e);
    int pw = V64(e), ps = V64(e), pxc = V64(e), pyr = V64(e), acc = V(e), cnt = V(e), wq = LB == 16 ? V128(e) : V64(e);
    int xq[4] = { NOREG, NOREG, NOREG, NOREG }, sc = V(e), dot = V(e), u = V(e), sg = V(e), tt = V(e);
    for (uint32_t q = 0; q < WORDS; q++) xq[q] = V128(e);
    if (e->err) return -1;
    ldg_wide(e, BW_IR_LDG_E_64, tb, c->pdesc, OMEGA_RES2_D_TAB, c->udesc, 1);
    ldg_wide(e, BW_IR_LDG_E_64, xv, c->pdesc, OMEGA_RES2_D_X, c->udesc, 0);
    ldg_wide(e, BW_IR_LDG_E_64, yv, c->pdesc, OMEGA_RES2_D_Y, c->udesc, 0);
    if (add) ldg_wide(e, BW_IR_LDG_E_64, av, c->pdesc, OMEGA_RES2_D_ADD, c->udesc, 0);
    ldg_wide(e, BW_IR_LDG_E_64, kks, c->pdesc, OMEGA_RES2_D_K, c->udesc, 0);
    ldg_wide(e, BW_IR_LDG_E_64, rgkb, c->pdesc, OMEGA_RES2_D_N, c->udesc, 0);
    ldg_wide(e, BW_IR_LDG_E_64, kg, c->pdesc, OMEGA_RES2_D_KIND, c->udesc, 0);
    ldg_wide(e, BW_IR_LDG_E_64, xw, c->pdesc, OMEGA_RES2_D_XGS, c->udesc, 0);
    imadi(e, kk, kks, 0, 1, NOREG);
    imadi(e, ks, kks, 1, 1, NOREG);
    imadi(e, rgn, rgkb, 0, 1, NOREG);
    imadi(e, kb4, rgkb, 1, 1, NOREG);
    imadi(e, wpg, xw, 1, 1, NOREG);
    /* group of this warp: g = gw / wpg, t = gw mod wpg (subtract loop, at most G + 1 rounds) */
    imadi(e, nwpg, wpg, 0, 0xffffffffu, NOREG); /* -wpg */
    imadi(e, t, c->gw, 0, 1, NOREG);
    movrz(e, g);
    const int L_g = here(e);
    loop_begin(e);
    isetp_ge_u32(e, t, wpg);
    { BlackwellIRInsn i = I0(BW_IR_BRA); i.predicate_p0 = true; i.predicate_not = true; i.imm = 1; emit(e, i, K_BRA); }
    const int bra_g_done = here(e) - 1;
    imadr(e, t, nwpg, c->one, t);
    imadi(e, g, c->one, 0, 1, g);
    bra(e, L_g);
    loop_end(e);
    patch_bra(e, bra_g_done, here(e));
    /* g >= G: this warp has no group (grid * warps not a multiple of G): no rows */
    { int gg = V(e); if (e->err) return -1; imadi(e, gg, kg, 1, 1, NOREG); isetp_ge_u32(e, g, gg); }
    const int bra_nogroup = bra_if_p0_fwd(e);
    imadw(e, ent, g, 16u, tb);
    ldg_wide(e, BW_IR_LDG_E_64, wv, ent, 0, c->udesc, 1);
    ldg_wide(e, BW_IR_LDG_E_64, sv, ent, 8, c->udesc, 0);
    imadrs(e, goff, g, 0, xw, 0, NOREG);           /* g * xgs */
    imadw(e, xv, goff, 1, xv);
    imadr(e, goff, g, rgn, NOREG);                 /* g * n */
    imadw(e, yv, goff, 4u, yv);
    if (add) imadw(e, av, goff, 4u, av);
    imadi(e, wlane, c->lane, 0, LB, NOREG);
    shr(e, slane, c->lane, 0, (uint32_t)log2lpb); imadi(e, slane, slane, 0, 4, NOREG);
    imadi(e, xlo, c->lane, 0, LB * 4u, NOREG);
    imadw(e, xlane, xlo, 1, xv);
    imadi(e, rg, t, 0, 1, NOREG);

    /* ---- row loop (variant 1, depth 0, rows 1), rows t, t + wpg, ... of group g ---- */
    const int L_rg = here(e);
    loop_begin(e);
    isetp_ge_u32(e, rg, rgn);
    const int bra_rg_end = bra_if_p0_fwd(e);
    imadr(e, off, rg, kk, NOREG); imadw(e, pw, off, 1, wv); imadw(e, pw, wlane, 1, pw);
    shr(e, srg, rg, 0, (uint32_t)log2rpb);
    imadr(e, off, srg, kb4, NOREG); imadw(e, ps, off, 1, sv); imadw(e, ps, slane, 1, ps);
    imadw(e, pxc, c->one, 0, xlane);
    movrz(e, acc);
    imadi(e, cnt, ks, 0, 1, NOREG);
    const int L_k = here(e);
    loop_begin(e);
    {
        int first = 1;
        for (uint32_t q = 0; q < WORDS; q++) { ldg_wide(e, BW_IR_LDG_E_128, xq[q], pxc, q * 16u, c->udesc, first); first = 0; }
        ldg_wide(e, ldw, wq, pw, 0, c->udesc, 0);
        ldg32(e, sc, ps, c->udesc, 0);
    }
    movrz(e, dot);
    for (uint32_t word = 0; word < WORDS; word++)
        for (unsigned i = 0; i < 4; i++) { decode_byte(e, sg, wq, word, i, u, u, sg); ffma(e, dot, sg, xq[word], i, dot); } /* W-L2i: a in u, f in sg */
    ffma(e, acc, dot, sc, 0, acc);
    imadw(e, pw, c->one, 32u * LB, pw);
    imadw(e, pxc, c->one, 32u * LB * 4u, pxc);
    imadw(e, ps, c->one, BPS * 4u, ps);
    imadi(e, cnt, c->one, 0, 0xffffffffu, cnt);
    isetp_ge_u32(e, cnt, c->one);
    bra_if_p0(e, L_k);
    loop_end(e);

    for (uint32_t o = 16; o >= 1; o /= 2) { shfl_down(e, tt, acc, o); fadd(e, R(acc), R(acc), R(tt)); }
    imadw(e, pyr, rg, 4u, yv);
    isetp_ge_u32(e, c->lane, c->one);
    {
        const Region r = region_skip_if_p0(e);
        stg(e, pyr, 0, acc, c->udesc);
        if (add) { /* the decode graph's RES ADD: x = x + o (FADD), here row by row */
            int pa = V64(e), xa = V(e);
            if (e->err) return -1;
            imadw(e, pa, rg, 4u, av);
            ldg32(e, xa, pa, c->udesc, 1);
            fadd(e, R(xa), R(xa), R(acc));
            stg(e, pa, 0, xa, c->udesc);
        }
        region_end(e, r);
    }
    imadi(e, rg, wpg, 0, 1, rg);
    bra(e, L_rg);
    loop_end(e);
    patch_bra(e, bra_rg_end, here(e));
    patch_bra(e, bra_nogroup, here(e));
    return bra_fwd(e);
}

static const uint32_t C_INV2048 = 0x3A000000u; /* 1/2048 */
static const uint32_t C_NLOG2E = 0xBFB8AA3Bu;  /* -1.4426950f */
static const uint32_t C_ONE = 0x3F800000u;

/* SWIGLU stage (gen_swiglu): every thread of the grid, elements e = global thread id, + grid threads, ... < G * I */
static int swiglu_block(Em *e, const Ctx *c) {
    int pgu = V64(e), pact = V64(e), kg = V64(e), fi = V64(e), total = V(e), eid = V(e), step = V(e), gi = V(e), t = V(e), ninter = V(e), ui = V(e);
    int ga = V64(e), ua = V64(e), aa = V64(e), gv = V(e), uv = V(e), tf = V(e), cst = V(e);
    if (e->err) return -1;
    ldg_wide(e, BW_IR_LDG_E_64, pgu, c->pdesc, OMEGA_RES2_D_X, c->udesc, 1);
    ldg_wide(e, BW_IR_LDG_E_64, pact, c->pdesc, OMEGA_RES2_D_Y, c->udesc, 0);
    ldg_wide(e, BW_IR_LDG_E_64, kg, c->pdesc, OMEGA_RES2_D_KIND, c->udesc, 0);
    ldg_wide(e, BW_IR_LDG_E_64, fi, c->pdesc, OMEGA_RES2_D_F0, c->udesc, 0); /* scale f32, I u32 */
    imadrs(e, total, kg, 1, fi, 1, NOREG);          /* G * I */
    imadi(e, ninter, fi, 1, 0xffffffffu, NOREG);    /* -I */
    imadi(e, eid, c->gw, 0, 32u, c->lane);          /* (cta * warps + warp) * 32 + lane */
    ldc32(e, step, OMEGA_RES_ARG_STRIDE);            /* W-L2i: from the constant bank here, not held kernel-wide */
    imadi(e, step, step, 0, 32u, NOREG);
    const int L_e = here(e);
    loop_begin(e);
    isetp_ge_u32(e, eid, total);
    const int bra_e_end = bra_if_p0_fwd(e);
    /* gi = g * 2I + i = eid + g * I with i = eid mod I (subtract loop) */
    imadi(e, t, eid, 0, 1, NOREG);
    imadi(e, gi, eid, 0, 1, NOREG);
    {
        int inter = V(e);
        if (e->err) return -1;
        imadi(e, inter, fi, 1, 1, NOREG);
        const int L_g = here(e);
        loop_begin(e);
        isetp_ge_u32(e, t, inter);
        { BlackwellIRInsn i = I0(BW_IR_BRA); i.predicate_p0 = true; i.predicate_not = true; i.imm = 1; emit(e, i, K_BRA); }
        const int bra_g_done = here(e) - 1;
        imadr(e, t, ninter, c->one, t);
        imadi(e, gi, inter, 0, 1, gi);
        bra(e, L_g);
        loop_end(e);
        patch_bra(e, bra_g_done, here(e));
        imadi(e, ui, inter, 0, 1, gi);              /* up = gate + I */
    }
    imadw(e, ga, gi, 4u, pgu);
    imadw(e, ua, ui, 4u, pgu);
    imadw(e, aa, eid, 4u, pact);
    ldg32(e, gv, ga, c->udesc, 1);
    ldg32(e, uv, ua, c->udesc, 0);
    movi(e, cst, C_NLOG2E);
    fmul(e, R(tf), R(gv), R(cst));
    mufu(e, BW_IR_MUFU_EX2, R(tf), R(tf));
    movi(e, cst, C_ONE);
    fadd(e, R(tf), R(tf), R(cst));
    mufu(e, BW_IR_MUFU_RCP, R(tf), R(tf));
    fmul(e, R(tf), R(gv), R(tf));
    fmul(e, R(tf), R(tf), R(uv));
    fmul(e, R(tf), R(tf), S(fi, 0));
    stg(e, aa, 0, tf, c->udesc);
    imadi(e, eid, step, 0, 1, eid);
    bra(e, L_e);
    loop_end(e);
    patch_bra(e, bra_e_end, here(e));
    return bra_fwd(e);
}

static void warp_sum(Em *e, int v, int t) { for (uint32_t o = 16; o >= 1; o /= 2) { shfl_down(e, t, v, o); fadd(e, R(v), R(v), R(t)); } }

/* RESNORM stage (gen_res, mode COMBINE | NORM, mutant 0): CTA 0 only, 256 threads, thread t owns x[8t .. 8t + 8) */
static int resnorm_block(Em *e, const Ctx *c) {
    isetp_ge_u32(e, c->cx, c->one);
    const int bra_not0 = bra_if_p0_fwd(e);
    int px = V64(e), pa = V64(e), pw = V64(e), pnw = V64(e), py = V64(e), fe = V64(e);
    int toff = V(e), t = V(e), t2 = V(e), a0 = V(e);
    int xa = V64(e), xq0 = V128(e), xq1 = V128(e);
    if (e->err) return -1;
    ldg_wide(e, BW_IR_LDG_E_64, px, c->pdesc, OMEGA_RES2_D_TAB, c->udesc, 1);
    ldg_wide(e, BW_IR_LDG_E_64, pa, c->pdesc, OMEGA_RES2_D_X, c->udesc, 0);
    ldg_wide(e, BW_IR_LDG_E_64, py, c->pdesc, OMEGA_RES2_D_Y, c->udesc, 0);
    ldg_wide(e, BW_IR_LDG_E_64, pw, c->pdesc, OMEGA_RES2_D_ADD, c->udesc, 0);
    ldg_wide(e, BW_IR_LDG_E_64, pnw, c->pdesc, OMEGA_RES2_D_ENTRY, c->udesc, 0);
    ldg_wide(e, BW_IR_LDG_E_64, fe, c->pdesc, OMEGA_RES2_D_F0, c->udesc, 0); /* eps f32, scale f32 */
    imadio(e, R(toff), R(c->tid), 32u, NOREG);
    imadw(e, xa, toff, 1u, px);
    ldg_wide(e, BW_IR_LDG_E_128, xq0, xa, 0, c->udesc, 1); ldg_wide(e, BW_IR_LDG_E_128, xq1, xa, 16, c->udesc, 0);
    Op x[8];
    for (unsigned i = 0; i < 8; i++) x[i] = i < 4 ? S(xq0, i) : S(xq1, i - 4);
    { /* COMBINE */
        int wq0 = V128(e), wq1 = V128(e), da = V64(e), dq0 = V128(e), dq1 = V128(e), m[8];
        for (unsigned i = 0; i < 8; i++) m[i] = V(e);
        if (e->err) return -1;
        ldg_wide(e, BW_IR_LDG_E_128, wq0, pw, 0, c->udesc, 1); ldg_wide(e, BW_IR_LDG_E_128, wq1, pw, 16, c->udesc, 0);
        imadw(e, da, toff, 1u, pa);
        for (unsigned g = 0; g < 8; g++) {
            ldg_wide(e, BW_IR_LDG_E_128, dq0, da, g * 2048u * 4u, c->udesc, 1); ldg_wide(e, BW_IR_LDG_E_128, dq1, da, g * 2048u * 4u + 16u, c->udesc, 0);
            Op w = g < 4 ? S(wq0, g) : S(wq1, g - 4);
            for (unsigned i = 0; i < 8; i++) {
                Op d = i < 4 ? S(dq0, i) : S(dq1, i - 4);
                if (g == 0) fmul(e, R(m[i]), w, d);
                else { fmul(e, R(t), w, d); fadd(e, R(m[i]), R(m[i]), R(t)); }
            }
        }
        for (unsigned i = 0; i < 8; i++) fadd(e, x[i], x[i], R(m[i]));
    }
    {
        int sa = V64(e);
        if (e->err) return -1;
        for (unsigned i = 0; i < 8; i++) { if (i == 0) imadw(e, sa, toff, 1u, px); else imadw(e, sa, c->one, 4u, sa); stgo(e, sa, x[i], c->udesc); }
    }
    { /* NORM */
        int sq = V(e), rs = V(e), na = V64(e), nq0 = V128(e), nq1 = V128(e), ya = V64(e), yv = V(e);
        if (e->err) return -1;
        imadw(e, na, toff, 1u, pnw);
        ldg_wide(e, BW_IR_LDG_E_128, nq0, na, 0, c->udesc, 1); ldg_wide(e, BW_IR_LDG_E_128, nq1, na, 16, c->udesc, 0);
        fmul(e, R(sq), x[0], x[0]);
        for (unsigned i = 1; i < 8; i++) ffmao(e, R(sq), x[i], x[i], R(sq));
        warp_sum(e, sq, t);
        imadio(e, R(a0), R(c->warp), 4u, NOREG); imadio(e, R(a0), R(c->one), 0u /* SM_RED */, a0);
        isetp_ge_u32(e, c->lane, c->one);
        { Region r = region_skip_if_p0(e); sts32o(e, a0, R(sq)); region_end(e, r); }
        bar_sync(e);
        { /* cta_reduce8 at shared 0, sum */
            int q0 = V128(e), q1 = V128(e);
            if (e->err) return -1;
            movi(e, a0, 0u);
            lds128(e, q0, a0, 0, 1); lds128(e, q1, a0, 16, 0);
            imadio(e, R(sq), S(q0, 0), 1u, NOREG);
            for (unsigned i = 1; i < 8; i++) fadd(e, R(sq), R(sq), i < 4 ? S(q0, i) : S(q1, i - 4));
        }
        movi(e, t2, C_INV2048);
        ffmao(e, R(rs), R(sq), R(t2), S(fe, 0));
        mufu(e, BW_IR_MUFU_RSQ, R(rs), R(rs));
        imadw(e, ya, toff, 1u, py);
        for (unsigned i = 0; i < 8; i++) {
            fmul(e, R(yv), x[i], R(rs));
            fmul(e, R(yv), R(yv), i < 4 ? S(nq0, i) : S(nq1, i - 4));
            fmul(e, R(yv), R(yv), S(fe, 1));
            if (i) imadw(e, ya, c->one, 4u, ya);
            stg(e, ya, 0, yv, c->udesc);
        }
    }
    patch_bra(e, bra_not0, here(e));
    return bra_fwd(e);
}

int omega_blackwell_codegen_resident2_prog_p(const OmegaResident2Spec *spec, BlackwellIRProgram *prog, OmegaResidentPatches *pt) {
    if (!spec || !prog || spec->warps != 8u || spec->mutant < 0 || spec->mutant > 1) return -1;
    if (pt) memset(pt, 0, sizeof *pt);
    const uint32_t W = spec->warps;
    omega_bw_ir_init(prog);
    Em em; memset(&em, 0, sizeof em); em.p = prog;
    Em *e = &em;

    /* ---- prologue: values live for the whole kernel ---- */
    Ctx c;
    int st = V(e); /* W-L2i: ctl pointer and barrier target are rebuilt inside the barrier (constant bank), not held kernel-wide */
    c.udesc = UV64(e); c.pdesc = V64(e); c.tid = V(e); c.cx = V(e); c.warp = V(e); c.lane = V(e); c.one = V(e); c.gw = V(e);
    if (e->err) return -1;
    ldcu64(e, c.udesc, 0x358);
    ldc64(e, c.pdesc, OMEGA_RES_ARG_TABLE);
    s2r(e, c.tid, BW_SR_TID_X); s2r(e, c.cx, BW_SR_CTAID_X);
    movi(e, c.one, 1);
    shr(e, c.warp, c.tid, 0, 5);
    andi(e, c.lane, c.tid, 0, 31);
    imadi(e, c.gw, c.cx, 0, W, c.warp);
    movrz(e, st);

    /* ---- stage loop: dispatch on kind ---- */
    const int L_st = here(e);
    loop_begin(e);
    int ends[8], ne = 0;
    int kv = V64(e), kc = V(e);
    if (e->err) return -1;
    ldg_wide(e, BW_IR_LDG_E_64, kv, c.pdesc, OMEGA_RES2_D_KIND, c.udesc, 1);
    int bra_k[4];
    const uint32_t kinds[4] = { OMEGA_RES2_SWIGLU, OMEGA_RES2_RESNORM, OMEGA_RES2_GEMV8, OMEGA_RES2_GEMV16_ADD };
    for (int i = 0; i < 4; i++) {
        /* kind == kinds[i]  <=>  kind >= kinds[i] && kind < kinds[i] + 1 ; ISETP.GE.U32 only: test (kind - kinds[i]) == 0 as
         * (kind - kinds[i]) < 1, i.e. !(x >= 1) */
        imadi(e, kc, c.one, 0, (uint32_t)(0u - kinds[i]), NOREG);   /* -kinds[i] */
        imadi(e, kc, kv, 0, 1, kc);                                  /* kind - kinds[i] */
        isetp_ge_u32(e, kc, c.one);
        BlackwellIRInsn b = I0(BW_IR_BRA); b.predicate_p0 = true; b.predicate_not = true; b.imm = 1; emit(e, b, K_BRA);
        bra_k[i] = here(e) - 1;
    }
    /* default: GEMV16 */
    ends[ne++] = gemv_block(e, &c, 16u, 0);
    patch_bra(e, bra_k[0], here(e)); ends[ne++] = swiglu_block(e, &c);
    patch_bra(e, bra_k[1], here(e)); ends[ne++] = resnorm_block(e, &c);
    patch_bra(e, bra_k[2], here(e)); ends[ne++] = gemv_block(e, &c, 8u, 0);
    patch_bra(e, bra_k[3], here(e)); ends[ne++] = gemv_block(e, &c, 16u, 1);
    if (e->err) return -1;
    for (int i = 0; i < ne; i++) { if (ends[i] < 0) return -1; patch_bra(e, ends[i], here(e)); }

    /* ---- next stage? ---- */
    imadi(e, st, c.one, 0, 1, st);
    { int nst = V(e); ldc32(e, nst, OMEGA_RES_ARG_NSTAGES); isetp_ge_u32(e, st, nst); }
    const int bra_done = bra_if_p0_fwd(e);
    imadw(e, c.pdesc, c.one, OMEGA_RES2_DESC_BYTES, c.pdesc);

    if (spec->mutant != 1) {
        /* ---- grid barrier: the W-L2f r4 production form (omega_blackwell_codegen_resident.c, scope_gpu 1, fence 0, abort 1) ---- */
        int v = V(e), old = V(e), spins = V(e), verdict = V(e), smaddr = V(e), t0 = V(e), lim = V(e), c1 = V(e), au = V(e), pa = V64(e), pb = V64(e), pe = V64(e);
        int target = V(e), pctl = V64(e);
        if (e->err) return -1;
        membar(e);
        ldc32(e, lim, OMEGA_RES_ARG_GRID);
        ldc32(e, target, OMEGA_RES_ARG_BASE);
        imadr(e, target, st, lim, target);           /* W-L2i: target = base + st * grid (was: += grid per barrier; same value) */
        bar_sync(e);
        s2r(e, t0, BW_SR_TID_X);
        isetp_ge_u32(e, t0, c.one);
        const int r_bssy = bssy_fwd(e);
        const int r_skip = bra_if_p0_fwd(e);
        ldc64(e, pb, OMEGA_RES_ARG_BAR);
        ldc64(e, pctl, OMEGA_RES_ARG_CTL);
        ldc32(e, lim, OMEGA_RES_ARG_SPINMAX);
        movi(e, au, OMEGA_RES_ABORT_UNIT);
        s2r(e, t0, BW_SR_CTAID_X);
        imadw(e, pe, c.one, OMEGA_RES_CTL_ERROR, pctl);
        imadw(e, pa, c.one, OMEGA_RES_CTL_CANCEL, pctl);
        movrz(e, v); movrz(e, c1);
        isetp_ge_u32(e, t0, c.one);
        const int bra_check_skip = bra_if_p0_fwd(e); /* CTA 0 only reads cancel */
        { BlackwellIRInsn i = I0(BW_IR_LDG_STRONG_SYS); i.dst_vreg = c1; i.src1_vreg = pa; i.ureg = c.udesc; emit(e, i, K_VAR); }
        patch_bra(e, bra_check_skip, here(e));
        { mark(pt, here(e), OMEGA_RES_PATCH_ATOM_GPU);
          BlackwellIRInsn i = I0(BW_IR_ATOMG_ADD_STRONG_SYS); i.dst_vreg = old; i.src1_vreg = pb; i.src2_vreg = c.one; i.ureg = c.udesc; emit(e, i, K_VAR_MORE); }
        imadi(e, verdict, v, 0, 1, c1);
        isetp_ge_u32(e, verdict, c.one);
        const int bra_abort = bra_if_p0_fwd(e);
        movrz(e, spins);
        const int L_sp = here(e);
        loop_begin(e);
        mark(pt, here(e), OMEGA_RES_PATCH_LDG_GPU);
        ldg_sys(e, v, pb, c.udesc);
        isetp_ge_u32(e, v, target);
        const int bra_arrived = bra_if_p0_fwd(e);
        imadi(e, spins, c.one, 0, 1, spins);
        isetp_ge_u32(e, spins, lim);
        const int bra_tmo = bra_if_p0_fwd(e);
        bra(e, L_sp);
        loop_end(e);
        patch_bra(e, bra_tmo, here(e));
        atom_add(e, old, pe, c.one, c.udesc);
        const int bra_tmo_abort = bra_fwd(e);
        patch_bra(e, bra_arrived, here(e));
        isetp_ge_u32(e, v, au);
        const int bra_seen_abort = bra_if_p0_fwd(e);
        isetp_ge_u32(e, t0, c.one);
        const int bra_not_c0 = bra_if_p0_fwd(e);
        imadw(e, pa, c.one, OMEGA_RES_CTL_PROGRESS, pctl);
        { BlackwellIRInsn i = I0(BW_IR_STG_STRONG_SYS); i.src1_vreg = pa; i.src2_vreg = st; i.ureg = c.udesc; emit(e, i, K_STORE); }
        patch_bra(e, bra_not_c0, here(e));
        movrz(e, verdict);
        const int bra_ok = bra_fwd(e);
        const int at_abort = here(e);
        patch_bra(e, bra_abort, at_abort);
        patch_bra(e, bra_tmo_abort, at_abort);
        { mark(pt, here(e), OMEGA_RES_PATCH_ATOM_GPU);
          BlackwellIRInsn i = I0(BW_IR_ATOMG_ADD_STRONG_SYS); i.dst_vreg = old; i.src1_vreg = pb; i.src2_vreg = au; i.ureg = c.udesc; emit(e, i, K_VAR); }
        patch_bra(e, bra_seen_abort, here(e));
        movi(e, verdict, 1);
        patch_bra(e, bra_ok, here(e));
        movi(e, smaddr, 64u); /* verdict word apart from RESNORM's partials at 0..31 */
        sts32(e, smaddr, verdict);
        const int r_bsync = here(e);
        bsync(e);
        patch_bra(e, r_skip, r_bsync);
        patch_bra(e, r_bssy, r_bsync + 1);
        bar_sync(e);
        movi(e, smaddr, 64u);
        lds32(e, v, smaddr);
        isetp_ge_u32(e, v, c.one);
        const int bra_exit = bra_if_p0_fwd(e);
        emit(e, I0(BW_IR_CCTL_IVALL), K_FIXED);
        bra(e, L_st);
        loop_end(e);
        const int L_end = here(e);
        patch_bra(e, bra_exit, L_end);
        patch_bra(e, bra_done, L_end);
    } else {
        bar_sync(e); /* RESNORM reuses shared memory across stages */
        bra(e, L_st);
        loop_end(e);
        patch_bra(e, bra_done, here(e));
    }
    tail(e);
    if (e->err) return -1;
    return omega_bw_regalloc_solve(prog);
}

int omega_resident2_prog_stats(const OmegaResident2Spec *spec, uint32_t *insns, uint32_t *gprs) {
    static BlackwellIRProgram prog;
    if (omega_blackwell_codegen_resident2_prog_p(spec, &prog, NULL) != 0) return -1;
    if (insns) *insns = (uint32_t)prog.count;
    if (gprs) *gprs = prog.regalloc.peak_gpr_usage;
    return 0;
}
