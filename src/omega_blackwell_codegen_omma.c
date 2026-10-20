/* omega_blackwell_codegen_omma.c: 4-bit e2m1 tensor-core placeholders, patch and peak kernel (L284 own-fp4). See the header. */
#include "omega_blackwell_codegen_omma.h"
#include "omega_bw_em.h"
#include "omega_bw_em_q.h"
#include "omega_gpu_fmt.h"

static uint32_t rd32o(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr32o(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

/* oracle opcode low 16 bits and word-2 bits above the Rc byte, per variant (index = OMMA_V_*) */
static const uint32_t op16[OMMA_V_N] = { 0x727au, 0x727au, 0x747au, 0x747fu, 0x747fu };
static const uint32_t w2hi[OMMA_V_N] = { 0x00002c00u, 0x0028ec00u, 0x0028fe00u, 0x00083e00u, 0x00043e00u };

static int omma_variant(const BlackwellIRInsn *in) {
    if (!in || in->op != BW_IR_HMMA_BF16 || in->imm < OMEGA_OMMA_MARK_BASE + OMMA_V_E2M1 || in->imm >= OMEGA_OMMA_MARK_BASE + OMMA_V_N) return -1;
    return (int)(in->imm - OMEGA_OMMA_MARK_BASE);
}

int omega_omma_patch_code(const BlackwellIRProgram *prog, int scale_vreg, uint8_t *code, size_t len) {
    if (!prog || !code || len < prog->count * 16u) return -1;
    int n = 0;
    for (size_t i = 0; i < prog->count; i++) {
        int v = omma_variant(&prog->insns[i]);
        if (v < 0) continue;
        uint8_t *w = code + i * 16u;
        uint32_t w0 = rd32o(w), w1 = rd32o(w + 4), w2 = rd32o(w + 8), w3 = rd32o(w + 12);
        if ((w0 & 0xffffu) != 0x723cu || (w1 & 0xffffff00u) != 0 || (w2 & 0xffffff00u) != 0x00041800u || (w3 & 0x1ffu) != 0) return -1;
        wr32o(w, (w0 & 0xffff0000u) | op16[v]);
        if (v != OMMA_V_E2M1) {
            if (scale_vreg < 0 || scale_vreg >= BW_MAX_VREGS) return -1;
            int s = prog->regalloc.vreg_to_phys[scale_vreg];
            if (s < 0 || s > 254) return -1;
            wr32o(w + 4, 0x70000000u | (uint32_t)s << 20 | (uint32_t)s << 8 | (w1 & 0xffu));
        }
        wr32o(w + 8, (w2 & 0xffu) | w2hi[v]);
        n++;
    }
    return n;
}

static void omma_emit(Em *e, int variant, int d, int a, int b, int c, uint32_t stall) {
    if (variant == OMMA_V_E4M3) { qmma(e, d, a, b, c, stall); return; }
    BlackwellIRInsn i = I0(BW_IR_HMMA_BF16); i.dst_vreg = d; i.src1_vreg = a; i.src2_vreg = b; i.src3_vreg = c; i.imm = OMEGA_OMMA_MARK_BASE + (uint32_t)variant;
    i.control = CW(stall, SB_NONE, SB_NONE, e->pending); e->pending = 0;
    if (omega_bw_ir_append(e->p, &i) < 0) e->err = -1;
}

int omega_omma_patch_selftest(char *msg, size_t msgn) {
    static BlackwellIRProgram p; static uint8_t buf[64 * 16]; size_t len = 0;
    omega_bw_ir_init(&p);
    Em em; memset(&em, 0, sizeof em); em.p = &p; Em *e = &em;
    int udesc = UV64(e), py = V64(e), a = V128(e), b = V64(e), c = V128(e), s = V(e);
    if (e->err) { omega_gpu_fmt(msg, msgn, "vregs"); return -1; }
    ldcu64(e, udesc, 0x358); ldc64(e, py, 0x380); movi(e, s, 0x7f7f7f7fu);
    ldg_wide(e, BW_IR_LDG_E_128, a, py, 0, udesc, 1); ldg_wide(e, BW_IR_LDG_E_64, b, py, 16, udesc, 0); ldg_wide(e, BW_IR_LDG_E_128, c, py, 32, udesc, 0);
    for (int v = OMMA_V_E2M1; v < OMMA_V_N; v++) { omma_emit(e, v, c, a, b, c, 15); nop(e, 15); }
    hmma(e, c, a, b, c, 15); nop(e, 15);
    for (unsigned q = 0; q < 4; q++) stg(e, py, 4u * q, c, udesc);
    stg(e, py, 64, s, udesc);
    tail(e);
    if (e->err || omega_bw_regalloc_solve(&p) != 0 || omega_bw_encode_program(&p, buf, sizeof buf, &len) != 0) { omega_gpu_fmt(msg, msgn, "build/encode failed"); return -1; }
    int np = omega_omma_patch_code(&p, s, buf, len);
    if (np != OMMA_V_N - 1) { omega_gpu_fmt(msg, msgn, "patched %d placeholders, expected %d", np, OMMA_V_N - 1); return -1; }
    int sp = p.regalloc.vreg_to_phys[s], seen = 0, seenh = 0;
    for (size_t i = 0; i < p.count; i++) {
        if (p.insns[i].op != BW_IR_HMMA_BF16) continue;
        const uint8_t *w = buf + i * 16u; uint32_t w0 = rd32o(w), w1 = rd32o(w + 4), w2 = rd32o(w + 8);
        int v = omma_variant(&p.insns[i]);
        if (v < 0) { if ((w0 & 0xffffu) != 0x723cu) { omega_gpu_fmt(msg, msgn, "HMMA word %zu changed", i); return -1; } seenh++; continue; }
        uint32_t w1want = v == OMMA_V_E2M1 ? 0u : 0x70000000u | (uint32_t)sp << 20 | (uint32_t)sp << 8;
        /* oracle words with Rd/Ra/Rb/Rc masked: w0 low 16 = opcode, w1 above Rb = scale regs + URZ, w2 above Rc = type bits */
        if ((w0 & 0xffffu) != op16[v] || (w1 & 0xffffff00u) != w1want || (w2 & 0xffffff00u) != w2hi[v]) {
            omega_gpu_fmt(msg, msgn, "variant %d word %zu: %08x %08x %08x", v, i, w0, w1, w2); return -1; }
        seen++;
    }
    if (seen != OMMA_V_N - 1 || seenh != 1) { omega_gpu_fmt(msg, msgn, "saw %d 4-bit ops, %d HMMA", seen, seenh); return -1; }
    omega_gpu_fmt(msg, msgn, "E2M1 QMMA, QMMA.SF E8, OMMA.SF E8 2X, OMMA.SF UE4M3 4X match the ptxas oracle bits (scale reg R%d), HMMA untouched", sp);
    return 0;
}

int omega_blackwell_codegen_omma_peak_prog(int variant, uint32_t data_word, uint32_t scale_word, uint32_t iters, uint32_t chains,
                                           uint32_t stall, uint32_t threads, BlackwellIRProgram *prog, int *scale_vreg_out) {
    if (!prog || !scale_vreg_out || variant < 0 || variant >= OMMA_V_N || iters == 0 || chains == 0 || chains > 16 || stall == 0 || stall > 15 ||
        threads == 0 || threads % 32 || threads > 1024) return -1;
    omega_bw_ir_init(prog);
    Em em; memset(&em, 0, sizeof em); em.p = prog; Em *e = &em;
    int udesc = UV64(e), py = V64(e), tid = V(e), cta = V(e), one = V(e), cnt = V(e), s = V(e), a = V128(e), b = V64(e), acc[16];
    for (uint32_t c = 0; c < chains; c++) acc[c] = V128(e);
    if (e->err) return -1;
    ldcu64(e, udesc, 0x358); ldc64(e, py, 0x380); s2r(e, tid, BW_SR_TID_X); s2r(e, cta, BW_SR_CTAID_X);
    movi(e, one, 1); movi(e, s, scale_word);
    for (unsigned q = 0; q < 4; q++) { BlackwellIRInsn m = I0(BW_IR_MOV_IMM); m.dst_vreg = a; m.dst_subreg = (uint8_t)q; m.imm = data_word; emit(e, m, K_FIXED); }
    for (unsigned q = 0; q < 2; q++) { BlackwellIRInsn m = I0(BW_IR_MOV_IMM); m.dst_vreg = b; m.dst_subreg = (uint8_t)q; m.imm = data_word; emit(e, m, K_FIXED); }
    for (uint32_t c = 0; c < chains; c++) for (unsigned q = 0; q < 4; q++) { BlackwellIRInsn mz = I0(BW_IR_MOV_RZ); mz.dst_vreg = acc[c]; mz.dst_subreg = (uint8_t)q; emit(e, mz, K_FIXED); }
    movi(e, cnt, iters);
    const int L = here(e);
    loop_begin(e);
    for (uint32_t c = 0; c < chains; c++) omma_emit(e, variant, acc[c], a, b, acc[c], stall);
    imadi(e, cnt, one, 0, 0xffffffffu, cnt);
    isetp_ge_u32(e, cnt, one);
    bra_if_p0(e, L);
    loop_end(e);
    nop(e, 15); nop(e, 15); nop(e, 15); nop(e, 15);
    {   int t = V(e); if (e->err) return -1;
        imadi(e, t, cta, 0, threads, tid); imadw(e, py, t, 16u * chains + 16u, py);
        for (uint32_t c = 0; c < chains; c++) for (unsigned q = 0; q < 4; q++) { BlackwellIRInsn st = I0(BW_IR_STG_E); st.src1_vreg = py; st.src2_vreg = acc[c]; st.src2_subreg = (uint8_t)q; st.imm = c * 16u + 4u * q; st.ureg = udesc; emit(e, st, K_STORE); }
        /* keeps the scale register live across the loop (the patch names it); the test checks it reads back */
        stg(e, py, chains * 16u, s, udesc);
    }
    tail(e);
    if (e->err) return -1;
    *scale_vreg_out = s;
    return omega_bw_regalloc_solve(prog);
}
