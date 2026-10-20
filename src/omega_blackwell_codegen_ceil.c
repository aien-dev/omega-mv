/* omega_blackwell_codegen_ceil.c: GB10 ceiling microbenchmarks (L272). See the header.
 * The cross-CTA pieces (system-scope atomic add, strong load, MEMBAR before publishing) are the chip-proven forms of the
 * decode attention's last-CTA combine (omega_blackwell_codegen_attn.c: atom_add, membar, LDG_STRONG_SYS). */
#include "omega_blackwell_codegen_ceil.h"
#include "omega_bw_em.h"

static void atom_add(Em *e, int d, int addr64, int val, int udesc) {
    BlackwellIRInsn i = I0(BW_IR_ATOMG_ADD_STRONG_SYS); i.dst_vreg = d; i.src1_vreg = addr64; i.src2_vreg = val; i.ureg = udesc; emit(e, i, K_VAR);
}
static void ldg_strong(Em *e, int d, int addr64, int udesc) {
    BlackwellIRInsn i = I0(BW_IR_LDG_STRONG_SYS); i.dst_vreg = d; i.src1_vreg = addr64; i.ureg = udesc; emit(e, i, K_VAR);
}
static void membar(Em *e) { emit(e, I0(BW_IR_MEMBAR_ALL_SYS), K_FIXED); }
typedef struct { int bssy, bra; } Region;
static Region region_skip_if_p0(Em *e) { Region r; r.bssy = bssy_fwd(e); r.bra = bra_if_p0_fwd(e); return r; }
static void region_end(Em *e, Region r) { int at = here(e); bsync(e); patch_bra(e, r.bra, at); patch_bra(e, r.bssy, at + 1); }
static void dec_loop(Em *e, int cnt, int one, int target) {
    imadi(e, cnt, one, 0, 0xffffffffu, cnt);
    isetp_ge_u32(e, cnt, one);
    bra_if_p0(e, target);
}

int omega_blackwell_codegen_ceil_stream_prog(uint32_t iters, uint32_t threads, uint32_t grid, uint32_t mask, uint32_t phases,
                                             BlackwellIRProgram *prog) {
    if (!prog || iters == 0 || phases == 0 || threads == 0 || threads % 32 || threads > 1024 || grid == 0) return -1;
    const uint64_t step = (uint64_t)grid * threads * 16u;
    if (step * 8u > 0xffffffffull) return -1;
    omega_bw_ir_init(prog);
    Em em; memset(&em, 0, sizeof em); em.p = prog; Em *e = &em;
    int udesc = UV64(e), px = V64(e), py = V64(e), pc = V64(e), base = V(e), tid = V(e), cta = V(e), one = V(e), off = V(e);
    int cnt = V(e), ph = V(e), acc = V(e), t = V(e), tgt = V(e), v = V(e), vp1 = V(e), a[8], ld[8];
    for (int q = 0; q < 8; q++) { a[q] = V64(e); ld[q] = V128(e); }
    if (e->err) return -1;
    ldcu64(e, udesc, 0x358); ldc64(e, px, 0x380); ldc64(e, py, 0x388); ldc64(e, pc, 0x390); ldc32(e, base, 0x398);
    s2r(e, tid, BW_SR_TID_X); s2r(e, cta, BW_SR_CTAID_X);
    movi(e, one, 1); movrz(e, acc); movrz(e, tgt);
    imadi(e, t, cta, 0, threads, tid);
    imadi(e, off, t, 0, 16u, base);
    movi(e, ph, phases);
    const int Lp = here(e);
    loop_begin(e);
    movi(e, cnt, iters);
    const int Li = here(e);
    loop_begin(e);
    for (int q = 0; q < 8; q++) { imadi(e, t, one, 0, (uint32_t)(q * step), off); andi(e, t, t, 0, mask); imadw(e, a[q], t, 1u, px); }
    for (int q = 0; q < 8; q++) ldg_wide(e, BW_IR_LDG_E_128, ld[q], a[q], 0, udesc, q == 0);
    for (int q = 0; q < 8; q++) for (unsigned s = 0; s < 4; s++) { BlackwellIRInsn x = I0(BW_IR_LOP3_XOR); x.dst_vreg = acc; x.src1_vreg = acc; x.src2_vreg = ld[q]; x.src2_subreg = (uint8_t)s; emit(e, x, K_FIXED); }
    imadi(e, off, one, 0, (uint32_t)(8u * step), off);
    dec_loop(e, cnt, one, Li);
    loop_end(e);
    if (phases > 1) {
        /* software grid barrier: every CTA's thread 0 counts in, then waits until all grid CTAs of this phase have */
        bar_sync(e);
        isetp_ge_u32(e, tid, one);
        Region r = region_skip_if_p0(e);
        membar(e);
        imadi(e, tgt, one, 0, grid, tgt);
        atom_add(e, v, pc, one, udesc);
        const int Ls = here(e);
        loop_begin(e);
        ldg_strong(e, v, pc, udesc);
        iadd(e, vp1, v, one);
        isetp_ge_u32(e, tgt, vp1); /* p0: target >= v + 1, i.e. not everyone is in yet */
        bra_if_p0(e, Ls);
        loop_end(e);
        region_end(e, r);
        bar_sync(e);
    }
    dec_loop(e, ph, one, Lp);
    loop_end(e);
    imadi(e, t, cta, 0, threads, tid); imadw(e, a[0], t, 4u, py);
    stg(e, a[0], 0, acc, udesc);
    tail(e);
    if (e->err) return -1;
    return omega_bw_regalloc_solve(prog);
}

int omega_blackwell_codegen_ceil_chase_prog(uint32_t iters, BlackwellIRProgram *prog) {
    if (!prog || iters < 2 || iters % 2) return -1;
    omega_bw_ir_init(prog);
    Em em; memset(&em, 0, sizeof em); em.p = prog; Em *e = &em;
    int udesc = UV64(e), pa = V64(e), pb = V64(e), py = V64(e), one = V(e), cnt = V(e);
    if (e->err) return -1;
    ldcu64(e, udesc, 0x358); ldc64(e, pa, 0x380); ldc64(e, py, 0x388);
    movi(e, one, 1); movi(e, cnt, iters / 2u);
    const int L = here(e);
    loop_begin(e);
    ldg_wide(e, BW_IR_LDG_E_64, pb, pa, 0, udesc, 1);
    ldg_wide(e, BW_IR_LDG_E_64, pa, pb, 0, udesc, 1);
    dec_loop(e, cnt, one, L);
    loop_end(e);
    for (unsigned q = 0; q < 2; q++) { BlackwellIRInsn st = I0(BW_IR_STG_E); st.src1_vreg = py; st.src2_vreg = pa; st.src2_subreg = (uint8_t)q; st.imm = 4u * q; st.ureg = udesc; emit(e, st, K_STORE); }
    tail(e);
    if (e->err) return -1;
    return omega_bw_regalloc_solve(prog);
}

int omega_blackwell_codegen_ceil_resid_prog(uint32_t spin, BlackwellIRProgram *prog) {
    if (!prog || spin == 0) return -1;
    omega_bw_ir_init(prog);
    Em em; memset(&em, 0, sizeof em); em.p = prog; Em *e = &em;
    int udesc = UV64(e), pc = V64(e), py = V64(e), pa = V64(e), tid = V(e), cta = V(e), one = V(e), m1 = V(e), cnt = V(e), v = V(e), old = V(e);
    if (e->err) return -1;
    ldcu64(e, udesc, 0x358); ldc64(e, pc, 0x380); ldc64(e, py, 0x388);
    s2r(e, tid, BW_SR_TID_X); s2r(e, cta, BW_SR_CTAID_X);
    movi(e, one, 1); movi(e, m1, 0xffffffffu);
    isetp_ge_u32(e, tid, one);
    Region r = region_skip_if_p0(e);
    atom_add(e, old, pc, one, udesc);
    movi(e, cnt, spin);
    const int L = here(e);
    loop_begin(e);
    ldg_strong(e, v, pc, udesc);
    dec_loop(e, cnt, one, L);
    loop_end(e);
    imadw(e, pa, cta, 4u, py);
    stg(e, pa, 0, v, udesc);
    atom_add(e, old, pc, m1, udesc);
    region_end(e, r);
    tail(e);
    if (e->err) return -1;
    return omega_bw_regalloc_solve(prog);
}

int omega_blackwell_codegen_ceil_empty_prog(BlackwellIRProgram *prog) {
    if (!prog) return -1;
    omega_bw_ir_init(prog);
    Em em; memset(&em, 0, sizeof em); em.p = prog; Em *e = &em;
    tail(e);
    if (e->err) return -1;
    return omega_bw_regalloc_solve(prog);
}
