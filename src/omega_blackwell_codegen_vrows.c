/* omega_blackwell_codegen_vrows.c: the batched-verify row copy kernel (OWN-30B W-L4f). See the header. */
#include "omega_blackwell_codegen_vrows.h"
#include "omega_bw_em.h"

uint32_t omega_vrows_src_unit(uint32_t lb, uint32_t q) {
    if (lb == 0) return q;
    const uint32_t U = 8u * lb, nw = lb / 4u, step = q / U, u = q % U;
    return step * U + (u % 32u) * nw + u / 32u;
}

int omega_blackwell_codegen_vrows_prog(const OmegaVrowsSpec *spec, BlackwellIRProgram *prog) {
    if (!spec || !prog || (spec->lane_bytes != 0 && spec->lane_bytes != 8 && spec->lane_bytes != 16)) return -1;
    omega_bw_ir_init(prog);
    Em em; memset(&em, 0, sizeof em); em.p = prog; Em *e = &em;
    const uint32_t lb = spec->mutant == 1 ? 0u : spec->lane_bytes;
    int udesc = UV64(e), pd = V64(e), ps = V64(e), pi = V64(e), units = V(e), sstr = V(e), tid = V(e), cx = V(e), cy = V(e);
    int q = V(e), srow = V(e), su = V(e), t = V(e), t2 = V(e), zero = V(e), sa = V64(e), da = V64(e), ia = V64(e), v = V128(e);
    if (e->err) return -1;
    ldcu64(e, udesc, 0x358);
    ldc64(e, pd, OMEGA_VROWS_DST); ldc64(e, ps, OMEGA_VROWS_SRC); ldc64(e, pi, OMEGA_VROWS_IDX);
    ldc32(e, units, OMEGA_VROWS_UNITS); ldc32(e, sstr, OMEGA_VROWS_SSTRIDE);
    s2r(e, tid, BW_SR_TID_X); s2r(e, cx, BW_SR_CTAID_X); s2r(e, cy, BW_SR_CTAID_Y);
    imadi(e, q, cx, 0, OMEGA_VROWS_THREADS, tid);
    isetp_ge_u32(e, q, units);
    { BlackwellIRInsn x = I0(BW_IR_EXIT); x.predicate_p0 = true; emit(e, x, K_EXIT); }
    movrz(e, zero);
    if (spec->mutant == 1) imadi(e, srow, cy, 0, 1u, NOREG);
    else { imadw(e, ia, cy, 4u, pi); ldg32(e, srow, ia, udesc, 1); }
    if (lb) {
        const uint32_t U = 8u * lb;
        const int lg = log2u(U);
        if (lg < 0) return -1;
        shr(e, t, q, 0, (uint32_t)lg);        /* K step */
        andi(e, t2, q, 0, U - 1u);            /* unit within the step: w * 32 + L */
        andi(e, su, t2, 0, 31u);              /* L */
        shr(e, t2, t2, 0, 5u);                /* w */
        imadi(e, su, su, 0, lb / 4u, t2);     /* L * (LB / 4) + w */
        imadi(e, su, t, 0, U, su);            /* + step * U */
    } else {
        imadi(e, su, q, 0, 1u, NOREG);
    }
    imadr(e, t, srow, sstr, zero);            /* source row byte offset */
    imadi(e, t, su, 0, 16u, t);               /* + unit * 16 */
    imadw(e, sa, t, 1u, ps);
    imadr(e, t2, cy, units, q);               /* destination unit index */
    imadw(e, da, t2, 16u, pd);
    ldg_wide(e, BW_IR_LDG_E_128, v, sa, 0, udesc, 1);
    for (unsigned w = 0; w < 4; w++) {
        BlackwellIRInsn st = I0(BW_IR_STG_E); st.src1_vreg = da; st.src2_vreg = v; st.src2_subreg = (uint8_t)w; st.imm = 4u * w; st.ureg = udesc;
        emit(e, st, K_STORE);
    }
    tail(e);
    if (e->err) return -1;
    return omega_bw_regalloc_solve(prog);
}
