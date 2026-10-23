/* omega_blackwell_codegen_tgemm.c: device-resident bf16 training GEMM (trainer lane E1; cut 1 NN / F32, cut 2 NT, TN, F32
 * accumulate, BF16 output, split-K reduce). See the header. The K loop, staging and scheduling follow
 * omega_blackwell_codegen_tc2.c (chip gate OWN30B-L1G-GEMM-1e76e728). */
#include "omega_blackwell_codegen_tgemm.h"
#include "omega_bw_em.h"

static int a_t(const OmegaTgemmSpec *s) { return s->layout == OMEGA_TGEMM_LAYOUT_TN; }
static int b_t(const OmegaTgemmSpec *s) { return s->layout == OMEGA_TGEMM_LAYOUT_NT; }
static uint32_t esize(uint32_t epi) { return epi == OMEGA_TGEMM_EPI_BF16 ? 2u : 4u; }

int omega_tgemm_spec_ok(const OmegaTgemmSpec *s) {
    if (!s) return 0;
    if (s->k < 128u || s->k > 65536u || s->k % 128u) return 0;
    if (s->rt != 2 && s->rt != 4 && s->rt != 8) return 0;
    if (s->ct != 2) return 0; /* the paired column tiles share one B word per lane (NN / TN) and one lane row of output */
    if (s->warps != 8 && s->warps != 16) return 0;
    if (s->kc != 32) return 0;
    if (s->aqn != 1 && s->aqn != 2) return 0;
    if (s->layout > OMEGA_TGEMM_LAYOUT_TN || s->epi > OMEGA_TGEMM_EPI_BF16 || s->tfast > 1u) return 0;
    const uint32_t span = s->warps * 8u * s->ct, T = 32u * s->warps, BM = 16u * s->rt;
    if (s->n == 0 || s->n % span || s->n >= (1u << 17)) return 0;
    if (s->m == 0 || s->m % BM || s->m >= (1u << 17)) return 0;
    if (a_t(s)) {
        /* a thread stages whole (k0, k0 + 1) pairs of 4 consecutive m: BM * kc / 8 pairs per chunk over T threads */
        const uint32_t MG = BM / 4u, npair = BM * s->kc / (8u * T), KPJ = T / MG;
        if ((BM * s->kc) % (8u * T) || npair == 0 || log2u(MG) < 0 || KPJ % 4u) return 0;
        if ((uint64_t)(npair - 1u) * 4u * KPJ * s->m + 2u * s->m >= (1u << 23)) return 0; /* constant load offsets */
        if ((uint64_t)s->k * s->m >= (1ull << 32)) return 0;                                /* 32-bit element arithmetic */
    } else {
        if ((BM * (s->kc / 4u)) % T) return 0; /* whole LDG.64 staging pieces per thread */
        /* the pieces of one thread are RJ rows apart: constant load offsets that must stay under the encoder's 2^23 */
        const uint32_t RJ = T / (s->kc / 4u), nj = BM * (s->kc / 4u) / T;
        if (RJ % 16u) return 0;
        if ((uint64_t)(nj - 1u) * RJ * s->k * 2u + (128u / s->kc - 1u) * s->kc * 2u >= (1u << 23)) return 0;
    }
    if ((uint64_t)s->k * s->n >= (1ull << 32)) return 0; /* B start, 32-bit element arithmetic */
    const uint32_t shared = 2u * BM * s->kc * 2u, es = esize(s->epi), wpr = span * es / 4u;
    if (shared > 16384u) return 0;         /* QMD shared cap */
    if (8u * span * es > shared) return 0; /* an 8-row output pass must fit the staging buffers */
    if ((8u * wpr) % T || log2u(wpr) < 0) return 0;
    if (s->mutant < 0 || s->mutant > 7) return 0;
    return 1;
}
uint32_t omega_tgemm_shared_bytes(const OmegaTgemmSpec *s) { return omega_tgemm_spec_ok(s) ? 2u * 16u * s->rt * s->kc * 2u : 0u; }
uint32_t omega_tgemm_span(const OmegaTgemmSpec *s) { return omega_tgemm_spec_ok(s) ? s->warps * 8u * s->ct : 0u; }
uint32_t omega_tgemm_bm(const OmegaTgemmSpec *s) { return omega_tgemm_spec_ok(s) ? 16u * s->rt : 0u; }

/* d = lut(a[asub], b[bsub], c) */
static void lop3s(Em *e, int d, int a, unsigned asub, int b, unsigned bsub, int c, uint32_t lut) {
    BlackwellIRInsn i = I0(BW_IR_LOP3_LUT); i.dst_vreg = d; i.src1_vreg = a; i.src1_subreg = (uint8_t)asub; i.src2_vreg = b; i.src2_subreg = (uint8_t)bsub; i.src3_vreg = c; i.imm = lut; emit(e, i, K_FIXED);
}
/* d[dsub] = a[asub] * imm + c */
static void imadi_ds(Em *e, int d, unsigned dsub, int a, unsigned asub, uint32_t imm, int c) {
    BlackwellIRInsn i = I0(BW_IR_IMAD); i.dst_vreg = d; i.dst_subreg = (uint8_t)dsub; i.src1_vreg = a; i.src1_subreg = (uint8_t)asub; i.imm = imm; i.src3_vreg = c; emit(e, i, K_FIXED);
}
/* d[dsub] = a[asub] >> imm (SHF.R.U32.HI) */
static void shr_ds(Em *e, int d, unsigned dsub, int a, unsigned asub, uint32_t imm) {
    BlackwellIRInsn i = I0(BW_IR_SHF_R); i.dst_vreg = d; i.dst_subreg = (uint8_t)dsub; i.src1_vreg = a; i.src1_subreg = (uint8_t)asub; i.imm = imm; emit(e, i, K_FIXED);
}
/* d[dsub] = a[asub] + b */
static void iadd_d(Em *e, int d, unsigned dsub, int a, unsigned asub, int b) {
    BlackwellIRInsn i = I0(BW_IR_IADD3); i.dst_vreg = d; i.dst_subreg = (uint8_t)dsub; i.src1_vreg = a; i.src1_subreg = (uint8_t)asub; i.src2_vreg = b; emit(e, i, K_FIXED);
}
/* d[dsub] = a[asub] + b[bsub] (FADD, round to nearest) */
static void fadd_s(Em *e, int d, unsigned dsub, int a, unsigned asub, int b, unsigned bsub) {
    BlackwellIRInsn i = I0(BW_IR_FADD); i.dst_vreg = d; i.dst_subreg = (uint8_t)dsub; i.src1_vreg = a; i.src1_subreg = (uint8_t)asub; i.src2_vreg = b; i.src2_subreg = (uint8_t)bsub; emit(e, i, K_FIXED);
}
/* d[dsub] = bf16_rn(hi) << 16 | bf16_rn(lo): the F2FP.BF16.F32.PACK_AB placeholder (omega_blackwell_codegen_tc.h) */
static void f2fp_s(Em *e, int d, unsigned dsub, int hi, unsigned hisub, int lo, unsigned losub) {
    BlackwellIRInsn i = I0(BW_IR_FMNMX_MAX); i.dst_vreg = d; i.dst_subreg = (uint8_t)dsub; i.src1_vreg = hi; i.src1_subreg = (uint8_t)hisub; i.src2_vreg = lo; i.src2_subreg = (uint8_t)losub; i.imm = OMEGA_TC_MARK_F2FP; emit(e, i, K_FIXED);
}
static void sts32_sub(Em *e, int addr, int val, unsigned sub) {
    BlackwellIRInsn st = I0(BW_IR_STS32); st.src1_vreg = addr; st.src2_vreg = val; st.src2_subreg = (uint8_t)sub; emit(e, st, K_STORE);
}
static void stg_sub(Em *e, int addr64, uint32_t imm, int val, unsigned sub, int udesc) {
    BlackwellIRInsn i = I0(BW_IR_STG_E); i.src1_vreg = addr64; i.src2_vreg = val; i.src2_subreg = (uint8_t)sub; i.imm = imm; i.ureg = udesc; emit(e, i, K_STORE);
}

int omega_blackwell_codegen_tgemm_prog(const OmegaTgemmSpec *spec, BlackwellIRProgram *prog) {
    if (!omega_tgemm_spec_ok(spec) || !prog) return -1;
    const int AT = a_t(spec), BT = b_t(spec), ACC = spec->epi == OMEGA_TGEMM_EPI_F32_ACC, BF = spec->epi == OMEGA_TGEMM_EPI_BF16;
    const uint32_t K = spec->k, N = spec->n, M = spec->m, RT = spec->rt, CT = spec->ct, W = spec->warps, T = 32u * W, KC = spec->kc, AQN = spec->aqn;
    const uint32_t BM = 16u * RT, SPAN = W * 8u * CT, NS = KC / 16u /* HMMA steps per chunk */, NCH = 128u / KC /* chunks per block */;
    const uint32_t PPR = KC / 4u /* LDG.64 pieces per row of a chunk (NN / NT) */, NJ = BM * PPR / T, BUF = BM * KC * 2u;
    const uint32_t MG = BM / 4u, NPAIR = BM * KC / (8u * T), KPJ = T / MG; /* TN staging */
    const uint32_t ES = esize(spec->epi), WPR = SPAN * ES / 4u /* output words per pass row */;
    const uint32_t BSTEP = BT ? KC * 2u : KC * N * 2u /* B bytes per chunk */, ASTEP = AT ? KC * M * 2u : 0u /* A bytes per chunk (TN) */;
    omega_bw_ir_init(prog);
    Em em; memset(&em, 0, sizeof em); em.p = prog;
    Em *e = &em;

    /* ---- prologue: arguments, ids, tile entry ---- */
    int udesc = UV64(e), ptab = V64(e), pa = V64(e), pbb = V64(e);
    int tid = V(e), cb = V(e) /* column block */, tl = V(e) /* tile */, warp = V(e), lane = V(e), g = V(e), tq = V(e), one = V(e), tent = V64(e);
    int rk = V64(e) /* row0, kb0 */, nkb = V64(e) /* nkb, pad */;
    int n0w = V(e), tmp = V(e), tmp2 = V(e), pb = V64(e), la = V(e), msel = BT ? NOREG : V(e);
    if (e->err) return -1;
    ldcu64(e, udesc, 0x358);
    ldc64(e, ptab, 0x380); ldc64(e, pa, 0x388); ldc64(e, pbb, 0x390);
    s2r(e, tid, BW_SR_TID_X); s2r(e, spec->tfast ? tl : cb, BW_SR_CTAID_X); s2r(e, spec->tfast ? cb : tl, BW_SR_CTAID_Y);
    movi(e, one, 1);
    shr(e, warp, tid, 0, 5); andi(e, lane, tid, 0, 31); shr(e, g, lane, 0, 2); andi(e, tq, lane, 0, 3);
    imadw(e, tent, tl, OMEGA_TGEMM_TILE_BYTES, ptab);
    ldg_wide(e, BW_IR_LDG_E_64, rk, tent, 8, udesc, 1);
    ldg_wide(e, BW_IR_LDG_E_64, nkb, tent, 16, udesc, 0);
    /* this warp's first output column n0w */
    imadi(e, n0w, cb, 0, SPAN, NOREG); imadi(e, tmp, warp, 0, 8u * CT, NOREG); iadd(e, n0w, n0w, tmp);
    if (BT) {
        /* B fragment pointer (Bt[N][K]): row n0w + 2 g (tile E; tile O is one row further), column kb0 * 128 + 8 t */
        imadi(e, tmp, g, 0, 2u, n0w); imadi(e, tmp, tmp, 0, K, NOREG);
        imadi(e, tmp2, rk, 1, 128u, NOREG); imadi(e, tmp2, tq, 0, 8u, tmp2); iadd(e, tmp, tmp, tmp2); imadw(e, pb, tmp, 2u, pbb);
    } else {
        /* B fragment pointer (B[K][N]): row kb0 * 128 + 8 t, column n0w + 4 (g / 2) */
        imadi(e, tmp, rk, 1, 128u, NOREG); imadi(e, tmp, tq, 0, 8u, tmp); imadi(e, tmp, tmp, 0, N, n0w);
        shr(e, tmp2, g, 0, 1); imadi(e, tmp, tmp2, 0, 4u, tmp); imadw(e, pb, tmp, 2u, pbb);
        /* half-word select mask: odd g takes the high word of its 8 B row bytes (mutant 4 swaps the parity) */
        andi(e, tmp, g, 0, 1); if (spec->mutant == 4) imadi(e, tmp, tmp, 0, 0xffffffffu, one);
        imadi(e, msel, tmp, 0, 0xffffffffu, NOREG);
    }
    imadi(e, la, lane, 0, 16u, NOREG);

    /* ---- staging assignment ---- */
    int gx = V64(e), sa = V(e), r = V(e), pp = V(e), hh = V(e);
    if (e->err) return -1;
    uint32_t GXJ = 0, SAJ = 0, ROWB = 0;
    if (AT) {
        /* thread: m group mg = tid % MG (m = row0 + 4 mg .. + 3), k pair kp = tid / MG (+ jj KPJ): rows k0 = 2 kp and k0 + 1 of At */
        GXJ = 2u * KPJ * M * 2u; SAJ = KPJ * 4u; ROWB = M * 2u;
        andi(e, r, tid, 0, MG - 1u) /* mg */; shr(e, pp, tid, 0, (uint32_t)log2u(MG)) /* kp */;
        /* global: a + ((kb0 * 128 + 2 kp) * M + row0 + 4 mg) * 2 */
        imadi(e, tmp, rk, 1, 128u, NOREG); imadi(e, tmp, pp, 0, 2u, tmp); imadi(e, tmp, tmp, 0, M, NOREG); iadd(e, tmp, tmp, rk);
        imadi(e, tmp, r, 0, 4u, tmp); imadw(e, gx, tmp, 2u, pa);
        /* shared slot of (m = 4 mg, k0 = 2 kp): row tile mg / 4, g = 4 (mg % 2) (+ u for m + u), a-slot (mg / 2) % 2,
         * t = kp / 4, step (kp / 2) % 2, position kp % 2: (i NS + s) 512 + 64 g + 16 t + 8 pos + 4 aslot */
        shr(e, tmp, r, 0, 2); imadi(e, tmp, tmp, 0, NS * 512u, NOREG);
        andi(e, tmp2, r, 0, 1); imadi(e, tmp, tmp2, 0, 256u, tmp);
        shr(e, tmp2, r, 0, 1); andi(e, tmp2, tmp2, 0, 1); imadi(e, tmp, tmp2, 0, 4u, tmp);
        shr(e, tmp2, pp, 0, 2); imadi(e, tmp, tmp2, 0, 16u, tmp);
        shr(e, tmp2, pp, 0, 1); andi(e, tmp2, tmp2, 0, 1); imadi(e, tmp, tmp2, 0, 512u, tmp);
        andi(e, tmp2, pp, 0, 1); imadi(e, sa, tmp2, 0, 8u, tmp);
    } else {
        /* piece = tid + jj * T -> row r = piece / PPR, pp = piece % PPR (4 bf16 at k 4 pp); piece jj of a thread is piece 0 moved
         * RJ = T / PPR rows down: the same pp, row + jj RJ, so one address register and one shared offset serve all pieces
         * (constant byte offsets jj * RJ * K * 2 on the load, jj * (RJ / 16) * NS * 512 on the store) */
        const uint32_t RJ = T / PPR; GXJ = RJ * K * 2u; SAJ = (RJ / 16u) * NS * 512u;
        shr(e, r, tid, 0, (uint32_t)log2u(PPR)); andi(e, pp, tid, 0, PPR - 1u);
        /* global: a + ((row0 + r) * K + kb0 * 128 + 4 pp) * 2 */
        iadd(e, tmp, rk, r); imadw(e, gx, tmp, K * 2u, pa);
        imadi(e, tmp, rk, 1, 256u, NOREG); imadi(e, tmp, pp, 0, 8u, tmp); imadw(e, gx, tmp, 1, gx);
        /* shared: (r / 16) * NS * 512 + (pp & 1) * 512 + (4 (r % 8) + pp / 2) * 16 + 4 ((r / 8) & 1) */
        shr(e, tmp, r, 0, 4); imadi(e, tmp, tmp, 0, NS * 512u, NOREG);
        andi(e, tmp2, pp, 0, 1); imadi(e, tmp, tmp2, 0, 512u, tmp);
        andi(e, tmp2, r, 0, 7); imadi(e, tmp2, tmp2, 0, 4u, NOREG); shr(e, hh, pp, 0, 1); iadd(e, tmp2, tmp2, hh); imadi(e, tmp, tmp2, 0, 16u, tmp);
        shr(e, hh, r, 0, 3); andi(e, hh, hh, 0, 1); imadi(e, sa, hh, 0, 4u, tmp);
    }

    /* ---- loop-wide registers ---- */
    const uint32_t NBQ = BT ? 2u * NS : 4u * NS, NST = AT ? 2u * NPAIR : NJ;
    int acc[16], bq[2][8], stgr[8], aq[2], wsel[2] = { NOREG, NOREG }, bE = NOREG, bO = NOREG, tw = V(e), cntk = V(e);
    for (uint32_t x = 0; x < RT * CT; x++) acc[x] = V128(e);
    for (unsigned set = 0; set < 2; set++) for (uint32_t j = 0; j < NBQ; j++) bq[set][j] = V64(e);
    for (uint32_t jj = 0; jj < NST; jj++) stgr[jj] = V64(e);
    for (uint32_t q = 0; q < AQN; q++) aq[q] = V128(e);
    if (!BT) { bE = V64(e); bO = V64(e); for (unsigned j = 0; j < 2; j++) wsel[j] = V(e); }
    if (e->err) return -1;

    /* A pieces of chunk c and the B rows of the chunk (pb points at the chunk's rows; gx at the chunk's A for TN) */
    #define ISSUE(set, sb, chunk) do { \
        if (AT) { for (uint32_t jj = 0; jj < NPAIR; jj++) for (unsigned h = 0; h < 2; h++) ldw_pf(e, BW_IR_LDG_E_64, stgr[2 * jj + h], gx, jj * GXJ + h * ROWB, udesc, (sb)); } \
        else { for (uint32_t jj = 0; jj < NJ; jj++) ldw_pf(e, BW_IR_LDG_E_64, stgr[jj], gx, jj * GXJ + (chunk) * KC * 2u, udesc, (sb)); } \
        if (BT) { for (uint32_t s = 0; s < NS; s++) for (unsigned h = 0; h < 2; h++) ldw_pf(e, BW_IR_LDG_E_64, bq[set][s * 2u + h], pb, h * K * 2u + s * 8u, udesc, (sb)); } \
        else { for (uint32_t s = 0; s < NS; s++) for (uint32_t j = 0; j < 4; j++) ldw_pf(e, BW_IR_LDG_E_64, bq[set][s * 4u + j], pb, (4u * s + j) * N * 2u, udesc, (sb)); } } while (0)
    /* TN pack: W0 = At[k0][m, m + 1], W1 = At[k0 + 1][m, m + 1] -> word m = (W0.lo, W1.lo), word m + 1 = (W0.hi, W1.hi) */
    #define STAGE(buf) do { \
        if (AT) { for (uint32_t jj = 0; jj < NPAIR; jj++) for (unsigned q = 0; q < 2; q++) { \
            /* each packed word is built in the scratch tw and stored at once: no loop-wide pair for the pack */ \
            andi(e, tw, stgr[2 * jj], q, 0xffffu); imadi_ds(e, tw, 0, stgr[2 * jj + 1], q, 0x10000u, tw); \
            imadi(e, tmp, one, 0, (buf) + jj * SAJ + 64u * (2u * q), sa); sts32_sub(e, tmp, tw, 0); \
            andi(e, tw, stgr[2 * jj + 1], q, 0xffff0000u); shr_ds(e, tmp2, 0, stgr[2 * jj], q, 16u); iadd_d(e, tw, 0, tmp2, 0, tw); \
            imadi(e, tmp, one, 0, (buf) + jj * SAJ + 64u * (2u * q + 1u), sa); sts32_sub(e, tmp, tw, 0); } } \
        else { for (uint32_t jj = 0; jj < NJ; jj++) { \
            imadi(e, tmp, one, 0, (buf) + jj * SAJ, sa); sts32_sub(e, tmp, stgr[jj], 0); \
            imadi(e, tmp, one, 0, (buf) + jj * SAJ + 8u, sa); sts32_sub(e, tmp, stgr[jj], 1); } } \
        bar_sync(e); } while (0)
    /* B fragments of step s (NN / TN): W_j = half word (g % 2) of row word j (LOP3 select); E: (W0.lo, W1.lo), (W2.lo, W3.lo) by
     * IMAD W1 * 2^16 + (W0 & 0xffff); O: (W0.hi, W1.hi), (W2.hi, W3.hi) by (W0 >> 16) + (W1 & 0xffff0000). (IMAD.HI.U32 would do
     * the O half in one instruction, but its Rc is a 64-bit pair on the chip: odd columns came back wrong, first chip run.) */
    #define BFRAG(set, s) do { for (unsigned h = 0; h < 2; h++) { \
        for (unsigned j = 0; j < 2; j++) lop3s(e, wsel[j], bq[set][(s) * 4u + 2 * h + j], 0, bq[set][(s) * 4u + 2 * h + j], 1, msel, 0xd8u); \
        andi(e, tw, wsel[0], 0, 0xffffu); imadi_ds(e, bE, h, wsel[1], 0, 0x10000u, tw); \
        andi(e, tw, wsel[1], 0, 0xffff0000u); shr_ds(e, bO, h, wsel[0], 0, 16u); iadd_d(e, bO, h, bO, h, tw); } } while (0)
    #define COMPUTE(buf, set, last) do { \
        for (uint32_t s = 0; s < NS; s++) { \
            if (spec->mutant == 1 && (last) && s == NS - 1u) break; \
            int fE = bE, fO = bO; \
            if (BT) { const unsigned sw4 = spec->mutant == 4 ? 1u : 0u; fE = bq[set][s * 2u + sw4]; fO = bq[set][s * 2u + (sw4 ^ 1u)]; } else BFRAG(set, s); \
            for (uint32_t i0 = 0; i0 < RT; i0 += AQN) { \
                const uint32_t nb = RT - i0 < AQN ? RT - i0 : AQN; \
                for (uint32_t q = 0; q < nb; q++) { uint32_t i = i0 + q, ii = spec->mutant == 3 ? (i ^ 1u) : i; \
                    lds128(e, aq[q], la, (buf) + (ii * NS + s) * 512u, q == 0); } \
                for (uint32_t q = 0; q < nb; q++) { const uint32_t x = (i0 + q) * CT; \
                    hmma(e, acc[x], aq[q], fE, acc[x], 8); hmma(e, acc[x + 1], aq[q], fO, acc[x + 1], 8); } \
            } \
        } } while (0)

    for (uint32_t x = 0; x < RT * CT; x++) for (unsigned q = 0; q < 4; q++) { BlackwellIRInsn mz = I0(BW_IR_MOV_RZ); mz.dst_vreg = acc[x]; mz.dst_subreg = (uint8_t)q; emit(e, mz, K_FIXED); }
    ISSUE(0, 3u, 0u);
    consume(e, 3);
    STAGE(0u);
    imadi(e, cntk, nkb, 0, 1, NOREG);

    /* ---- K loop: one 128-wide block (NCH chunks) per iteration; sets and buffers alternate per chunk (NCH even) ---- */
    const int L_k = here(e);
    loop_begin(e);
    for (uint32_t c = 0; c < NCH; c++) {
        const unsigned cur = c & 1u, nxt = cur ^ 1u;        /* set / buffer holding chunk c; the other receives chunk c + 1 */
        const uint32_t cbuf = cur ? BUF : 0u, nbuf = nxt ? BUF : 0u;
        if (c + 1u < NCH) {
            imadw(e, pb, one, BSTEP, pb);                   /* B rows of chunk c + 1 */
            if (AT) imadw(e, gx, one, ASTEP, gx);           /* At rows of chunk c + 1 */
            ISSUE(nxt, nxt ? 4u : 3u, c + 1u);
        } else {
            /* the next block's chunk 0: advance only when a block follows (nf = cntk != 1); the last iteration re-reads its own */
            int nf = V(e); if (e->err) return -1;
            imadi(e, nf, one, 0, 0xfffffffeu, cntk); shr(e, nf, nf, 0, 31); imadi(e, nf, nf, 0, 0xffffffffu, one);
            imadw(e, gx, nf, spec->mutant == 2 ? 0u : (AT ? ASTEP : 256u), gx);
            imadw(e, pb, nf, BSTEP, pb);
            ISSUE(nxt, nxt ? 4u : 3u, 0u);
        }
        COMPUTE(cbuf, cur, c + 1u == NCH);
        consume(e, nxt ? 4u : 3u);
        STAGE(nbuf);
    }
    imadi(e, cntk, one, 0, 0xffffffffu, cntk);
    isetp_ge_u32(e, cntk, one);
    bra_if_p0(e, L_k);
    loop_end(e);
    #undef ISSUE
    #undef STAGE
    #undef BFRAG
    #undef COMPUTE

    /* ---- epilogue: the HMMA results must have settled. The tile goes out through shared memory in 8-row passes so that every
     * warp store writes 128 contiguous bytes: storing straight from the fragment layout (a lane owns 16 bytes per row, one 4-byte
     * STG per word) touched 16 half-filled sectors per instruction and cost more than half of the kernel time at K 768
     * (chip probe: the no-store mutant 5 ran 8192x768x768 in 0.150 ms against 0.334 ms). Addresses in fresh registers.
     * Words: F32 four per lane row (E.c, O.c, E.c', O.c' = columns 4 t .. 4 t + 3), BF16 two packed pairs. ---- */
    nop(e, 15); nop(e, 14);
    {
        const uint32_t RJ2 = T / WPR /* rows per read step */, NJ2 = 8u * WPR / T /* words per thread per pass */, WL = 16u / ES /* words per lane row */;
        int tid2 = V(e), cb2 = V(e), tl2 = V(e), lane2 = V(e), g2 = V(e), tq2 = V(e), warp2 = V(e), tab2 = V64(e), ten2 = V64(e), cva2 = V64(e), rk2 = V64(e);
        int onee = V(e), p0 = V64(e), pp2 = V64(e), sw[4] = { NOREG, NOREG, NOREG, NOREG }, sr[4] = { NOREG, NOREG, NOREG, NOREG }, rd[4] = { NOREG, NOREG, NOREG, NOREG };
        int oa[4] = { NOREG, NOREG, NOREG, NOREG }, old[4] = { NOREG, NOREG, NOREG, NOREG }, pw = BF ? V64(e) : NOREG;
        for (unsigned w = 0; w < WL; w++) sw[w] = V(e);
        for (uint32_t j = 0; j < NJ2; j++) { sr[j] = V(e); rd[j] = V(e); if (ACC) { oa[j] = V64(e); old[j] = V(e); } }
        if (e->err) return -1;
        ldc64(e, tab2, 0x380);
        s2r(e, tid2, BW_SR_TID_X); s2r(e, spec->tfast ? tl2 : cb2, BW_SR_CTAID_X); s2r(e, spec->tfast ? cb2 : tl2, BW_SR_CTAID_Y);
        imadw(e, ten2, tl2, OMEGA_TGEMM_TILE_BYTES, tab2);
        ldg_wide(e, BW_IR_LDG_E_64, cva2, ten2, 0, udesc, 1);
        ldg_wide(e, BW_IR_LDG_E_64, rk2, ten2, 8, udesc, 0);
        andi(e, lane2, tid2, 0, 31); shr(e, g2, lane2, 0, 2); andi(e, tq2, lane2, 0, 3); shr(e, warp2, tid2, 0, 5); movi(e, onee, 1);
        /* the lane's slots of a pass: (g * SPAN + warp * 16 + 4 t) * ES + 4 w (row g of the pass, the warp's columns) */
        imadi(e, tmp, g2, 0, SPAN, NOREG); imadi(e, tmp, warp2, 0, 16u, tmp); imadi(e, tmp, tq2, 0, 4u, tmp); imadi(e, tmp2, tmp, 0, ES, NOREG);
        for (unsigned w = 0; w < WL; w++) imadi(e, sw[w], onee, 0, 4u * w, tmp2);
        /* the thread's read slots: word q = tid + T j (consecutive lanes, consecutive words): row q / WPR = tid / WPR + j RJ2 */
        for (uint32_t j = 0; j < NJ2; j++) { imadi(e, tmp, onee, 0, j * T, tid2); imadi(e, sr[j], tmp, 0, 4u, NOREG); }
        /* p0 = c + ((row0 + tid / WPR) * N + cb * SPAN + (tid % WPR) * (4 / ES)) * ES; pass (i, h) adds (16 i + 8 h) rows, step j adds j RJ2 */
        shr(e, tmp, tid2, 0, (uint32_t)log2u(WPR)); iadd(e, tmp, rk2, tmp); imadi(e, tmp, tmp, 0, N, NOREG);
        imadi(e, tmp2, cb2, 0, SPAN, tmp); andi(e, tmp, tid2, 0, WPR - 1u); imadi(e, tmp, tmp, 0, 4u / ES, tmp2); imadw(e, p0, tmp, ES, cva2);
        for (uint32_t i = 0; i < RT; i++) for (unsigned h = 0; h < 2; h++) {
            if (spec->mutant == 5) continue; /* timing probe only: no output */
            imadw(e, pp2, onee, (16u * i + 8u * h) * N * ES, p0);
            if (ACC) for (uint32_t j = 0; j < NJ2; j++) { /* the old words, in flight across the barriers (scoreboard 3 is free after the loop) */
                imadw(e, oa[j], onee, j * RJ2 * N * ES, pp2); ldw_pf(e, BW_IR_LDG_E, old[j], oa[j], 0, udesc, 3u); }
            bar_sync(e); /* the previous pass's reads (or the K loop's last LDS) are complete before the slots are rewritten */
            if (BF) for (unsigned w = 0; w < 2; w++) { /* columns 4 t + 2 w (low half, tile E) and 4 t + 2 w + 1 (high half, tile O) */
                const unsigned hi = spec->mutant == 7 ? 0u : 1u;
                f2fp_s(e, pw, w, acc[i * CT + hi], 2u * h + w, acc[i * CT + (hi ^ 1u)], 2u * h + w); sts32_sub(e, sw[w], pw, w); }
            else for (unsigned w = 0; w < 4; w++) sts32_sub(e, sw[w], acc[i * CT + (w & 1u)], 2u * h + (w >> 1));
            bar_sync(e);
            for (uint32_t j = 0; j < NJ2; j++) { BlackwellIRInsn ld = I0(BW_IR_LDS32); ld.dst_vreg = rd[j]; ld.src1_vreg = sr[j]; emit(e, ld, j == 0 ? K_VAR : K_VAR_MORE); }
            if (ACC) { consume(e, 3u); if (spec->mutant != 6) for (uint32_t j = 0; j < NJ2; j++) fadd_s(e, rd[j], 0, rd[j], 0, old[j], 0); }
            for (uint32_t j = 0; j < NJ2; j++) stg(e, pp2, j * RJ2 * N * ES, rd[j], udesc);
        }
    }
    tail(e);
    if (e->err) return -1;
    if (prog->count > OMEGA_TGEMM_MAX_INSNS) return -1;
    if (omega_bw_regalloc_solve(prog) != 0) return -1;
    if (!omega_bw_gpr_declared_ok(omega_bw_gpr_declared(prog->regalloc.peak_gpr_usage), prog->regalloc.peak_gpr_usage) || omega_bw_gpr_declared(prog->regalloc.peak_gpr_usage) > 128u) return -1;
    return 0;
}

/* ---- split-K reduce: out[w] = (old[w]) + P0[w] + P1[w] + ... in this order, one FADD per term; f32 or bf16 out ----
 * CTA of 256 threads covers 1024 f32 words: thread tid owns the word pairs u = ctaid.x * 512 + tid + 256 j (j 0, 1), read with
 * LDG.E.64 from every partial (pointer stepped by pstride, 32-bit immediate) and written as two STG.E (f32) or one packed
 * STG.E (bf16). Constant bank: out u64 (0x380), partials u64 (0x388). */
int omega_tgemm_reduce_spec_ok(const OmegaTgemmReduceSpec *r) {
    if (!r) return 0;
    if (r->splits < 2u || r->splits > 16u) return 0;
    if (r->epi > OMEGA_TGEMM_EPI_BF16) return 0;
    if (r->pstride == 0 || r->pstride % 16u) return 0;
    if (r->mutant != 0 && r->mutant != 7 && r->mutant != 8) return 0;
    return 1;
}
int omega_blackwell_codegen_tgemm_reduce_prog(const OmegaTgemmReduceSpec *r, BlackwellIRProgram *prog) {
    if (!omega_tgemm_reduce_spec_ok(r) || !prog) return -1;
    const uint32_t S = r->splits, T = OMEGA_TGEMM_REDUCE_THREADS;
    const int ACC = r->epi == OMEGA_TGEMM_EPI_F32_ACC, BF = r->epi == OMEGA_TGEMM_EPI_BF16;
    omega_bw_ir_init(prog);
    Em em; memset(&em, 0, sizeof em); em.p = prog;
    Em *e = &em;
    int udesc = UV64(e), pout = V64(e), ppart = V64(e), tid = V(e), cx = V(e), one = V(e), u0 = V(e), u = V(e);
    int pa64[2], po64[2], pv[2][16], old[2], sum = V64(e), pw = V(e);
    for (unsigned j = 0; j < 2; j++) { pa64[j] = V64(e); po64[j] = V64(e); old[j] = ACC ? V64(e) : NOREG; for (uint32_t p = 0; p < S; p++) pv[j][p] = V64(e); }
    if (e->err) return -1;
    ldcu64(e, udesc, 0x358);
    ldc64(e, pout, 0x380); ldc64(e, ppart, 0x388);
    s2r(e, tid, BW_SR_TID_X); s2r(e, cx, BW_SR_CTAID_X);
    movi(e, one, 1);
    imadi(e, u0, cx, 0, 2u * T, tid);
    for (unsigned j = 0; j < 2; j++) {
        imadi(e, u, one, 0, T * j, u0);
        imadw(e, pa64[j], u, 8u, ppart); imadw(e, po64[j], u, BF ? 4u : 8u, pout);
        if (ACC) ldw_pf(e, BW_IR_LDG_E_64, old[j], po64[j], 0, udesc, 3u);
        for (uint32_t p = 0; p < S; p++) { ldw_pf(e, BW_IR_LDG_E_64, pv[j][p], pa64[j], 0, udesc, 3u); if (p + 1u < S) imadw(e, pa64[j], one, r->pstride, pa64[j]); }
    }
    consume(e, 3u);
    for (unsigned j = 0; j < 2; j++) {
        const uint32_t last = r->mutant == 8 ? S - 1u : S; /* mutant 8 leaves the last partial out */
        for (unsigned w = 0; w < 2; w++) {
            uint32_t p = 0;
            if (ACC) { fadd_s(e, sum, w, old[j], w, pv[j][0], w); p = 1; }
            else if (last >= 2) { fadd_s(e, sum, w, pv[j][0], w, pv[j][1], w); p = 2; }
            else imadi_ds(e, sum, w, pv[j][0], w, 1u, NOREG);
            for (; p < last; p++) fadd_s(e, sum, w, sum, w, pv[j][p], w);
        }
        if (BF) { const unsigned hi = r->mutant == 7 ? 0u : 1u; f2fp_s(e, pw, 0, sum, hi, sum, hi ^ 1u); stg(e, po64[j], 0, pw, udesc); }
        else { stg_sub(e, po64[j], 0, sum, 0, udesc); stg_sub(e, po64[j], 4, sum, 1, udesc); }
    }
    tail(e);
    if (e->err) return -1;
    if (prog->count > OMEGA_TGEMM_MAX_INSNS) return -1;
    if (omega_bw_regalloc_solve(prog) != 0) return -1;
    if (!omega_bw_gpr_declared_ok(omega_bw_gpr_declared(prog->regalloc.peak_gpr_usage), prog->regalloc.peak_gpr_usage) || omega_bw_gpr_declared(prog->regalloc.peak_gpr_usage) > 128u) return -1;
    return 0;
}
