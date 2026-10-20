/* omega_blackwell_codegen_ceil.c: ceiling probe kernels (FFMA rate, shared-memory read rate, store stream). See the header. */
#include "omega_blackwell_codegen_ceil.h"
#include "omega_bw_em.h"

int omega_blackwell_codegen_ffma_peak_prog(uint32_t iters, uint32_t chains, uint32_t stall, uint32_t threads, BlackwellIRProgram *prog) {
    if (!prog || iters == 0 || iters > (1u << 24) || chains == 0 || chains > 32 || stall == 0 || stall > 15 || chains * stall < 4 ||
        threads == 0 || threads % 32 || threads > 1024) return -1;
    omega_bw_ir_init(prog);
    Em em; memset(&em, 0, sizeof em); em.p = prog; Em *e = &em;
    int udesc = UV64(e), py = V64(e), tid = V(e), cta = V(e), one = V(e), cnt = V(e), fone = V(e), fadd1 = V(e), acc[32];
    for (uint32_t c = 0; c < chains; c++) acc[c] = V(e);
    if (e->err) return -1;
    ldcu64(e, udesc, 0x358); ldc64(e, py, 0x380); s2r(e, tid, BW_SR_TID_X); s2r(e, cta, BW_SR_CTAID_X);
    movi(e, one, 1); movi(e, fone, 0x3f800000u); movi(e, fadd1, 0x3f800000u); /* b and c in two registers: one register read twice per FFMA can cost a register-bank cycle */
    for (uint32_t c = 0; c < chains; c++) movrz(e, acc[c]);
    movi(e, cnt, iters);
    const int L = here(e);
    loop_begin(e);
    for (uint32_t c = 0; c < chains; c++) {
        BlackwellIRInsn i = I0(BW_IR_FFMA); i.dst_vreg = acc[c]; i.src1_vreg = acc[c]; i.src2_vreg = fone; i.src3_vreg = fadd1;
        i.control = CW(stall, SB_NONE, SB_NONE, e->pending); e->pending = 0;
        if (omega_bw_ir_append(e->p, &i) < 0) e->err = -1;
    }
    imadi(e, cnt, one, 0, 0xffffffffu, cnt);
    isetp_ge_u32(e, cnt, one);
    bra_if_p0(e, L);
    loop_end(e);
    nop(e, 15);
    {   int t = V(e); if (e->err) return -1;
        imadi(e, t, cta, 0, threads, tid); imadw(e, py, t, 4u * chains, py);
        for (uint32_t c = 0; c < chains; c++) stg(e, py, 4u * c, acc[c], udesc);
    }
    tail(e);
    if (e->err) return -1;
    return omega_bw_regalloc_solve(prog);
}

int omega_blackwell_codegen_lds_bw_prog(uint32_t iters, uint32_t threads, BlackwellIRProgram *prog) {
    if (!prog || iters == 0 || threads == 0 || threads % 32 || threads > 512) return -1;
    omega_bw_ir_init(prog);
    Em em; memset(&em, 0, sizeof em); em.p = prog; Em *e = &em;
    int udesc = UV64(e), py = V64(e), tid = V(e), cta = V(e), one = V(e), cnt = V(e), sa = V(e), sv = V(e), la = V(e), acc = V(e), ld[8];
    for (int q = 0; q < 8; q++) ld[q] = V128(e);
    if (e->err) return -1;
    ldcu64(e, udesc, 0x358); ldc64(e, py, 0x380); s2r(e, tid, BW_SR_TID_X); s2r(e, cta, BW_SR_CTAID_X);
    movi(e, one, 1); movrz(e, acc);
    /* word k*threads + tid holds its own index: thread tid writes words tid, tid + threads, ... (32 of them) */
    imadi(e, sa, tid, 0, 4u, NOREG); imadi(e, sv, tid, 0, 1u, NOREG);
    for (uint32_t k = 0; k < 32; k++) {
        sts32(e, sa, sv);
        if (k + 1 < 32) { imadi(e, sa, one, 0, threads * 4u, sa); imadi(e, sv, one, 0, threads, sv); }
    }
    bar_sync(e);
    imadi(e, la, tid, 0, 16u, NOREG);
    movi(e, cnt, iters);
    const int L = here(e);
    loop_begin(e);
    for (int q = 0; q < 8; q++) lds128(e, ld[q], la, (uint32_t)q * threads * 16u, q == 0);
    for (int q = 0; q < 8; q++) for (unsigned s = 0; s < 4; s++) {
        BlackwellIRInsn x = I0(BW_IR_IADD3); x.dst_vreg = acc; x.src1_vreg = acc; x.src2_vreg = ld[q]; x.src2_subreg = (uint8_t)s; emit(e, x, K_FIXED);
    }
    imadi(e, cnt, one, 0, 0xffffffffu, cnt);
    isetp_ge_u32(e, cnt, one);
    bra_if_p0(e, L);
    loop_end(e);
    {   int t = V(e); if (e->err) return -1;
        imadi(e, t, cta, 0, threads, tid); imadw(e, py, t, 4u, py);
        stg(e, py, 0, acc, udesc);
    }
    tail(e);
    if (e->err) return -1;
    return omega_bw_regalloc_solve(prog);
}

int omega_blackwell_codegen_stg_stream_prog(uint32_t iters, uint32_t threads, BlackwellIRProgram *prog) {
    if (!prog || iters == 0 || threads == 0 || threads % 32 || threads > 1024 || (uint64_t)iters * threads * 32u > 0xffffffffu) return -1;
    omega_bw_ir_init(prog);
    Em em; memset(&em, 0, sizeof em); em.p = prog; Em *e = &em;
    int udesc = UV64(e), py = V64(e), tid = V(e), cta = V(e), one = V(e), cnt = V(e), t = V(e);
    if (e->err) return -1;
    ldcu64(e, udesc, 0x358); ldc64(e, py, 0x380); s2r(e, tid, BW_SR_TID_X); s2r(e, cta, BW_SR_CTAID_X);
    movi(e, one, 1);
    imadw(e, py, cta, iters * threads * 32u, py);
    imadi(e, t, tid, 0, 4u, NOREG); imadw(e, py, t, 1, py);
    movi(e, cnt, iters);
    const int L = here(e);
    loop_begin(e);
    for (uint32_t q = 0; q < 8; q++) stg(e, py, q * threads * 4u, cnt, udesc);
    imadw(e, py, one, threads * 32u, py);
    imadi(e, cnt, one, 0, 0xffffffffu, cnt);
    isetp_ge_u32(e, cnt, one);
    bra_if_p0(e, L);
    loop_end(e);
    tail(e);
    if (e->err) return -1;
    return omega_bw_regalloc_solve(prog);
}
