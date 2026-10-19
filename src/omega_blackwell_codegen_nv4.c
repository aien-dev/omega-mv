/* omega_blackwell_codegen_nv4.c: grouped NVFP4 GEMV (e2m1 + e4m3 per 16 + f32 per row), flashnext/port.
 * See the header for the contract. The emitter helpers, the scheduling rules and the nibble decode are the W4 kernel's
 * (omega_blackwell_codegen_w4.c, chip-proven at 9fde26ff), repeated here so that file stays untouched; FMUL joins the
 * fixed-latency set of the stall pass (it is fixed latency, as FFMA). */
#include "omega_blackwell_codegen_nv4.h"
#include "omega_blackwell_codegen_w4.h"  /* OMEGA_W4_NIBBLE */
#include "omega_blackwell_codegen_moe.h" /* argument words OMEGA_FP8_ARG_*, OMEGA_FP8_FIXED_LAT */
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
static void stg(Em *e, int addr64, uint32_t imm, int val, int udesc) { BlackwellIRInsn i = I0(BW_IR_STG_E); i.src1_vreg = addr64; i.src2_vreg = val; i.imm = imm; i.ureg = udesc; emit(e, i, K_STORE); }
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
static int nv4_fixed(BlackwellIROpcode op) {
    switch (op) {
    case BW_IR_IMAD: case BW_IR_IMAD_WIDE: case BW_IR_SHF_R: case BW_IR_LOP3_AND: case BW_IR_LOP3_XOR: case BW_IR_MOV_IMM:
    case BW_IR_MOV_RZ: case BW_IR_FFMA: case BW_IR_FADD: case BW_IR_FMUL: case BW_IR_SHF_L_U32: return 1;
    default: return 0;
    }
}
static int nv4_control(BlackwellIROpcode op) {
    return op == BW_IR_BRA || op == BW_IR_EXIT || op == BW_IR_BSSY || op == BW_IR_BSYNC || op == BW_IR_BAR_SYNC;
}
static void nv4_span(const BlackwellIRProgram *p, int v, int *lo, int *cnt) {
    *lo = -1; *cnt = 0;
    if (v < 0 || v >= p->regalloc.num_vregs) return;
    const int ph = p->regalloc.vreg_to_phys[v];
    if (ph < 0) return;
    *lo = ph; *cnt = (int)(p->regalloc.intervals[v].bundle_size ? p->regalloc.intervals[v].bundle_size : 1u);
}
static int nv4_schedule_stalls(BlackwellIRProgram *p) {
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
        const int fixed = nv4_fixed(in->op);
        if (fixed) {
            int lo, c; nv4_span(p, in->dst_vreg, &lo, &c);
            for (int k = 0; k < c; k++) { if (lo + k >= BW_MAX_VREGS + 8) return -1; ready[lo + k] = T + OMEGA_FP8_FIXED_LAT; }
            if (c && T + OMEGA_FP8_FIXED_LAT > all) all = T + OMEGA_FP8_FIXED_LAT;
        }
        uint64_t need = T + (fixed ? 1u : s_emit);
        if (i + 1 < n) {
            const BlackwellIRInsn *nx = &p->insns[i + 1];
            const int ops[4] = { nx->dst_vreg, nx->src1_vreg, nx->src2_vreg, nx->src3_vreg };
            for (int q = 0; q < 4; q++) {
                int lo, c; nv4_span(p, ops[q], &lo, &c);
                for (int k = 0; k < c; k++) if (lo + k < BW_MAX_VREGS + 8 && ready[lo + k] > need) need = ready[lo + k];
            }
            if ((nv4_control(nx->op) || target[i + 1]) && all > need) need = all;
        }
        const uint64_t s = need - T;
        if (s > 15 || (s == 0 && (fixed || s_emit != 0))) return -1; /* stall 0 only where emitted (the final self-branch) */
        if (fixed || s > s_emit) in->control = (in->control & ~(0xfu << 9)) | ((uint32_t)s << 9);
        T = need;
    }
    return 0;
}


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


static void fmul(Em *e, int d, int a, int b) { BlackwellIRInsn i = I0(BW_IR_FMUL); i.dst_vreg = d; i.src1_vreg = a; i.src2_vreg = b; emit(e, i, K_FIXED); }

uint32_t omega_nv4_e4m3_kernel_bits(uint8_t b) { return ((uint32_t)(b & 0x7Fu) << 20) | ((uint32_t)(b & 0x80u) << 24); }
uint64_t omega_nv4_block_bytes(uint32_t n, uint32_t k) { return (uint64_t)n * k / 2u + (uint64_t)n * k / OMEGA_NV4_GROUP + (uint64_t)n * 4u; }

int omega_nv4_gemv_spec_ok(const OmegaNv4GemvSpec *s) {
    if (!s) return 0;
    if (s->rows != 1 && s->rows != 2 && s->rows != 4 && s->rows != 8) return 0;
    if (s->warps < 1 || s->warps > 32) return 0;
    if (s->k < 64u || s->k > OMEGA_NV4_MAX_K || s->k % 64u) return 0;
    if (s->mutant < 0 || s->mutant > 7 || s->sched > 1) return 0;
    if (s->mutant == 7 && s->k % OMEGA_NV4_STEP == 0) return 0; /* no tail to unmask */
    /* mutant 2 (scale pointer stuck) is equivalent to production unless two or more full steps share the pointer: the
     * tail recomputes its scale address from the row base, so with one full step nothing reads the stuck pointer twice */
    if (s->mutant == 2 && s->k / OMEGA_NV4_STEP < 2u) return 0;
    /* 8 rows with a tail: two step bodies of 8 rows need more than the 255 registers a thread can hold (the allocator
     * fails); refused explicitly. No Flash-Next launch uses it: every expert GEMV runs at the automatic 2 rows. */
    if (s->rows == 8u && s->k % OMEGA_NV4_STEP) return 0;
    if ((uint64_t)(s->rows - 1) * (s->k / 2u) + 8u >= (1u << 23)) return 0;
    return 1;
}

/* one lane step for every row: loads (x quads, weights, scale words), decode, scale, accumulate */
typedef struct { int *acc, *wq, *sw, *psr, *xq, dot, u, sg, sp, t, *pt1, *pt2; } StepRegs;
static void step_body(Em *e, const OmegaNv4GemvSpec *spec, const StepRegs *R, int pw, int ps, int pxc, int udesc, int shamt, int cscale) {
    const uint32_t K = spec->k, ROWB = K / 2u, SROW = K / OMEGA_NV4_GROUP;
    (void)ps; (void)SROW; /* the caller set psr[r] = ps + r * SROW before the batch (no fixed-latency op inside it) */
    int first = 1;
    for (uint32_t q = 0; q < 4u; q++) { ldg_wide(e, BW_IR_LDG_E_128, R->xq[q], pxc, q * 16u, udesc, first); first = 0; }
    for (uint32_t r = 0; r < spec->rows; r++) ldg_wide(e, BW_IR_LDG_E_64, R->wq[r], pw, r * ROWB, udesc, 0);
    for (uint32_t r = 0; r < spec->rows; r++) ldg32(e, R->sw[r], R->psr[r], udesc, 0);
    for (uint32_t r = 0; r < spec->rows; r++) {
        movrz(e, R->dot);
        if (spec->sched) decode_pipe(e, R->wq[r], 2u, R->xq, R->dot, R->pt1, R->pt2, spec->mutant);
        else decode_seq(e, R->wq[r], 2u, R->xq, R->dot, R->u, R->sg, spec->mutant);
        if (spec->mutant == 1) { fadd(e, R->acc[r], R->acc[r], R->dot); continue; }
        shlr(e, R->t, R->sw[r], shamt);                 /* the lane's scale byte to bits 31..24 */
        shr(e, R->t, R->t, 0, 24);                      /* b */
        andi(e, R->u, R->t, 0, 0x7Fu);
        imadi(e, R->u, R->u, 0, 1u << 20, NOREG);       /* (b & 0x7f) << 20 */
        andi(e, R->sg, R->t, 0, 0x80u);
        imadi(e, R->sp, R->sg, 0, 1u << 24, R->u);      /* | (b & 0x80) << 24: e4m3 * 2^-120 */
        fmul(e, R->sp, R->sp, cscale);                  /* * 2^120 (or 0 on a masked tail lane) */
        ffma(e, R->acc[r], R->dot, R->sp, 0, R->acc[r]);
    }
}

int omega_blackwell_codegen_nv4_gemv_prog(const OmegaNv4GemvSpec *spec, BlackwellIRProgram *prog) {
    if (!omega_nv4_gemv_spec_ok(spec) || !prog) return -1;
    const uint32_t K = spec->k, R = spec->rows, W = spec->warps;
    const uint32_t ROWB = K / 2u, SROW = K / OMEGA_NV4_GROUP, KS = K / OMEGA_NV4_STEP, TL = (K % OMEGA_NV4_STEP) / OMEGA_NV4_GROUP;
    omega_bw_ir_init(prog);
    Em em; memset(&em, 0, sizeof em); em.p = prog;
    Em *e = &em;

    /* ---- prologue ---- */
    int udesc = UV64(e), ptab = V64(e), px = V64(e), py = V64(e);
    int rgn = V(e), xs = V(e), ys = V(e), stride = V(e);
    int tid = V(e), cx = V(e), cy = V(e), warp = V(e), lane = V(e), one = V(e), gidx = V(e);
    int tent = V64(e), wbase = V64(e), sbase = V64(e), off = V(e), xrow = V64(e), yrow = V64(e), xlane = V64(e), rsb = V64(e);
    int wlane = V(e), slane = V(e), rg = V(e), bsel = V(e), shamt = V(e), c24 = V(e), c120 = V(e);
    int ok = NOREG, lc = NOREG, tmp = NOREG, wlt = NOREG, slt = NOREG, xlt = NOREG, pxt = NOREG, shamt_t = NOREG, c120m = NOREG, ccst = NOREG;
    if (TL) { ok = V(e); lc = V(e); tmp = V(e); wlt = V(e); slt = V(e); xlt = V(e); pxt = V64(e); shamt_t = V(e); c120m = V(e); ccst = V(e); }
    if (e->err) return -1;
    ldcu64(e, udesc, 0x358);
    ldc64(e, ptab, OMEGA_FP8_ARG_TAB); ldc64(e, px, OMEGA_FP8_ARG_X); ldc64(e, py, OMEGA_FP8_ARG_Y);
    ldc32(e, rgn, OMEGA_FP8_ARG_RG); ldc32(e, xs, OMEGA_FP8_ARG_XSTRIDE); ldc32(e, ys, OMEGA_FP8_ARG_YSTRIDE); ldc32(e, stride, OMEGA_FP8_ARG_STRIDE);
    s2r(e, tid, BW_SR_TID_X); s2r(e, cx, BW_SR_CTAID_X); s2r(e, cy, BW_SR_CTAID_Y);
    movi(e, one, 1);
    movi(e, c24, 24);
    movi(e, c120, 0x7B800000u); /* 2^120 */
    shr(e, warp, tid, 0, 5);
    andi(e, lane, tid, 0, 31);
    if (spec->mutant == 3) xorr(e, gidx, cy, one); else imadi(e, gidx, cy, 0, 1, NOREG);
    imadw(e, tent, gidx, 16, ptab);
    ldg_wide(e, BW_IR_LDG_E_64, wbase, tent, 0, udesc, 1);
    ldg_wide(e, BW_IR_LDG_E_64, sbase, tent, 8, udesc, 0);
    imadr(e, off, cy, xs, NOREG); imadw(e, xrow, off, 1, px);
    imadr(e, off, cy, ys, NOREG); imadw(e, yrow, off, 1, py);
    imadw(e, xlane, lane, 16u * 4u, xrow);           /* this lane's 16 x' floats of step 0 */
    imadi(e, wlane, lane, 0, 8u, NOREG);             /* its 8 weight bytes in a row */
    andi(e, slane, lane, 0, 0xFFFFFFFCu);            /* the scale word holding its byte */
    if (spec->mutant == 4) imadi(e, bsel, one, 0, 1, lane); else imadi(e, bsel, lane, 0, 1, NOREG);
    andi(e, bsel, bsel, 0, 3u);
    imadi(e, shamt, bsel, 0, 0xFFFFFFF8u, c24);      /* 24 - 8 b */
    imadi(e, rg, cx, 0, W, warp);
    imadw(e, rsb, rgn, R * SROW, sbase);             /* row scales: s_va + n * k / 16 */
    if (TL) {
        movi(e, ccst, 0u - TL);
        imadi(e, tmp, lane, 0, 1, ccst); shr(e, ok, tmp, 0, 31);       /* ok = lane < TL */
        movi(e, ccst, 0u - (TL - 1u));
        imadi(e, tmp, lane, 0, 1, ccst);                                /* lane - (TL - 1) */
        movi(e, ccst, TL - 1u);
        imadr(e, lc, ok, tmp, ccst);                                    /* lc = ok ? lane : TL - 1 */
        movi(e, ccst, KS * 256u); imadi(e, wlt, lc, 0, 8u, ccst);       /* KS * 256 + lc * 8 */
        andi(e, slt, lc, 0, 0xFFFFFFFCu); movi(e, ccst, KS * 32u); imadi(e, slt, slt, 0, 1, ccst);
        movi(e, ccst, KS * 2048u); imadi(e, xlt, lc, 0, 64u, ccst);
        imadw(e, pxt, xlt, 1, xrow);
        if (spec->mutant == 4) imadi(e, tmp, one, 0, 1, lc); else imadi(e, tmp, lc, 0, 1, NOREG);
        andi(e, tmp, tmp, 0, 3u);
        imadi(e, shamt_t, tmp, 0, 0xFFFFFFF8u, c24);
        if (spec->mutant == 7) imadi(e, c120m, c120, 0, 1, NOREG); else imadi(e, c120m, ok, 0, 0x7B800000u, NOREG);
    }

    int pwr = V64(e), psr0 = V64(e), pw = V64(e), ps = V64(e), pxc = V64(e), pyr = V64(e), pya = V64(e), cnt = V(e);
    int acc[8], wq[8], sw[8], psr[8], xq[4], rsv[8];
    for (uint32_t r = 0; r < R; r++) { acc[r] = V(e); wq[r] = V64(e); sw[r] = V(e); psr[r] = V64(e); rsv[r] = V(e); }
    for (uint32_t q = 0; q < 4u; q++) xq[q] = V128(e);
    int dot = V(e), u = V(e), sg = V(e), sp = V(e), t = V(e);
    int pt1[PIPE_T1], pt2[PIPE_T2];
    if (spec->sched) { for (int q = 0; q < PIPE_T1; q++) pt1[q] = V(e); for (int q = 0; q < PIPE_T2; q++) pt2[q] = V(e); }
    if (e->err) return -1;
    StepRegs SR = { acc, wq, sw, psr, xq, dot, u, sg, sp, t, pt1, pt2 };

    /* ---- row-group loop ---- */
    const int L_rg = here(e);
    loop_begin(e);
    isetp_ge_u32(e, rg, rgn);
    const int bra_end = bra_if_p0_fwd(e);
    imadw(e, pwr, rg, R * ROWB, wbase);
    imadw(e, psr0, rg, R * SROW, sbase);
    for (uint32_t r = 0; r < R; r++) movrz(e, acc[r]);
    if (KS) {
        imadw(e, pw, wlane, 1, pwr);
        imadw(e, ps, slane, 1, psr0);
        imadw(e, pxc, one, 0, xlane);
        movi(e, cnt, KS);
        const int L_k = here(e);
        loop_begin(e);
        for (uint32_t r = 0; r < R; r++) imadw(e, psr[r], one, r * SROW, ps);
        step_body(e, spec, &SR, pw, ps, pxc, udesc, shamt, c120);
        imadw(e, pw, one, 32u * 8u, pw);
        imadw(e, pxc, one, OMEGA_NV4_STEP * 4u, pxc);
        imadw(e, ps, one, spec->mutant == 2 ? 0u : 32u, ps);
        imadi(e, cnt, one, 0, 0xffffffffu, cnt);
        isetp_ge_u32(e, cnt, one);
        bra_if_p0(e, L_k);
        loop_end(e);
    }
    if (TL) {
        imadw(e, pw, wlt, 1, pwr);
        imadw(e, ps, slt, 1, psr0);
        imadw(e, pxc, one, 0, pxt);
        for (uint32_t r = 0; r < R; r++) imadw(e, psr[r], one, r * SROW, ps);
        step_body(e, spec, &SR, pw, ps, pxc, udesc, shamt_t, c120m);
    }

    /* ---- warp reduction, row scale, store ---- */
    for (uint32_t r = 0; r < R; r++)
        for (uint32_t o = 16; o >= 1; o /= 2) { shfl_down(e, t, acc[r], o); fadd(e, acc[r], acc[r], t); }
    if (spec->mutant != 6) {
        imadw(e, pya, rg, R * 4u, rsb);
        for (uint32_t r = 0; r < R; r++) imadw(e, psr[r], one, r * 4u, pya);
        for (uint32_t r = 0; r < R; r++) ldg32(e, rsv[r], psr[r], udesc, r == 0);
        for (uint32_t r = 0; r < R; r++) fmul(e, acc[r], acc[r], rsv[r]);
    }
    imadw(e, pyr, rg, R * 4u, yrow);
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
    if (omega_bw_regalloc_solve(prog) != 0) return -1;
    return spec->sched ? nv4_schedule_stalls(prog) : 0;
}

int omega_nv4_gemv_prog_stats(const OmegaNv4GemvSpec *spec, uint32_t *insns, uint32_t *gprs) {
    static BlackwellIRProgram prog;
    if (omega_blackwell_codegen_nv4_gemv_prog(spec, &prog) != 0) return -1;
    if (insns) *insns = (uint32_t)prog.count;
    if (gprs) *gprs = prog.regalloc.peak_gpr_usage;
    return 0;
}
