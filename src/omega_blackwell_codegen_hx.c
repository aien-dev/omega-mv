/* omega_blackwell_codegen_hx.c: W8A16 activation kernels (W-L1H). See the header. */
#include "omega_blackwell_codegen_hx.h"
#include "omega_bw_em.h"

#define F_2P64 0x5f800000u   /* 2^64 */
#define F_NLOG2E 0xbfb8aa3bu /* -log2(e), the W8A8 epilogue's constant */
#define F_125 0x42fa0000u    /* 125.0f cap (subnormal MUFU.RCP results flush on the chip, see omega_blackwell_codegen_qg.c) */
#define F_ONE 0x3f800000u

int omega_hx_spec_ok(const OmegaHxSpec *s) {
    if (!s || s->mode > OMEGA_HX_SWIGLU_NAT) return 0;
    if (s->k < 128u || s->k > 4096u || s->k % 128u) return 0;
    if (s->in_type > OMEGA_HX_IN_BF16 || (s->mode != OMEGA_HX_CONVERT && s->in_type != OMEGA_HX_IN_F32)) return 0;
    return s->mutant >= 0 && s->mutant <= 2;
}

static void fop(Em *e, BlackwellIROpcode op, int d, unsigned dsub, int a, unsigned asub, int b, unsigned bsub) {
    BlackwellIRInsn i = I0(op); i.dst_vreg = d; i.dst_subreg = (uint8_t)dsub; i.src1_vreg = a; i.src1_subreg = (uint8_t)asub;
    i.src2_vreg = b; i.src2_subreg = (uint8_t)bsub; emit(e, i, K_FIXED);
}
static void mufu(Em *e, BlackwellIROpcode op, int d, int a) { BlackwellIRInsn i = I0(op); i.dst_vreg = d; i.src1_vreg = a; emit(e, i, K_VAR); }
/* F2FP.BF16.F32.PACK_AB placeholder (omega_blackwell_codegen_tc.h): d = bf16_rn(hi) << 16 | bf16_rn(lo) */
static void pack(Em *e, int d, int v, unsigned hi, unsigned lo) {
    BlackwellIRInsn i = I0(BW_IR_FMNMX_MAX); i.dst_vreg = d; i.src1_vreg = v; i.src1_subreg = (uint8_t)hi; i.src2_vreg = v; i.src2_subreg = (uint8_t)lo;
    i.imm = OMEGA_TC_MARK_F2FP; emit(e, i, K_FIXED);
}

int omega_blackwell_codegen_hx_prog(const OmegaHxSpec *spec, BlackwellIRProgram *prog) {
    if (!omega_hx_spec_ok(spec) || !prog) return -1;
    const uint32_t K = spec->k;
    omega_bw_ir_init(prog);
    Em em; memset(&em, 0, sizeof em); em.p = prog; Em *e = &em;
    int udesc = UV64(e), pin = V64(e), pout = V64(e), tid = V(e), row = V(e), v = V128(e), w = V64(e), s64 = V(e), p0 = V(e), p1 = V(e);
    if (e->err) return -1;
    ldcu64(e, udesc, 0x358); ldc64(e, pin, 0x380); ldc64(e, pout, 0x388);
    s2r(e, tid, BW_SR_TID_X); s2r(e, row, BW_SR_CTAID_X);
    const int nat = spec->mode == OMEGA_HX_SWIGLU_NAT;
    movi(e, s64, spec->mutant == 2 || nat ? F_ONE : F_2P64);
    if (spec->mode == OMEGA_HX_CONVERT) {
        if (spec->in_type == OMEGA_HX_IN_F32) { imadw(e, pin, row, K * 4u, pin); imadw(e, pin, tid, 16u, pin); ldg_wide(e, BW_IR_LDG_E_128, v, pin, 0, udesc, 1); }
        else {
            imadw(e, pin, row, K * 2u, pin); imadw(e, pin, tid, 8u, pin); ldg_wide(e, BW_IR_LDG_E_64, w, pin, 0, udesc, 1);
            for (unsigned j = 0; j < 2; j++) { /* bf16 pairs -> f32 (element 2j in the low half of word j), as the W8A8 quantiser */
                BlackwellIRInsn lo = I0(BW_IR_IMAD); lo.dst_vreg = v; lo.dst_subreg = (uint8_t)(2u * j); lo.src1_vreg = w; lo.src1_subreg = (uint8_t)j; lo.imm = 65536u; lo.src3_vreg = NOREG; emit(e, lo, K_FIXED);
                BlackwellIRInsn hi = I0(BW_IR_LOP3_AND); hi.dst_vreg = v; hi.dst_subreg = (uint8_t)(2u * j + 1u); hi.src1_vreg = w; hi.src1_subreg = (uint8_t)j; hi.imm = 0xffff0000u; emit(e, hi, K_FIXED);
            }
        }
    } else {
        int u = V128(e), kf = V(e), c125 = V(e), onef = V(e), t = V(e), ex = V(e);
        if (e->err) return -1;
        const int G = spec->mutant == 1 ? u : v, U = spec->mutant == 1 ? v : u;
        imadw(e, pin, row, 2u * K * 4u, pin); imadw(e, pin, tid, 16u, pin);
        ldg_wide(e, BW_IR_LDG_E_128, v, pin, 0, udesc, 1);
        ldg_wide(e, BW_IR_LDG_E_128, u, pin, K * 4u, udesc, 0);
        movi(e, kf, F_NLOG2E); movi(e, c125, F_125); movi(e, onef, F_ONE);
        for (unsigned q = 0; q < 4; q++) {
            fop(e, BW_IR_FMUL, t, 0, G, q, kf, 0);
            fop(e, BW_IR_FMNMX_MIN, t, 0, t, 0, c125, 0);
            mufu(e, BW_IR_MUFU_EX2, ex, t);
            fop(e, BW_IR_FADD, ex, 0, ex, 0, onef, 0);
            mufu(e, BW_IR_MUFU_RCP, ex, ex);
            fop(e, BW_IR_FMUL, t, 0, G, q, ex, 0);
            fop(e, BW_IR_FMUL, v == G ? u : v, q, t, 0, U, q); /* o into the register the loop no longer reads */
        }
        if (spec->mutant != 1) { /* results live in u: move them to v for the common store below */
            for (unsigned q = 0; q < 4; q++) fop(e, BW_IR_FMUL, v, q, u, q, onef, 0);
        }
    }
    if (!nat) for (unsigned q = 0; q < 4; q++) fop(e, BW_IR_FMUL, v, q, v, q, s64, 0);
    if (nat || (spec->mutant == 1 && spec->mode == OMEGA_HX_CONVERT)) { pack(e, p0, v, 1, 0); pack(e, p1, v, 3, 2); }
    else { pack(e, p0, v, 2, 0); pack(e, p1, v, 3, 1); }
    imadw(e, pout, row, K * 2u, pout); imadw(e, pout, tid, 8u, pout);
    stg(e, pout, 0, p0, udesc);
    stg(e, pout, 4, p1, udesc);
    tail(e);
    if (e->err) return -1;
    return omega_bw_regalloc_solve(prog);
}
