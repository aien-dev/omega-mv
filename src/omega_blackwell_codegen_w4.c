/* omega_blackwell_codegen_w4.c: MXFP4 (e2m1 + E8M0 per 32) GEMV kernel (qwen38/w4-gemv).
 * See the header for the contract and the argument words.
 *
 * Scheduling: the same chip-learned rules as omega_gpu_elementwise_api.c (every instruction
 * waits on everything still pending; variable-latency results go through scoreboard 0, stores
 * set the read barrier 1, uniform loads scoreboard 2; fixed-latency producers stall 6; ISETP
 * stalls 14 before a predicated branch). Loads of one K step are issued back to back (all set
 * SB0, only the first waits), so a warp has every load of the step in flight at once.
 */
#include "omega_blackwell_codegen_w4.h"
#include "omega_blackwell_codegen_moe.h" /* argument words OMEGA_FP8_ARG_*, OMEGA_FP8_FIXED_LAT */
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
#define SB_PF_A 3u /* pf: register set A (scoreboard 0 stays with S2R, LDC, SHFL, LDS) */
#define SB_PF_B 4u
#define NOREG (-1)
#define MAX_LOOPS 4

enum { K_FIXED, K_PRED, K_VAR, K_VAR_MORE, K_VAR_PF, K_VAR_PF_MORE, K_UVAR, K_STORE, K_SYNC, K_EXIT, K_BRA, K_BSSY, K_BSYNC };

typedef struct {
    BlackwellIRProgram *p;
    uint32_t pending;
    int err;
    int open_start[MAX_LOOPS];
    int depth;
    uint32_t pf_sb; /* K_VAR_PF*: the write barrier of the register set being loaded (SB_PF_A or SB_PF_B) */
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
    /* pf loads: write barrier = the set's own scoreboard, which is NOT added to pending: the decode of the other set must
     * not wait on these. wait_sb() adds it right before the first use. The read barrier works as for K_VAR. */
    case K_VAR_PF:      in.control = CW(4, e->pf_sb, SB_RD, wait) | CW_YIELD; e->pending = 1u << SB_RD; break;
    case K_VAR_PF_MORE: in.control = CW(4, e->pf_sb, SB_RD, (e->pending & (1u << SB_RD))) | CW_YIELD; e->pending |= 1u << SB_RD; break;
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
static void imadr(Em *e, int d, int a, int b, int c) { BlackwellIRInsn i = I0(BW_IR_IMAD); i.dst_vreg = d; i.src1_vreg = a; i.src2_vreg = b; i.src3_vreg = c; emit(e, i, K_FIXED); }
/* d64 = a * imm + c64 */
static void imadw(Em *e, int d64, int a, uint32_t imm, int c64) { BlackwellIRInsn i = I0(BW_IR_IMAD_WIDE); i.dst_vreg = d64; i.src1_vreg = a; i.imm = imm; i.src3_vreg = c64; emit(e, i, K_FIXED); }
static void shr(Em *e, int d, int a, unsigned asub, uint32_t imm) { BlackwellIRInsn i = I0(BW_IR_SHF_R); i.dst_vreg = d; i.src1_vreg = a; i.src1_subreg = (uint8_t)asub; i.imm = imm; emit(e, i, K_FIXED); }
static void andi(Em *e, int d, int a, unsigned asub, uint32_t imm) { BlackwellIRInsn i = I0(BW_IR_LOP3_AND); i.dst_vreg = d; i.src1_vreg = a; i.src1_subreg = (uint8_t)asub; i.imm = imm; emit(e, i, K_FIXED); }
static void xorr(Em *e, int d, int a, int b) { BlackwellIRInsn i = I0(BW_IR_LOP3_XOR); i.dst_vreg = d; i.src1_vreg = a; i.src2_vreg = b; emit(e, i, K_FIXED); }
static void ldg_wide(Em *e, BlackwellIROpcode op, int d, int addr64, uint32_t imm, int udesc, int first) {
    BlackwellIRInsn i = I0(op); i.dst_vreg = d; i.src1_vreg = addr64; i.imm = imm; i.ureg = udesc; emit(e, i, first ? K_VAR : K_VAR_MORE);
}
static void ldg32(Em *e, int d, int addr64, int udesc, int first) { BlackwellIRInsn i = I0(BW_IR_LDG_E); i.dst_vreg = d; i.src1_vreg = addr64; i.ureg = udesc; emit(e, i, first ? K_VAR : K_VAR_MORE); }
/* pf loads of one register set (e->pf_sb) */
static void ldg_pf(Em *e, BlackwellIROpcode op, int d, int addr64, uint32_t imm, int udesc, int first) {
    BlackwellIRInsn i = I0(op); i.dst_vreg = d; i.src1_vreg = addr64; i.imm = imm; i.ureg = udesc; emit(e, i, first ? K_VAR_PF : K_VAR_PF_MORE);
}
/* the next instruction waits for every load of the set on scoreboard sb */
static void wait_sb(Em *e, uint32_t sb) { e->pending |= 1u << sb; }
/* d[dsub] = a[asub] (IMAD a * 1 + RZ: an exact 32-bit copy, so f32 bit patterns pass unchanged) */
static void movr(Em *e, int d, unsigned dsub, int a, unsigned asub) {
    BlackwellIRInsn i = I0(BW_IR_IMAD); i.dst_vreg = d; i.dst_subreg = (uint8_t)dsub; i.src1_vreg = a; i.src1_subreg = (uint8_t)asub; i.imm = 1; i.src3_vreg = NOREG;
    emit(e, i, K_FIXED);
}
static void stg(Em *e, int addr64, uint32_t imm, int val, int udesc) { BlackwellIRInsn i = I0(BW_IR_STG_E); i.src1_vreg = addr64; i.src2_vreg = val; i.imm = imm; i.ureg = udesc; emit(e, i, K_STORE); }
static void lds32(Em *e, int d, int addr) { BlackwellIRInsn i = I0(BW_IR_LDS32); i.dst_vreg = d; i.src1_vreg = addr; emit(e, i, K_VAR); }
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

/* register-amount left shift (SHF.L.U32 Rd, Ra, Rb, RZ; chip-verified golden words, omega_blackwell_codegen.c) */
static void shlr(Em *e, int d, int a, int b) { BlackwellIRInsn i = I0(BW_IR_SHF_L_U32); i.dst_vreg = d; i.src1_vreg = a; i.src2_vreg = b; emit(e, i, K_FIXED); }


/* ---- sched 1: dependency-driven stall counts (after register allocation) ----
 * The emitter gives every fixed-latency instruction stall 6, which keeps any user >= 6 cycles behind its producer but
 * also makes independent instructions wait 6 cycles each. The repository's chip-proven rule is the distance, not the
 * per-instruction count: a fixed-latency producer stalls >= 5 cycles before its consumer (we keep 6;
 * omega_gpu_elementwise_api.c:13). Same pass as omega_blackwell_codegen_tern.c (chip-proven at TERN-T0 2300f164) with
 * this kernel's fixed-latency set, which adds FFMA, FADD and LOP3_XOR: walk the program in order on physical registers
 * and give each instruction the smallest stall that keeps the next instruction >= OMEGA_FP8_FIXED_LAT cycles behind
 * every fixed-latency write it reads or overwrites. Before a control instruction (BRA, EXIT, BSSY, BSYNC, BAR) or a
 * branch target every fixed-latency write has completed, so straight-line timing never spans a jump. Other instructions
 * keep at least their emitted stall (loads 4, ISETP 14 before a predicated branch); wait masks are untouched. */
static int w4_fixed(BlackwellIROpcode op) {
    switch (op) {
    case BW_IR_IMAD: case BW_IR_IMAD_WIDE: case BW_IR_SHF_R: case BW_IR_LOP3_AND: case BW_IR_LOP3_XOR: case BW_IR_MOV_IMM:
    case BW_IR_MOV_RZ: case BW_IR_FFMA: case BW_IR_FADD: case BW_IR_SHF_L_U32: return 1;
    default: return 0;
    }
}
static int w4_control(BlackwellIROpcode op) {
    return op == BW_IR_BRA || op == BW_IR_EXIT || op == BW_IR_BSSY || op == BW_IR_BSYNC || op == BW_IR_BAR_SYNC;
}
static void w4_span(const BlackwellIRProgram *p, int v, int *lo, int *cnt) {
    *lo = -1; *cnt = 0;
    if (v < 0 || v >= p->regalloc.num_vregs) return;
    const int ph = p->regalloc.vreg_to_phys[v];
    if (ph < 0) return;
    *lo = ph; *cnt = (int)(p->regalloc.intervals[v].bundle_size ? p->regalloc.intervals[v].bundle_size : 1u);
}
static int w4_schedule_stalls(BlackwellIRProgram *p) {
    static uint8_t target[BW_MAX_IR_INSNS + 1];
    uint64_t ready[BW_MAX_VREGS + 8];
    const size_t n = p->count;
    memset(target, 0, sizeof target);
    memset(ready, 0, sizeof ready);
    for (size_t i = 0; i < n; i++) {
        const BlackwellIRInsn *in = &p->insns[i];
        if (in->op == BW_IR_BRA || in->op == BW_IR_BSSY) {
            const long t = (long)i + (long)(int32_t)in->imm;
            if (t < 0 || t > (long)n) return -1;
            target[t] = 1;
        }
    }
    uint64_t T = 0, all = 0;
    for (size_t i = 0; i < n; i++) {
        BlackwellIRInsn *in = &p->insns[i];
        const uint32_t s_emit = (in->control >> 9) & 0xfu;
        const int fixed = w4_fixed(in->op);
        if (fixed) {
            int lo, c; w4_span(p, in->dst_vreg, &lo, &c);
            for (int k = 0; k < c; k++) { if (lo + k >= BW_MAX_VREGS + 8) return -1; ready[lo + k] = T + OMEGA_FP8_FIXED_LAT; }
            if (c && T + OMEGA_FP8_FIXED_LAT > all) all = T + OMEGA_FP8_FIXED_LAT;
        }
        uint64_t need = T + (fixed ? 1u : s_emit);
        if (i + 1 < n) {
            const BlackwellIRInsn *nx = &p->insns[i + 1];
            const int ops[4] = { nx->dst_vreg, nx->src1_vreg, nx->src2_vreg, nx->src3_vreg };
            for (int q = 0; q < 4; q++) {
                int lo, c; w4_span(p, ops[q], &lo, &c);
                for (int k = 0; k < c; k++) if (lo + k < BW_MAX_VREGS + 8 && ready[lo + k] > need) need = ready[lo + k];
            }
            if ((w4_control(nx->op) || target[i + 1]) && all > need) need = all;
        }
        const uint64_t s = need - T;
        if (s > 15 || (s == 0 && (fixed || s_emit != 0))) return -1; /* stall 0 only where emitted (the final self-branch) */
        if (fixed || s > s_emit) in->control = (in->control & ~(0xfu << 9)) | ((uint32_t)s << 9);
        T = need;
    }
    return 0;
}


/* ---- e2m1 reference (host) ---- */
float omega_w4_e2m1_ref(uint8_t nib) {
    static const float mag[8] = { 0.0f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f };
    const float v = mag[nib & 7u];
    return (nib & 8u) ? -v : v;
}
uint32_t omega_w4_e2m1_kernel_bits(uint8_t nib) { return ((uint32_t)(nib & 7u) << 22) | ((uint32_t)(nib & 8u) << 28); }

static int spec_ok(const OmegaW4GemvSpec *s) {
    if (!s) return 0;
    if (s->lane_bytes != 8 && s->lane_bytes != 16) return 0;
    if (s->rows != 1 && s->rows != 2 && s->rows != 4 && s->rows != 8) return 0;
    if (s->warps < 1 || s->warps > 32) return 0;
    if (s->k == 0 || s->k > OMEGA_W4_MAX_K || s->k % (64u * s->lane_bytes)) return 0;
    if (s->mutant < 0 || s->mutant > 7 || (s->mutant == 6 && !s->coal) || (s->mutant == 7 && !s->pf)) return 0;
    if (s->sched > 1 || s->coal > 1 || s->pf > 2) return 0;
    if (s->coal && s->warps * s->rows > 32u) return 0;
    if ((uint64_t)(s->rows - 1) * (s->k / 2u) + s->lane_bytes >= (1u << 23)) return 0; /* 24-bit signed LDG immediate */
    return 1;
}
uint32_t omega_w4_gemv_steps(const OmegaW4GemvSpec *spec) { return spec_ok(spec) ? spec->k / (64u * spec->lane_bytes) : 0; }
static uint32_t out_slot_bytes(const OmegaW4GemvSpec *s) { return s->warps * s->rows * 4u; }
uint32_t omega_w4_gemv_shared_bytes(const OmegaW4GemvSpec *spec) { return spec_ok(spec) && spec->coal ? 2u * out_slot_bytes(spec) : 0u; }

/* Nibble i (0..7) of a 32-bit weight word goes through five instructions:
 *   E: u = the word shifted so the nibble's bits 2..0 sit at 24..22 (IMAD by 2^(22 - 4i), or SHF.R for i = 6, 7)
 *   S: sg = the word AND the nibble's sign bit (bit 4i + 3)
 *   M: a = u AND 0x01C00000
 *   F: f = sg * 2^(28 - 4i) + a: the sign at bit 31 plus the magnitude, the f32 bits of e2m1 * 2^-126
 *   C: dot = fma(f, x', dot) */
static void nib_e(Em *e, int u, int wq, unsigned word, unsigned i) {
    if (i <= 5) imadi(e, u, wq, word, 1u << (22u - 4u * i), NOREG); else shr(e, u, wq, word, 4u * i - 22u);
}
static void nib_s(Em *e, int sg, int wq, unsigned word, unsigned i) { andi(e, sg, wq, word, 8u << (4u * i)); }
static void nib_m(Em *e, int u) { andi(e, u, u, 0, 0x01C00000u); }
static void nib_f(Em *e, int sg, int u, unsigned i) { imadi(e, sg, sg, 0, 1u << (28u - 4u * i), u); }
/* x' float of weight j of the lane chunk: quad j / 4, lane j % 4; mutant 5 swaps the two nibbles of every byte */
static void nib_c(Em *e, int dot, int f, const int *xq, unsigned j, int mutant) {
    const unsigned jx = mutant == 5 ? (j ^ 1u) : j;
    ffma(e, dot, f, xq[jx / 4u], jx % 4u, dot);
}
#define PIPE_T1 4
#define PIPE_T2 6
static void decode_seq(Em *e, int wqv, uint32_t words, const int *xq, int dot, int u, int sg, int mutant) {
    for (uint32_t word = 0; word < words; word++)
        for (unsigned i = 0; i < 8; i++) {
            nib_e(e, u, wqv, word, i); nib_m(e, u); nib_s(e, sg, wqv, word, i); nib_f(e, sg, u, i);
            nib_c(e, dot, sg, xq, word * 8u + i, mutant);
        }
}
/* sched 1: iteration c emits E(c), S(c), M(c - 2), F(c - 3), C(c - 5) (the FP8 kernel's pipeline, one nibble per slot) */
static void decode_pipe(Em *e, int wqv, uint32_t words, const int *xq, int dot, const int *t1, const int *t2, int mutant) {
    const uint32_t NB = 8u * words;
    for (uint32_t c = 0; c < NB + 5u; c++) {
        if (c < NB) { nib_e(e, t1[c % PIPE_T1], wqv, c / 8u, c % 8u); nib_s(e, t2[c % PIPE_T2], wqv, c / 8u, c % 8u); }
        if (c >= 2 && c - 2u < NB) nib_m(e, t1[(c - 2u) % PIPE_T1]);
        if (c >= 3 && c - 3u < NB) { const uint32_t b = c - 3u; nib_f(e, t2[b % PIPE_T2], t1[b % PIPE_T1], b % 8u); }
        if (c >= 5 && c - 5u < NB) { const uint32_t b = c - 5u; nib_c(e, dot, t2[b % PIPE_T2], xq, b, mutant); }
    }
}

int omega_blackwell_codegen_w4_gemv_prog(const OmegaW4GemvSpec *spec, BlackwellIRProgram *prog) {
    if (!spec_ok(spec) || !prog) return -1;
    const uint32_t K = spec->k, LB = spec->lane_bytes, R = spec->rows, W = spec->warps;
    const uint32_t WPL = 2u * LB, KS = K / (32u * WPL), XQ = WPL / 4u, WORDS = LB / 4u;
    const uint32_t ROWB = K / 2u, ROWS_S = K / OMEGA_W4_GROUP; /* weight and scale bytes per row */
    const uint32_t GPS = 32u * WPL / OMEGA_W4_GROUP;            /* scale bytes per K step (groups the warp covers) */
    const uint32_t lg_shift = LB == 8 ? 1u : 0u;                /* lane -> group within the step */
    const BlackwellIROpcode ldw = LB == 16 ? BW_IR_LDG_E_128 : BW_IR_LDG_E_64;
    omega_bw_ir_init(prog);
    if (spec->pf == 2 && omega_bw_ir_set_gpr_limit(prog, 200u) != 0) return -1; /* x' twice: past the default 128 */
    Em em; memset(&em, 0, sizeof em); em.p = prog;
    Em *e = &em;

    /* ---- prologue ---- */
    int udesc = UV64(e), ptab = V64(e), px = V64(e), py = V64(e);
    int rgn = V(e), xs = V(e), ys = V(e), stride = V(e);
    int tid = V(e), cx = V(e), cy = V(e), warp = V(e), lane = V(e), one = V(e), gidx = V(e);
    int tent = V64(e), wbase = V64(e), sbase = V64(e), off = V(e), xrow = V64(e), yrow = V64(e), xlane = V64(e);
    int wlane = V(e), slane = V(e), rg = V(e), lg = V(e), bsel = V(e), shamt = V(e), c24 = V(e), cbias = V(e);
    if (e->err) return -1;
    ldcu64(e, udesc, 0x358);
    ldc64(e, ptab, OMEGA_FP8_ARG_TAB); ldc64(e, px, OMEGA_FP8_ARG_X); ldc64(e, py, OMEGA_FP8_ARG_Y);
    ldc32(e, rgn, OMEGA_FP8_ARG_RG); ldc32(e, xs, OMEGA_FP8_ARG_XSTRIDE); ldc32(e, ys, OMEGA_FP8_ARG_YSTRIDE); ldc32(e, stride, OMEGA_FP8_ARG_STRIDE);
    s2r(e, tid, BW_SR_TID_X); s2r(e, cx, BW_SR_CTAID_X); s2r(e, cy, BW_SR_CTAID_Y);
    movi(e, one, 1);
    movi(e, c24, 24);
    movi(e, cbias, OMEGA_W4_SCALE_BIAS << 23);
    shr(e, warp, tid, 0, 5);
    andi(e, lane, tid, 0, 31);
    if (spec->mutant == 3) xorr(e, gidx, cy, one); else imadi(e, gidx, cy, 0, 1, NOREG);
    imadw(e, tent, gidx, 16, ptab);
    ldg_wide(e, BW_IR_LDG_E_64, wbase, tent, 0, udesc, 1);
    ldg_wide(e, BW_IR_LDG_E_64, sbase, tent, 8, udesc, 0);
    imadr(e, off, cy, xs, NOREG); imadw(e, xrow, off, 1, px);
    imadr(e, off, cy, ys, NOREG); imadw(e, yrow, off, 1, py);
    imadw(e, xlane, lane, WPL * 4u, xrow);           /* this lane's x' chunk of step 0 */
    imadi(e, wlane, lane, 0, LB, NOREG);             /* byte offset of this lane's weights in a row */
    if (lg_shift) shr(e, lg, lane, 0, lg_shift); else imadi(e, lg, lane, 0, 1, NOREG); /* this lane's group in a step */
    andi(e, slane, lg, 0, 0xFFFFFFFCu);              /* the scale word holding it */
    if (spec->mutant == 4) imadi(e, bsel, one, 0, 1, lg); else imadi(e, bsel, lg, 0, 1, NOREG);
    andi(e, bsel, bsel, 0, 3u);                      /* its byte in the word */
    imadi(e, shamt, bsel, 0, 0xFFFFFFF8u, c24);      /* 24 - 8 b: (word << shamt) >> 24 = byte b */
    imadi(e, rg, cx, 0, W, warp);                    /* first row group of this warp */

    int pw = V64(e), ps = V64(e), pxc = V64(e), pyr = V64(e), pya = V64(e), cnt = V(e);
    int acc[8], wq[8], sw[8], psr[8], xq[8];
    for (uint32_t r = 0; r < R; r++) { acc[r] = V(e); wq[r] = LB == 16 ? V128(e) : V64(e); sw[r] = V(e); psr[r] = V64(e); }
    for (uint32_t q = 0; q < XQ; q++) xq[q] = V128(e);
    int dot = V(e), u = V(e), sg = V(e), sp = V(e), t = V(e);
    int pt1[PIPE_T1], pt2[PIPE_T2];
    if (spec->sched) { for (int q = 0; q < PIPE_T1; q++) pt1[q] = V(e); for (int q = 0; q < PIPE_T2; q++) pt2[q] = V(e); }
    const int coal = spec->coal != 0;
    int uc = NOREG, rgx = rg, um1 = NOREG, negU = NOREG, negn = NOREG, okw = NOREG, tq = NOREG, slb = NOREG, stog = NOREG;
    int so = NOREG, sa = NOREG, wl1 = NOREG, val = NOREG, row = NOREG, skip = NOREG;
    if (coal) {
        uc = V(e); rgx = V(e); um1 = V(e); negU = V(e); negn = V(e); okw = V(e); tq = V(e); slb = V(e); stog = V(e);
        so = V(e); sa = V(e); wl1 = V(e); val = V(e); row = V(e); skip = V(e);
    }
    const int pf = (int)spec->pf; /* 0 classic; 1 weights + scales of step s + 1 in flight; 2 also its x' */
    int wqB[8], swB[8], xqB[8], two = NOREG, rgn2 = NOREG;
    for (int q = 0; q < 8; q++) wqB[q] = swB[q] = xqB[q] = NOREG;
    if (pf) { /* set B, and the clamp registers the classic coal-off kernel does not need */
        for (uint32_t r = 0; r < R; r++) { wqB[r] = LB == 16 ? V128(e) : V64(e); swB[r] = V(e); }
        if (pf == 2) for (uint32_t q = 0; q < XQ; q++) xqB[q] = V128(e);
        two = V(e); rgn2 = V(e);
        if (!coal) { rgx = V(e); um1 = V(e); negU = V(e); okw = V(e); tq = V(e); }
    }
    if (e->err) return -1;

    if (coal) { /* the FP8 kernel's variant A (omega_blackwell_codegen_moe.c), slot sets at shared 0 and W R 4 */
        const uint32_t nrow = W * R;
        imadi(e, um1, one, 0, 0xffffffffu, rgn);
        imadi(e, negU, rgn, 0, 0xffffffffu, NOREG);
        imadi(e, negn, rgn, 0, 0u - R, NOREG);
        movi(e, slb, 0);
        movi(e, stog, spec->mutant == 6 ? 0u : out_slot_bytes(spec)); /* mutant 6: one slot set, the round race */
        imadi(e, so, warp, 0, R * 4u, NOREG);
        imadi(e, tq, lane, 0, 0xffffffffu, NOREG); imadi(e, tq, one, 0, nrow - 1u, tq); shr(e, tq, tq, 0, 31);
        imadi(e, wl1, warp, 0, 1, tq); imadi(e, wl1, one, 0, 1, wl1);
        imadi(e, uc, cx, 0, W, NOREG);
    }

    /* ---- pf: helpers over the registers above (macros: they read the locals) ---- */
#define W4_CLAMP(src) do { imadi(e, tq, src, 0, 1, negU); shr(e, okw, tq, 0, 31); imadi(e, tq, one, 0, 1, tq); \
                           imadr(e, rgx, okw, tq, um1); /* src < RG ? src : RG - 1 */ } while (0)
#define W4_PTRS() do { imadw(e, pw, rgx, R * ROWB, wbase); imadw(e, pw, wlane, 1, pw); \
                       imadw(e, ps, rgx, R * ROWS_S, sbase); imadw(e, ps, slane, 1, ps); if (pf != 1) imadw(e, pxc, one, 0, xlane); } while (0)
    /* one batch: x' of a step into XQ_ on scoreboard SBX (LX), weights + scales of a step into (WQ, SW) on SBW (LW); then the
     * pointers of what was loaded move to the next step */
#define W4_LOAD(LX, XQ_, SBX, LW, WQ, SW, SBW) do { \
        if (LW) for (uint32_t r = 0; r < R; r++) imadw(e, psr[r], one, r * ROWS_S, ps); \
        int first_ = 1; \
        if (LX) { e->pf_sb = (SBX); for (uint32_t q = 0; q < XQ; q++) { ldg_pf(e, BW_IR_LDG_E_128, XQ_[q], pxc, q * 16u, udesc, first_); first_ = 0; } } \
        if (LW) { e->pf_sb = (SBW); \
            for (uint32_t r = 0; r < R; r++) { ldg_pf(e, ldw, WQ[r], pw, r * ROWB, udesc, first_); first_ = 0; } \
            for (uint32_t r = 0; r < R; r++) ldg_pf(e, BW_IR_LDG_E, SW[r], psr[r], 0, udesc, 0); } \
        if (LW) { imadw(e, pw, one, 32u * LB, pw); imadw(e, ps, one, spec->mutant == 2 ? 0u : GPS, ps); } \
        if (LX) imadw(e, pxc, one, 32u * WPL * 4u, pxc); \
    } while (0)
    if (pf) { /* step 0 of this warp's first row group (clamped, so a warp without rows still reads in range) */
        if (!coal) { imadi(e, um1, one, 0, 0xffffffffu, rgn); imadi(e, negU, rgn, 0, 0xffffffffu, NOREG); }
        movi(e, two, 2);
        W4_CLAMP(rg); W4_PTRS(); W4_LOAD(pf == 2, xq, SB_PF_A, 1, wq, sw, SB_PF_A);
    }

    /* ---- row-group loop ---- */
    const int L_rg = here(e);
    loop_begin(e);
    isetp_ge_u32(e, coal ? uc : rg, rgn);
    const int bra_end = bra_if_p0_fwd(e);
    if (!pf) {
        if (coal) W4_CLAMP(rg);
        W4_PTRS();
    }
    for (uint32_t r = 0; r < R; r++) movrz(e, acc[r]);
    movi(e, cnt, KS);
    if (pf == 1) imadw(e, pxc, one, 0, xlane);              /* pf 1: x' is read for the step being decoded */

    /* ---- K loop: one step per iteration, every load of the step issued before any use ---- */
    const int L_k = here(e);
    loop_begin(e);
    if (pf) {
        /* set A holds step s. Last step of the row group: the pointers move to the next row group's step 0 first. */
        isetp_ge_u32(e, cnt, two);
        const int bra_norm = bra_if_p0_fwd(e);
        imadi(e, rgn2, stride, 0, 1, rg);
        W4_CLAMP(rgn2); W4_PTRS();
        patch_bra(e, bra_norm, here(e));
        if (pf == 2) W4_LOAD(1, xqB, SB_PF_B, 1, wqB, swB, SB_PF_B);   /* step s + 1 flies while step s decodes */
        else W4_LOAD(1, xq, SB_PF_A, 1, wqB, swB, SB_PF_B);             /* x' of step s (an L1/L2 hit), weights of s + 1 */
        if (spec->mutant != 7) wait_sb(e, SB_PF_A);
    } else {
        /* scale addresses first: a fixed-latency op waits on every pending load (emit), so none may sit inside the batch */
        for (uint32_t r = 0; r < R; r++) imadw(e, psr[r], one, r * ROWS_S, ps);
        int first = 1;
        for (uint32_t q = 0; q < XQ; q++) { ldg_wide(e, BW_IR_LDG_E_128, xq[q], pxc, q * 16u, udesc, first); first = 0; }
        for (uint32_t r = 0; r < R; r++) ldg_wide(e, ldw, wq[r], pw, r * ROWB, udesc, 0);
        for (uint32_t r = 0; r < R; r++) ldg32(e, sw[r], psr[r], udesc, 0);
    }
    for (uint32_t r = 0; r < R; r++) {
        movrz(e, dot);
        if (spec->sched) decode_pipe(e, wq[r], WORDS, xq, dot, pt1, pt2, spec->mutant);
        else decode_seq(e, wq[r], WORDS, xq, dot, u, sg, spec->mutant);
        if (spec->mutant == 1) { fadd(e, acc[r], acc[r], dot); continue; }
        shlr(e, t, sw[r], shamt);                             /* the lane's scale byte to bits 31..24 */
        shr(e, t, t, 0, 24);                                  /* E */
        imadi(e, sp, t, 0, 1u << 23, cbias);                  /* S' bits (E + 62) << 23 */
        ffma(e, acc[r], dot, sp, 0, acc[r]);
    }
    if (pf) { /* set B becomes set A: exact 32-bit copies once B has landed */
        wait_sb(e, SB_PF_B);
        for (uint32_t r = 0; r < R; r++) for (uint32_t c = 0; c < LB / 4u; c++) movr(e, wq[r], c, wqB[r], c);
        for (uint32_t r = 0; r < R; r++) movr(e, sw[r], 0, swB[r], 0);
        if (pf == 2) for (uint32_t q = 0; q < XQ; q++) for (uint32_t c = 0; c < 4u; c++) movr(e, xq[q], c, xqB[q], c);
    } else {
        imadw(e, pw, one, 32u * LB, pw);
        imadw(e, pxc, one, 32u * WPL * 4u, pxc);
        imadw(e, ps, one, spec->mutant == 2 ? 0u : GPS, ps);
    }
    imadi(e, cnt, one, 0, 0xffffffffu, cnt);
    isetp_ge_u32(e, cnt, one);
    bra_if_p0(e, L_k);
    loop_end(e);
#undef W4_CLAMP
#undef W4_PTRS
#undef W4_LOAD

    /* ---- warp reduction, store (the FP8 kernel's epilogue) ---- */
    for (uint32_t r = 0; r < R; r++)
        for (uint32_t o = 16; o >= 1; o /= 2) { shfl_down(e, t, acc[r], o); fadd(e, acc[r], acc[r], t); }
    if (!coal) {
        imadw(e, pyr, rg, R * 4u, yrow);
        isetp_ge_u32(e, lane, one);
        const int at_bssy = bssy_fwd(e);
        const int bra_skip = bra_if_p0_fwd(e);
        for (uint32_t r = 0; r < R; r++) { imadw(e, pya, one, r * 4u, pyr); stg(e, pya, 0, acc[r], udesc); }
        const int at_bsync = here(e);
        bsync(e);
        patch_bra(e, bra_skip, at_bsync);
        patch_bra(e, at_bssy, at_bsync + 1);
    } else {
        isetp_ge_u32(e, lane, one);
        int at_bssy = bssy_fwd(e), bra_skip = bra_if_p0_fwd(e);
        imadi(e, sa, slb, 0, 1, so);
        for (uint32_t r = 0; r < R; r++) { if (r) imadi(e, sa, one, 0, 4u, sa); sts32(e, sa, acc[r]); }
        int at_bsync = here(e);
        bsync(e);
        patch_bra(e, bra_skip, at_bsync);
        patch_bra(e, at_bssy, at_bsync + 1);
        bar_sync(e);
        imadi(e, row, uc, 0, R, lane);
        imadi(e, okw, row, 0, 1, negn); shr(e, okw, okw, 0, 31);
        imadi(e, skip, okw, 0, 0xffffffffu, wl1);
        isetp_ge_u32(e, skip, one);
        at_bssy = bssy_fwd(e); bra_skip = bra_if_p0_fwd(e);
        imadi(e, sa, lane, 0, 4u, slb);
        lds32(e, val, sa);
        imadw(e, pya, row, 4u, yrow);
        stg(e, pya, 0, val, udesc);
        at_bsync = here(e);
        bsync(e);
        patch_bra(e, bra_skip, at_bsync);
        patch_bra(e, at_bssy, at_bsync + 1);
        imadi(e, slb, slb, 0, 0xffffffffu, stog);             /* next round: the other slot set (stog - slb) */
        imadi(e, uc, stride, 0, 1, uc);
    }
    imadi(e, rg, stride, 0, 1, rg);
    bra(e, L_rg);
    loop_end(e);
    const int L_end = here(e);
    patch_bra(e, bra_end, L_end);
    if (pf) { wait_sb(e, SB_PF_A); wait_sb(e, SB_PF_B); } /* no load of either set still landing at EXIT */
    tail(e);
    if (e->err) return -1;
    if (omega_bw_regalloc_solve(prog) != 0) return -1;
    return spec->sched ? w4_schedule_stalls(prog) : 0;
}

int omega_w4_gemv_prog_stats(const OmegaW4GemvSpec *spec, uint32_t *insns, uint32_t *gprs) {
    static BlackwellIRProgram prog;
    if (omega_blackwell_codegen_w4_gemv_prog(spec, &prog) != 0) return -1;
    if (insns) *insns = (uint32_t)prog.count;
    if (gprs) *gprs = prog.regalloc.peak_gpr_usage;
    return 0;
}
