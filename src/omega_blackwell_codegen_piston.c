/* omega_blackwell_codegen_piston.c: the chip piston (L312). See the header.
 * Built from L272's stream probe (origin/feat/decode-onchip-megakernel src/omega_blackwell_codegen_ceil.c: the 8 x 16-byte
 * load body, the software grid barrier, the strong system-scope atomic/load forms) and the R15 seat's clock read
 * (src/runtime/rx_resident_gpu.c: S2R %globaltimer). The helpers are copied, not shared, so neither file changes. */
#include "omega_blackwell_codegen_piston.h"
#include "omega_bw_em.h"

#define PISTON_SR_GLOBALTIMER_LO 0x52u

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
/* spin until *addr >= want (p0 loop: want >= v + 1 means not yet) */
static void spin_ge(Em *e, int v, int vp1, int one, int addr64, int want, int udesc) {
    const int L = here(e);
    loop_begin(e);
    ldg_strong(e, v, addr64, udesc);
    iadd(e, vp1, v, one);
    isetp_ge_u32(e, want, vp1);
    bra_if_p0(e, L);
    loop_end(e);
}

int omega_blackwell_codegen_piston_prog(uint32_t iters, uint32_t threads, uint32_t grid, uint32_t beats, uint32_t slots,
                                        BlackwellIRProgram *prog) {
    if (!prog || iters == 0 || beats == 0 || slots == 0 || threads == 0 || threads % 32 || threads > 1024 || grid == 0) return -1;
    const uint64_t step = (uint64_t)grid * threads * 16u, slot = omega_piston_slot_bytes(iters, threads, grid);
    if (slot * slots > 0xffffffffull || (uint64_t)beats * 8u > 0xffffffffull) return -1;
    omega_bw_ir_init(prog);
    Em em; memset(&em, 0, sizeof em); em.p = prog; Em *e = &em;
    int udesc = UV64(e), px = V64(e), py = V64(e), pc = V64(e), pf = V64(e), pd = V64(e), ps = V64(e), pa = V64(e);
    int tid = V(e), cta = V(e), gid = V(e), one = V(e), off = V(e), soff = V(e), lim = V(e), sp1 = V(e), cnt = V(e), bl = V(e);
    int bt = V(e), bt1 = V(e), acc = V(e), t = V(e), tgt = V(e), v = V(e), vp1 = V(e), clk = V(e), a[8], ld[8];
    for (int q = 0; q < 8; q++) { a[q] = V64(e); ld[q] = V128(e); }
    if (e->err) return -1;
    ldcu64(e, udesc, 0x358); ldc64(e, px, 0x380); ldc64(e, py, 0x388); ldc64(e, pc, 0x390);
    ldc64(e, pf, 0x398); ldc64(e, pd, 0x3a0); ldc64(e, ps, 0x3a8);
    s2r(e, tid, BW_SR_TID_X); s2r(e, cta, BW_SR_CTAID_X);
    movi(e, one, 1); movrz(e, acc); movrz(e, tgt); movrz(e, bt); movrz(e, soff); movi(e, lim, (uint32_t)(slot * slots));
    imadi(e, gid, cta, 0, threads, tid);
    movi(e, bl, beats);
    const int Lb = here(e);
    loop_begin(e);
    iadd(e, bt1, bt, one);
    /* 1. thread 0 of each CTA waits for the host's fill word of this beat */
    isetp_ge_u32(e, tid, one);
    {
        Region r = region_skip_if_p0(e);
        imadw(e, pa, bt, 4u, pf);
        spin_ge(e, v, vp1, one, pa, bt1, udesc);
        region_end(e, r);
    }
    /* 2. fire start stamp */
    isetp_ge_u32(e, gid, one);
    {
        Region r = region_skip_if_p0(e);
        s2r(e, clk, PISTON_SR_GLOBALTIMER_LO);
        imadw(e, pa, bt, 8u, ps);
        stg(e, pa, 0, clk, udesc);
        region_end(e, r);
    }
    bar_sync(e);
    /* 3. stream this beat's slot */
    imadi(e, off, gid, 0, 16u, soff);
    movi(e, cnt, iters);
    const int Li = here(e);
    loop_begin(e);
    for (int q = 0; q < 8; q++) { imadi(e, t, one, 0, (uint32_t)(q * step), off); imadw(e, a[q], t, 1u, px); }
    for (int q = 0; q < 8; q++) ldg_wide(e, BW_IR_LDG_E_128, ld[q], a[q], 0, udesc, q == 0);
    for (int q = 0; q < 8; q++) for (unsigned s = 0; s < 4; s++) { BlackwellIRInsn x = I0(BW_IR_LOP3_XOR); x.dst_vreg = acc; x.src1_vreg = acc; x.src2_vreg = ld[q]; x.src2_subreg = (uint8_t)s; emit(e, x, K_FIXED); }
    imadi(e, off, one, 0, (uint32_t)(8u * step), off);
    dec_loop(e, cnt, one, Li);
    loop_end(e);
    /* 4. software grid barrier (thread 0 of each CTA counts in, then waits for the whole grid of this beat) */
    bar_sync(e);
    isetp_ge_u32(e, tid, one);
    {
        Region r = region_skip_if_p0(e);
        membar(e);
        imadi(e, tgt, one, 0, grid, tgt);
        atom_add(e, v, pc, one, udesc);
        spin_ge(e, v, vp1, one, pc, tgt, udesc);
        region_end(e, r);
    }
    bar_sync(e);
    /* 5. fire done stamp, then release the slot */
    isetp_ge_u32(e, gid, one);
    {
        Region r = region_skip_if_p0(e);
        s2r(e, clk, PISTON_SR_GLOBALTIMER_LO);
        imadw(e, pa, bt, 8u, ps);
        stg(e, pa, 4, clk, udesc);
        membar(e);
        imadw(e, pa, bt, 4u, pd);
        stg(e, pa, 0, bt1, udesc);
        membar(e);
        region_end(e, r);
    }
    /* next slot: soff += slot; wrap to 0 at slots x slot */
    imadi(e, soff, one, 0, (uint32_t)slot, soff);
    iadd(e, sp1, soff, one);
    isetp_ge_u32(e, lim, sp1); /* p0: soff < lim, keep it */
    {
        Region r = region_skip_if_p0(e);
        movrz(e, soff);
        region_end(e, r);
    }
    iadd(e, bt, bt, one);
    dec_loop(e, bl, one, Lb);
    loop_end(e);
    imadw(e, pa, gid, 4u, py);
    stg(e, pa, 0, acc, udesc);
    tail(e);
    if (e->err) return -1;
    return omega_bw_regalloc_solve(prog);
}
