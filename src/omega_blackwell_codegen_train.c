/* omega_blackwell_codegen_train.c: on-chip training probe (LMS on one linear layer). See the header. */
#include "omega_blackwell_codegen_train.h"
#include "omega_bw_em.h"

/* d[dsub] = a[asub] * b[bsub] + c[csub]; c NOREG = RZ */
static void ffma_s(Em *e, int d, unsigned dsub, int a, unsigned asub, int b, unsigned bsub, int c, unsigned csub) {
    BlackwellIRInsn i = I0(BW_IR_FFMA);
    i.dst_vreg = d; i.dst_subreg = (uint8_t)dsub; i.src1_vreg = a; i.src1_subreg = (uint8_t)asub;
    i.src2_vreg = b; i.src2_subreg = (uint8_t)bsub; i.src3_vreg = c; i.src3_subreg = (uint8_t)csub;
    emit(e, i, K_FIXED);
}
static void fop(Em *e, BlackwellIROpcode op, int d, int a, int b) {
    BlackwellIRInsn i = I0(op); i.dst_vreg = d; i.src1_vreg = a; i.src2_vreg = b; emit(e, i, K_FIXED);
}
static void stg_s(Em *e, int addr64, uint32_t imm, int val, unsigned sub, int udesc) {
    BlackwellIRInsn i = I0(BW_IR_STG_E); i.src1_vreg = addr64; i.src2_vreg = val; i.src2_subreg = (uint8_t)sub; i.imm = imm; i.ureg = udesc;
    emit(e, i, K_STORE);
}

static void load_w(Em *e, const int *w, uint32_t nq, int pw, uint32_t rows, int udesc) {
    for (uint32_t q = 0; q < nq; q++) ldg_wide(e, BW_IR_LDG_E_128, w[q], pw, q * rows * 16u, udesc, q == 0);
}
static void store_w(Em *e, const int *w, uint32_t nq, int pw, uint32_t rows, int udesc) {
    for (uint32_t q = 0; q < nq; q++) for (unsigned j = 0; j < 4; j++) stg_s(e, pw, q * rows * 16u + 4u * j, w[q], j, udesc);
}

/* One SGD step on sample (cnt & data_mask): forward, backward, update, loss. */
static void emit_step(Em *e, const OmegaTrainProbeSpec *s, uint32_t rows, const int *w, int cnt, int px0, int py0, int udesc,
                      int neglr, int loss) {
    const uint32_t nq = s->in / 4, half = nq / 2;
    int sidx = V(e), px = V64(e), py = V64(e), ys = V(e), y = V(e), err_ = V(e), sc = V(e), x[4];
    for (int k = 0; k < 4; k++) x[k] = V128(e);
    if (e->err) return;
    andi(e, sidx, cnt, 0, s->data_mask);
    imadw(e, px, sidx, s->in * 4u, px0);
    imadw(e, py, sidx, rows * 4u, py0);
    /* forward: y = sum_i W_i x_i, one FMA chain from zero, in two halves of the input (register budget) */
    movrz(e, y);
    for (uint32_t h = 0; h < 2; h++) {
        uint32_t q0 = h ? half : 0, q1 = h ? nq : half;
        for (uint32_t q = q0; q < q1; q++) ldg_wide(e, BW_IR_LDG_E_128, x[q - q0], px, q * 16u, udesc, q == q0);
        if (h == 0) ldg32(e, ys, py, udesc, 0);
        for (uint32_t q = q0; q < q1; q++) for (unsigned j = 0; j < 4; j++) ffma_s(e, y, 0, w[q], j, x[q - q0], j, y, 0);
    }
    /* backward for one linear layer: dL/dy = e (the factor 2 folded into lr), dL/dW_i = e x_i */
    fop(e, BW_IR_FSUB, err_, y, ys);
    ffma_s(e, loss, 0, err_, 0, err_, 0, loss, 0);
    fop(e, BW_IR_FMUL, sc, err_, neglr);
    /* update: W_i = x_i * (-lr e) + W_i (gradient product and step in one FMA) */
    for (uint32_t h = 0; h < 2; h++) {
        uint32_t q0 = h ? half : 0, q1 = h ? nq : half;
        for (uint32_t q = q0; q < q1; q++) ldg_wide(e, BW_IR_LDG_E_128, x[q - q0], px, q * 16u, udesc, q == q0);
        for (uint32_t q = q0; q < q1; q++) for (unsigned j = 0; j < 4; j++) ffma_s(e, w[q], j, x[q - q0], j, sc, 0, w[q], j);
    }
}

int omega_blackwell_codegen_train_lms_prog(const OmegaTrainProbeSpec *s, BlackwellIRProgram *prog) {
    if (!s || !prog || s->in == 0 || s->in % 8 || s->in > 32 || s->threads == 0 || s->threads % 32 || s->threads > 1024 ||
        s->ctas == 0 || s->steps == 0 || s->lr_bits == 0 || (s->data_mask & (s->data_mask + 1u)) != 0 ||
        (s->batch && s->steps % s->batch) || (!s->batch && s->ivall)) return -1;
    const uint32_t rows = s->threads * s->ctas, nq = s->in / 4;
    if ((uint64_t)nq * rows * 16u > 0x7fffffffu || (uint64_t)(s->data_mask + 1u) * rows * 4u > 0xffffffffu) return -1;
    omega_bw_ir_init(prog);
    Em em; memset(&em, 0, sizeof em); em.p = prog; Em *e = &em;
    int udesc = UV64(e), pw = V64(e), px0 = V64(e), py0 = V64(e), pl = V64(e), tid = V(e), cta = V(e), g = V(e);
    int one = V(e), neglr = V(e), cnt = V(e), loss = V(e), w[8];
    for (uint32_t q = 0; q < nq; q++) w[q] = V128(e);
    if (e->err) return -1;

    ldcu64(e, udesc, 0x358);
    ldc64(e, pw, 0x380); ldc64(e, px0, 0x388); ldc64(e, py0, 0x390); ldc64(e, pl, 0x398);
    s2r(e, tid, BW_SR_TID_X); s2r(e, cta, BW_SR_CTAID_X);
    imadi(e, g, cta, 0, s->threads, tid);
    imadw(e, pw, g, 16u, pw); imadw(e, py0, g, 4u, py0); imadw(e, pl, g, 4u, pl);
    movi(e, one, 1); movi(e, neglr, s->lr_bits ^ 0x80000000u); movrz(e, loss);
    movi(e, cnt, s->steps);

    if (!s->batch) {
        /* read once: weights come in once, every step runs out of registers, weights go out once */
        load_w(e, w, nq, pw, rows, udesc);
        const int L = here(e);
        loop_begin(e);
        emit_step(e, s, rows, w, cnt, px0, py0, udesc, neglr, loss);
        imadi(e, cnt, one, 0, 0xffffffffu, cnt);
        isetp_ge_u32(e, cnt, one);
        bra_if_p0(e, L);
        loop_end(e);
        store_w(e, w, nq, pw, rows, udesc);
    } else {
        /* streamed: per micro-batch of `batch` samples, load W, run the samples, store W */
        int inner = V(e);
        if (e->err) return -1;
        const int Lo = here(e);
        loop_begin(e);
        if (s->ivall) emit(e, I0(BW_IR_CCTL_IVALL), K_FIXED);
        load_w(e, w, nq, pw, rows, udesc);
        movi(e, inner, s->batch);
        const int Li = here(e);
        loop_begin(e);
        emit_step(e, s, rows, w, cnt, px0, py0, udesc, neglr, loss);
        imadi(e, cnt, one, 0, 0xffffffffu, cnt);
        imadi(e, inner, one, 0, 0xffffffffu, inner);
        isetp_ge_u32(e, inner, one);
        bra_if_p0(e, Li);
        loop_end(e);
        store_w(e, w, nq, pw, rows, udesc);
        isetp_ge_u32(e, cnt, one);
        bra_if_p0(e, Lo);
        loop_end(e);
    }
    stg(e, pl, 0, loss, udesc);
    tail(e);
    if (e->err) return -1;
    return omega_bw_regalloc_solve(prog);
}
