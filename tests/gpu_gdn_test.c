/*
 * gpu_gdn_test: the one-launch Gated DeltaNet decode step of Qwen3.8-27B (omega_gpu_gdn_api.h), omega#542.
 *   --host-only   layout helpers, codegen budgets and refusals, the double oracle's delta-rule property
 *   --sim         warp-simulator parity over the whole grid (48 CTAs x 256 threads, both fragment orders bit-identical),
 *                 three chained steps against the double oracle, mutants 1-4 caught
 *   (default)     the chip: two layers (addressing), five chained steps each against the oracle (output, tail, state),
 *                 a rerun from the same state bit-identical, reset, mutants 1-4 caught, the record path (two steps in
 *                 one command list: the second reads the first's state), chip medians (--reps N, --out receipt.json)
 * The oracle is sc crates/aien-inference-abi/src/qwen38.rs `delta_net` in double, from the projections on.
 * Verdict line last: "gpu_gdn_test: N checks, F failed: PASS|FAIL".
 */
#include "omega_gpu_gdn_api.h"
#include "omega_gpu_session.h"
#include "omega_gpu_cmdlist.h"
#include "bw_warp_sim_k.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g_checks, g_failed;
#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { g_failed++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)
static uint32_t lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s; }
static float frand(uint32_t *s, float lo, float hi) { return lo + (hi - lo) * ((float)(lcg(s) >> 8) / 16777216.0f); }
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static int cmp_u64(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }

#define CD OMEGA_GDN_CONV_DIM
#define VH OMEGA_GDN_VALUE_HEADS
#define KH OMEGA_GDN_KEY_HEADS
#define HD OMEGA_GDN_HEAD_DIM
#define VD OMEGA_GDN_V_DIM
#define NS OMEGA_GPU_GDN_STATE_FLOATS
#define NT OMEGA_GPU_GDN_TAIL_FLOATS
static const float EPS_L2 = 1e-6f, EPS_N = 1e-6f;
/* tolerances, normalised by the largest reference magnitude of the vector (EX2 / RCP / RSQ / LG2 are approximate on
 * the chip, the oracle is double; the mutants miss by orders of magnitude more, see the receipts) */
#define TOL_OUT 2e-4
#define TOL_STATE 2e-5
#define TOL_TAIL 1e-6

typedef struct {
    float *conv_w, a_log[VH], dtb[VH], nw[HD];
    float *tail0, *state0; /* initial state, reference layouts */
} Layer;
typedef struct { float *qkv, *z, ab[2 * VH]; } Inp;

static void layer_make(Layer *L, uint32_t seed) {
    uint32_t s = seed;
    L->conv_w = malloc((size_t)CD * 4 * 4); L->tail0 = malloc((size_t)NT * 4); L->state0 = malloc((size_t)NS * 4);
    for (size_t i = 0; i < (size_t)CD * 4; i++) L->conv_w[i] = frand(&s, -0.6f, 0.6f);
    for (unsigned h = 0; h < VH; h++) { L->a_log[h] = frand(&s, -1.5f, 1.5f); L->dtb[h] = frand(&s, -3.0f, 1.0f); }
    for (unsigned i = 0; i < HD; i++) L->nw[i] = frand(&s, 0.4f, 1.6f);
    for (size_t i = 0; i < NT; i++) L->tail0[i] = frand(&s, -2.0f, 2.0f);
    for (size_t i = 0; i < NS; i++) L->state0[i] = frand(&s, -0.2f, 0.2f);
}
static void layer_free(Layer *L) { free(L->conv_w); free(L->tail0); free(L->state0); }
/* in_scale: the kernel multiplies qkv and z by it, so the stored rows are the true rows / in_scale */
static void inp_make(Inp *in, uint32_t seed, float in_scale) {
    uint32_t s = seed;
    in->qkv = malloc((size_t)CD * 4); in->z = malloc((size_t)VD * 4);
    for (unsigned i = 0; i < CD; i++) in->qkv[i] = frand(&s, -2.5f, 2.5f) / in_scale;
    for (unsigned i = 0; i < VD; i++) in->z[i] = frand(&s, -3.0f, 3.0f) / in_scale;
    for (unsigned h = 0; h < VH; h++) { in->ab[2 * h] = frand(&s, -4.0f, 4.0f); in->ab[2 * h + 1] = frand(&s, -4.0f, 4.0f); }
    /* a few heads at the softplus threshold and past it, and beta near 0 / 1 */
    if (!getenv("GDN_NOFORCE")) { in->ab[0] = 25.0f; in->ab[2] = 19.9f; in->ab[4] = -30.0f; in->ab[3] = 12.0f; in->ab[5] = -12.0f; }
}
static void inp_free(Inp *in) { free(in->qkv); free(in->z); }

/* ---- the double oracle ---- */
typedef struct { double *tail, *state; } DState;
static void dstate_init(DState *d, const Layer *L) {
    d->tail = malloc((size_t)NT * 8); d->state = malloc((size_t)NS * 8);
    for (size_t i = 0; i < NT; i++) d->tail[i] = L->tail0[i];
    for (size_t i = 0; i < NS; i++) d->state[i] = L->state0[i];
}
static void dstate_free(DState *d) { free(d->tail); free(d->state); }
static double sigm(double x) { return 1.0 / (1.0 + exp(-x)); }
static double softplus_ref(double x) { return x > 20.0 ? x : log1p(exp(x)); }
static void oracle_step(const Layer *L, DState *d, const Inp *in, float in_scale, float out_scale, double *y) {
    static double mixed[CD], q[KH * HD], k[KH * HD];
    for (unsigned ch = 0; ch < CD; ch++) {
        const double x = (double)(in->qkv[ch] * in_scale); /* the kernel's f32 product */
        const float *w = &L->conv_w[(size_t)ch * 4];
        double acc = w[0] * d->tail[ch] + w[1] * d->tail[CD + ch] + w[2] * d->tail[2 * CD + ch] + w[3] * x;
        mixed[ch] = acc * sigm(acc);
        d->tail[ch] = d->tail[CD + ch]; d->tail[CD + ch] = d->tail[2 * CD + ch]; d->tail[2 * CD + ch] = x;
    }
    for (unsigned hk = 0; hk < KH; hk++) {
        double sq = 0, sk = 0;
        for (unsigned i = 0; i < HD; i++) { sq += mixed[hk * HD + i] * mixed[hk * HD + i]; sk += mixed[KH * HD + hk * HD + i] * mixed[KH * HD + hk * HD + i]; }
        const double iq = 1.0 / sqrt(sq + EPS_L2), ik = 1.0 / sqrt(sk + EPS_L2);
        for (unsigned i = 0; i < HD; i++) { q[hk * HD + i] = mixed[hk * HD + i] * iq / sqrt((double)HD); k[hk * HD + i] = mixed[KH * HD + hk * HD + i] * ik; }
    }
    for (unsigned h = 0; h < VH; h++) {
        const unsigned hk = h / (VH / KH);
        const double negA = -(double)expf(L->a_log[h]); /* the API's float negA */
        const double g = (double)(float)negA * softplus_ref((double)in->ab[2 * h] + L->dtb[h]);
        const double decay = exp(g), beta = sigm(in->ab[2 * h + 1]);
        double *S = d->state + (size_t)h * HD * HD, kv[HD], o[HD], dl[HD];
        const double *kk = &k[hk * HD], *qq = &q[hk * HD], *v = &mixed[2 * KH * HD + h * HD];
        for (size_t i = 0; i < (size_t)HD * HD; i++) S[i] *= decay;
        for (unsigned c = 0; c < HD; c++) kv[c] = 0;
        for (unsigned r = 0; r < HD; r++) for (unsigned c = 0; c < HD; c++) kv[c] += S[r * HD + c] * kk[r];
        for (unsigned c = 0; c < HD; c++) dl[c] = (v[c] - kv[c]) * beta;
        for (unsigned r = 0; r < HD; r++) for (unsigned c = 0; c < HD; c++) S[r * HD + c] += kk[r] * dl[c];
        for (unsigned c = 0; c < HD; c++) o[c] = 0;
        for (unsigned r = 0; r < HD; r++) for (unsigned c = 0; c < HD; c++) o[c] += S[r * HD + c] * qq[r];
        double ms = 0; for (unsigned c = 0; c < HD; c++) ms += o[c] * o[c];
        const double inv = 1.0 / sqrt(ms / HD + EPS_N);
        for (unsigned c = 0; c < HD; c++) {
            const double zz = (double)(in->z[h * HD + c] * in_scale);
            y[h * HD + c] = o[c] * inv * L->nw[c] * (zz * sigm(zz)) * out_scale;
        }
    }
}
/* max |got - ref| / max |ref| */
static double nerr(const float *got, const double *ref, size_t n) {
    double e = 0, m = 1e-30;
    for (size_t i = 0; i < n; i++) {
        const double a = fabs((double)got[i] - ref[i]);
        if (!isfinite(a)) { if (getenv("GDN_DBG")) printf("  non-finite at %zu: got %g ref %g\n", i, (double)got[i], ref[i]); return INFINITY; } /* a NaN or inf on either side never passes */
        if (a > e) e = a;
        if (fabs(ref[i]) > m) m = fabs(ref[i]);
    }
    return e / m;
}
typedef struct { double out, tail, state; } Errs;
static int errs_ok(const Errs *e) { return e->out <= TOL_OUT && e->tail <= TOL_TAIL && e->state <= TOL_STATE; }

/* ---- host-only ---- */
static void host_only(void) {
    /* layout round trip, and the copy check */
    float *t = malloc((size_t)NT * 4), *t2 = malloc((size_t)NT * 4), *dev = malloc(OMEGA_GDN_TAIL_BYTES);
    uint32_t s = 5;
    for (size_t i = 0; i < NT; i++) t[i] = frand(&s, -1, 1);
    omega_gdn_pack_tail(t, dev);
    CHECK(omega_gdn_unpack_tail(dev, t2) == 0 && memcmp(t, t2, (size_t)NT * 4) == 0, "tail pack/unpack round trip");
    dev[(1u * OMEGA_GDN_SLOTS + 7u) * 4u + 1u] += 1.0f; /* head 1 (a second copy of key head 0), q slot 7 */
    CHECK(omega_gdn_unpack_tail(dev, t2) == -1, "unpack refuses disagreeing copies");
    free(t); free(t2); free(dev);
    /* parameter block */
    Layer L; layer_make(&L, 9);
    uint8_t *blk = malloc(OMEGA_GDN_PAR_BYTES);
    CHECK(omega_gdn_pack_params(L.conv_w, L.a_log, L.dtb, L.nw, blk) == 0, "pack params");
    const float *sc = (const float *)(blk + OMEGA_GDN_PAR_SCAL);
    CHECK(sc[2 * 5] == -expf(L.a_log[5]) && sc[2 * 5 + 1] == L.dtb[5], "negA and dt_bias interleaved");
    CHECK(memcmp(blk + OMEGA_GDN_PAR_NORM, L.nw, HD * 4) == 0, "norm weight at its offset");
    CHECK(omega_gdn_pack_params(NULL, L.a_log, L.dtb, L.nw, blk) == -1, "pack params refuses NULL");
    free(blk);
    /* codegen budgets */
    for (int m = 0; m <= 4; m++) {
        OmegaGdnSpec sp = { m }; uint32_t ni = 0, ng = 0;
        int rc = omega_gdn_prog_stats(&sp, &ni, &ng);
        CHECK(rc == 0 && ni <= BW_MAX_IR_INSNS && ng + 2 <= BW_PHYS_GPR_MAX, "mutant %d codegen: rc %d, %u insns, %u registers", m, rc, ni, ng);
        if (m == 0) printf("kernel: %u instructions, peak %u registers (%u declared, %u threads)\n", ni, ng, omega_bw_gpr_declared(ng), OMEGA_GDN_THREADS);
    }
    OmegaGdnSpec bad = { 5 };
    CHECK(omega_gdn_prog_stats(&bad, NULL, NULL) == -1, "mutant 5 refused");
    /* the oracle's delta rule: from S = 0 with beta ~ 1, the step writes v along k, so S^T k = v (|k| = 1) */
    {
        DState d; dstate_init(&d, &L);
        memset(d.state, 0, (size_t)NS * 8);
        Inp in; inp_make(&in, 77, 1.0f);
        for (unsigned h = 0; h < VH; h++) in.ab[2 * h + 1] = 40.0f;
        double *y = malloc((size_t)VD * 8);
        static double mixed_v[CD];
        /* the expected v and k: recompute the conv of this step from the oracle's tail before the step */
        for (unsigned ch = 0; ch < CD; ch++) {
            const float *w = &L.conv_w[(size_t)ch * 4];
            double acc = w[0] * d.tail[ch] + w[1] * d.tail[CD + ch] + w[2] * d.tail[2 * CD + ch] + w[3] * (double)in.qkv[ch];
            mixed_v[ch] = acc * sigm(acc);
        }
        oracle_step(&L, &d, &in, 1.0f, 1.0f, y);
        double worst = 0;
        for (unsigned h = 0; h < VH; h += 7) {
            const unsigned hk = h / 3;
            double nk = 0; for (unsigned i = 0; i < HD; i++) nk += mixed_v[KH * HD + hk * HD + i] * mixed_v[KH * HD + hk * HD + i];
            nk = sqrt(nk + EPS_L2);
            for (unsigned c = 0; c < HD; c++) {
                double kv = 0;
                for (unsigned r = 0; r < HD; r++) kv += d.state[(size_t)h * HD * HD + r * HD + c] * mixed_v[KH * HD + hk * HD + r] / nk;
                const double want = mixed_v[2 * KH * HD + h * HD + c];
                if (fabs(kv - want) > worst) worst = fabs(kv - want);
            }
        }
        CHECK(worst < 1e-6, "oracle delta rule: S^T k = v after one step from zero (worst %.3g)", worst);
        free(y); inp_free(&in); dstate_free(&d);
    }
    layer_free(&L);
}

/* ---- simulator ---- */
enum { SB_QKV, SB_Z, SB_AB, SB_PAR, SB_TAIL, SB_STATE, SB_OUT, SB_N };
static uint64_t g_sim_issues;
static int sim_run_steps(int mutant, uint32_t steps, float in_scale, float out_scale, Errs *worst, char *msg, size_t n) {
    static BlackwellIRProgram prog;
    OmegaGdnSpec spec = { mutant };
    if (omega_blackwell_codegen_gdn_prog(&spec, &prog) != 0) { snprintf(msg, n, "codegen refused"); return -1; }
    Layer L; layer_make(&L, 21);
    DState d; dstate_init(&d, &L);
    size_t sz[SB_N] = { CD * 4u, VD * 4u, VH * 8u, OMEGA_GDN_PAR_BYTES, OMEGA_GDN_TAIL_BYTES, OMEGA_GDN_STATE_BYTES, VD * 4u };
    uint8_t *buf[SB_N];
    for (int i = 0; i < SB_N; i++) buf[i] = aligned_alloc(256, (sz[i] + 255) & ~(size_t)255);
    omega_gdn_pack_params(L.conv_w, L.a_log, L.dtb, L.nw, buf[SB_PAR]);
    omega_gdn_pack_tail(L.tail0, (float *)buf[SB_TAIL]);
    memcpy(buf[SB_STATE], L.state0, OMEGA_GDN_STATE_BYTES);
    uint8_t *tail_save = malloc(OMEGA_GDN_TAIL_BYTES), *state_save = malloc(OMEGA_GDN_STATE_BYTES), *out_copy = malloc(VD * 4u);
    uint8_t *tail_copy = malloc(OMEGA_GDN_TAIL_BYTES), *state_copy = malloc(OMEGA_GDN_STATE_BYTES);
    double *y = malloc((size_t)VD * 8);
    float *tref = malloc((size_t)NT * 4);
    int err = 0;
    memset(worst, 0, sizeof *worst);
    for (uint32_t st = 0; st < steps && !err; st++) {
        Inp in; inp_make(&in, 1000 + st, in_scale);
        memcpy(buf[SB_QKV], in.qkv, sz[SB_QKV]); memcpy(buf[SB_Z], in.z, sz[SB_Z]); memcpy(buf[SB_AB], in.ab, sz[SB_AB]);
        memcpy(tail_save, buf[SB_TAIL], OMEGA_GDN_TAIL_BYTES); memcpy(state_save, buf[SB_STATE], OMEGA_GDN_STATE_BYTES);
        for (int order = 0; order < 2 && !err; order++) {
            if (order == 1) { memcpy(buf[SB_TAIL], tail_save, OMEGA_GDN_TAIL_BYTES); memcpy(buf[SB_STATE], state_save, OMEGA_GDN_STATE_BYTES); }
            float *yo = (float *)buf[SB_OUT];
            for (unsigned i = 0; i < VD; i++) yo[i] = -999.0f;
            WsSim s; memset(&s, 0, sizeof s);
            s.p = &prog; s.threads = OMEGA_GDN_THREADS; s.order = order;
            static uint8_t smem[OMEGA_GDN_SHARED_BYTES]; s.smem = smem; s.smem_bytes = OMEGA_GDN_SHARED_BYTES;
            for (int i = 0; i < SB_N; i++) {
                const uint64_t a = (uintptr_t)buf[i];
                s.cbank[(0x380 + 8 * i) / 4] = (uint32_t)a; s.cbank[(0x380 + 8 * i) / 4 + 1] = (uint32_t)(a >> 32);
                s.ranges[i].lo = (uintptr_t)buf[i]; s.ranges[i].hi = (uintptr_t)buf[i] + sz[i];
            }
            s.nranges = SB_N;
            memcpy(&s.cbank[OMEGA_GDN_ARG_EPS_L2 / 4], &EPS_L2, 4); memcpy(&s.cbank[OMEGA_GDN_ARG_EPS_NORM / 4], &EPS_N, 4);
            memcpy(&s.cbank[OMEGA_GDN_ARG_IN_SCALE / 4], &in_scale, 4); memcpy(&s.cbank[OMEGA_GDN_ARG_OUT_SCALE / 4], &out_scale, 4);
            err = ws_run(&s, OMEGA_GDN_GRID_X, OMEGA_GDN_GRID_Y);
            if (err) { snprintf(msg, n, "step %u order %d: %s (pc %d)", st, order, s.msg, s.err_pc); break; }
            g_sim_issues += s.warp_issues;
            size_t unwritten = 0; for (unsigned i = 0; i < VD; i++) if (yo[i] == -999.0f) unwritten++;
            if (unwritten) { err = -1; snprintf(msg, n, "%zu output words never written", unwritten); break; }
            if (order == 0) { memcpy(out_copy, yo, VD * 4u); memcpy(tail_copy, buf[SB_TAIL], OMEGA_GDN_TAIL_BYTES); memcpy(state_copy, buf[SB_STATE], OMEGA_GDN_STATE_BYTES); }
            else if (memcmp(out_copy, yo, VD * 4u) || memcmp(tail_copy, buf[SB_TAIL], OMEGA_GDN_TAIL_BYTES) || memcmp(state_copy, buf[SB_STATE], OMEGA_GDN_STATE_BYTES)) {
                err = -1; snprintf(msg, n, "step %u: the fragment order changed the result", st);
            }
        }
        if (err) { inp_free(&in); break; }
        oracle_step(&L, &d, &in, in_scale, out_scale, y);
        double *ts = malloc((size_t)NT * 8);
        for (size_t i = 0; i < NT; i++) ts[i] = d.tail[i];
        Errs e;
        e.out = nerr((const float *)buf[SB_OUT], y, VD);
        if (omega_gdn_unpack_tail((const float *)buf[SB_TAIL], tref) != 0) e.tail = INFINITY;
        else e.tail = nerr(tref, ts, NT);
        e.state = nerr((const float *)buf[SB_STATE], d.state, NS);
        free(ts);
        if (e.out > worst->out) worst->out = e.out;
        if (e.tail > worst->tail) worst->tail = e.tail;
        if (e.state > worst->state) worst->state = e.state;
        inp_free(&in);
    }
    for (int i = 0; i < SB_N; i++) free(buf[i]);
    free(tail_save); free(state_save); free(out_copy); free(tail_copy); free(state_copy); free(y); free(tref);
    dstate_free(&d); layer_free(&L);
    return err;
}
static void sim_battery(void) {
    struct { int mutant; uint32_t steps; float in_scale, out_scale; const char *name; } pts[] = {
        { 0, 3, 1.0f, 1.0f, "3 steps" },
        { 0, 2, 0.0009765625f, 18446744073709551616.0f, "2 steps, in_scale 2^-10, out_scale 2^64" },
        { 1, 2, 1.0f, 1.0f, "mutant 1 (no decay)" }, { 2, 2, 1.0f, 1.0f, "mutant 2 (beta 1)" },
        { 3, 2, 1.0f, 1.0f, "mutant 3 (gate z, not silu z)" }, { 4, 2, 1.0f, 1.0f, "mutant 4 (tail not shifted)" },
    };
    for (size_t i = 0; i < sizeof pts / sizeof pts[0]; i++) {
        char msg[300] = ""; Errs w = { 0, 0, 0 };
        double t0 = now_ms();
        int err = sim_run_steps(pts[i].mutant, pts[i].steps, pts[i].in_scale, pts[i].out_scale, &w, msg, sizeof msg);
        printf("sim %-44s err out %.3g tail %.3g state %.3g (%.0f ms)%s%s\n", pts[i].name, w.out, w.tail, w.state, now_ms() - t0, err ? " ERROR " : "", msg);
        CHECK(err == 0, "sim %s ran (%s)", pts[i].name, msg);
        if (pts[i].mutant == 0) CHECK(errs_ok(&w), "sim %s within tolerance", pts[i].name);
        else CHECK(!errs_ok(&w), "sim %s caught", pts[i].name);
    }
    printf("sim warp issues: %" PRIu64 "\n", g_sim_issues);
}

/* ---- chip ---- */
static int chip_steps(OmegaGpuGdn *g, uint32_t layer, const Layer *L, DState *d, uint32_t steps, uint32_t seed, float in_scale,
                      float out_scale, Errs *worst, float *last_out) {
    float *out = malloc((size_t)VD * 4), *tail = malloc((size_t)NT * 4), *state = malloc((size_t)NS * 4);
    double *y = malloc((size_t)VD * 8), *td = malloc((size_t)NT * 8);
    int rc = 0;
    memset(worst, 0, sizeof *worst);
    for (uint32_t st = 0; st < steps && rc == 0; st++) {
        Inp in; inp_make(&in, seed + st, in_scale);
        rc = omega_gpu_gdn_step(g, layer, in.qkv, in.z, in.ab, EPS_L2, EPS_N, in_scale, out_scale, out, NULL);
        if (rc == 0) rc = omega_gpu_gdn_get_state(g, layer, tail, state);
        if (rc == 0) {
            oracle_step(L, d, &in, in_scale, out_scale, y);
            for (size_t i = 0; i < NT; i++) td[i] = d->tail[i];
            Errs e = { nerr(out, y, VD), nerr(tail, td, NT), nerr(state, d->state, NS) };
            if (e.out > worst->out) worst->out = e.out;
            if (e.tail > worst->tail) worst->tail = e.tail;
            if (e.state > worst->state) worst->state = e.state;
        }
        inp_free(&in);
    }
    if (last_out) memcpy(last_out, out, (size_t)VD * 4);
    free(out); free(tail); free(state); free(y); free(td);
    return rc;
}
static int chip_run(const char *out_path, uint32_t reps) {
    FILE *out = out_path ? fopen(out_path, "w") : NULL;
    if (out_path && !out) { printf("cannot write %s\n", out_path); return 1; }
    if (out) fprintf(out, "{\"receipt\":\"OMEGA_GPU_GDN_V1\",\"reps\":%u,\"tol\":{\"out\":%g,\"tail\":%g,\"state\":%g},\"points\":[", reps, TOL_OUT, TOL_TAIL, TOL_STATE);
    OmegaGpuGdn *g = NULL;
    double t0 = now_ms();
    int rc = omega_gpu_gdn_alloc(2, &g);
    printf("gdn alloc (2 layers): rc=%s %.1f ms (%s)\n", omega_gpu_gdn_rc_name(rc), now_ms() - t0, rc ? omega_gpu_gdn_last_error() : "");
    CHECK(rc == OMEGA_GPU_GDN_OK, "gdn alloc");
    if (rc) { if (out) fprintf(out, "],\"verdict\":\"FAIL\"}\n"); return 1; }
    Layer L[2]; layer_make(&L[0], 31); layer_make(&L[1], 32);
    for (uint32_t l = 0; l < 2; l++) {
        CHECK(omega_gpu_gdn_set_layer(g, l, L[l].conv_w, L[l].a_log, L[l].dtb, L[l].nw) == 0, "set layer %u", l);
        CHECK(omega_gpu_gdn_set_state(g, l, L[l].tail0, L[l].state0) == 0, "set state %u", l);
    }
    int first = 1;
#define POINT(name, e, extra) do { if (out) { fprintf(out, "%s{\"name\":\"%s\",\"err_out\":%.6g,\"err_tail\":%.6g,\"err_state\":%.6g%s}", first ? "" : ",", name, (e).out, (e).tail, (e).state, extra); first = 0; } } while (0)
    /* parity: layer 1 first (addressing), then layer 0 with in/out scales */
    struct { uint32_t layer, steps, seed; float in_scale, out_scale; const char *name; } pp[] = {
        { 1, 5, 500, 1.0f, 1.0f, "layer 1, 5 steps" },
        { 0, 5, 600, 0.0009765625f, 18446744073709551616.0f, "layer 0, 5 steps, in 2^-10 out 2^64" },
    };
    float *o1 = malloc((size_t)VD * 4), *o2 = malloc((size_t)VD * 4);
    for (size_t i = 0; i < 2; i++) {
        DState d; dstate_init(&d, &L[pp[i].layer]);
        Errs e;
        rc = chip_steps(g, pp[i].layer, &L[pp[i].layer], &d, pp[i].steps, pp[i].seed, pp[i].in_scale, pp[i].out_scale, &e, o1);
        printf("chip %-40s rc=%s err out %.3g tail %.3g state %.3g %s\n", pp[i].name, omega_gpu_gdn_rc_name(rc), e.out, e.tail, e.state, rc ? omega_gpu_gdn_last_error() : "");
        CHECK(rc == 0 && errs_ok(&e), "chip %s", pp[i].name);
        POINT(pp[i].name, e, "");
        dstate_free(&d);
    }
    /* rerun from the same state: bit-identical */
    {
        Inp in; inp_make(&in, 700, 1.0f);
        CHECK(omega_gpu_gdn_set_state(g, 1, L[1].tail0, L[1].state0) == 0, "set state for rerun");
        rc = omega_gpu_gdn_step(g, 1, in.qkv, in.z, in.ab, EPS_L2, EPS_N, 1.0f, 1.0f, o1, NULL);
        CHECK(omega_gpu_gdn_set_state(g, 1, L[1].tail0, L[1].state0) == 0, "set state for rerun 2");
        int rc2 = omega_gpu_gdn_step(g, 1, in.qkv, in.z, in.ab, EPS_L2, EPS_N, 1.0f, 1.0f, o2, NULL);
        CHECK(rc == 0 && rc2 == 0 && memcmp(o1, o2, (size_t)VD * 4) == 0, "rerun from the same state is bit-identical");
        inp_free(&in);
    }
    /* reset: state and tail zero */
    {
        float *t = malloc((size_t)NT * 4), *s = malloc((size_t)NS * 4);
        rc = omega_gpu_gdn_reset(g);
        int z = rc == 0 && omega_gpu_gdn_get_state(g, 0, t, s) == 0;
        for (size_t i = 0; z && i < NT; i++) if (t[i] != 0.0f) z = 0;
        for (size_t i = 0; z && i < NS; i++) if (s[i] != 0.0f) z = 0;
        CHECK(z, "reset zeroes the tail and the state");
        free(t); free(s);
    }
    /* mutants: each must miss */
    for (int m = 1; m <= 4; m++) {
        omega_gpu_gdn_test_set_mutant(m);
        CHECK(omega_gpu_gdn_set_state(g, 1, L[1].tail0, L[1].state0) == 0, "set state mutant %d", m);
        DState d; dstate_init(&d, &L[1]);
        Errs e;
        rc = chip_steps(g, 1, &L[1], &d, 2, 800, 1.0f, 1.0f, &e, NULL);
        char nm[32]; snprintf(nm, sizeof nm, "mutant %d", m);
        printf("chip %-40s rc=%s err out %.3g tail %.3g state %.3g\n", nm, omega_gpu_gdn_rc_name(rc), e.out, e.tail, e.state);
        CHECK(rc == 0 && !errs_ok(&e), "chip mutant %d caught", m);
        POINT(nm, e, "");
        dstate_free(&d);
    }
    omega_gpu_gdn_test_set_mutant(0);
    /* the record path: two chained steps in one command list on layer 0 */
    {
        CHECK(omega_gpu_gdn_set_state(g, 0, L[0].tail0, L[0].state0) == 0, "set state for record");
        Inp in[2]; inp_make(&in[0], 900, 1.0f); inp_make(&in[1], 901, 1.0f);
        DState d; dstate_init(&d, &L[0]);
        double *y = malloc((size_t)VD * 8);
        oracle_step(&L[0], &d, &in[0], 1.0f, 1.0f, y); oracle_step(&L[0], &d, &in[1], 1.0f, 1.0f, y);
        /* warm the kernel cache outside the record (a miss inside is refused) */
        float *tmp = malloc((size_t)VD * 4);
        CHECK(omega_gpu_gdn_step(g, 1, in[0].qkv, in[0].z, in[0].ab, EPS_L2, EPS_N, 1.0f, 1.0f, tmp, NULL) == 0, "warm-up step");
        omega_gpu_session_lock();
        OmegaGpuMem inb[2]; memset(inb, 0, sizeof inb);
        OmegaGpuCmdList *cl = NULL;
        int ok = omega_gpu_session_open() != NULL;
        const size_t ib = (size_t)CD * 4 + (size_t)VD * 4 + VH * 8;
        for (int i = 0; ok && i < 2; i++) {
            ok = omega_gpu_session_alloc(ib, &inb[i]) == 0;
            if (ok) {
                memcpy(inb[i].cpu, in[i].qkv, (size_t)CD * 4); memcpy((uint8_t *)inb[i].cpu + CD * 4, in[i].z, (size_t)VD * 4);
                memcpy((uint8_t *)inb[i].cpu + CD * 4 + VD * 4, in[i].ab, VH * 8);
                ok = omega_gpu_session_sync_to_gpu(&inb[i]) == 0;
            }
        }
        uint64_t out_va = 0;
        if (ok) ok = omega_gpu_cmdlist_create(4, &cl) == 0;
        for (int i = 0; ok && i < 2; i++)
            ok = omega_gpu_gdn_record(cl, g, 0, inb[i].va, inb[i].va + CD * 4, inb[i].va + CD * 4 + VD * 4, EPS_L2, EPS_N, 1.0f, 1.0f, &out_va) == 0;
        OmegaGpuCmdListOpts opts; memset(&opts, 0, sizeof opts);
        OmegaGpuCmdListStats stc; memset(&stc, 0, sizeof stc);
        if (ok) ok = omega_gpu_cmdlist_submit(cl, &opts, &stc) == 0;
        if (cl) omega_gpu_cmdlist_free(cl);
        for (int i = 0; i < 2; i++) if (inb[i].cpu) omega_gpu_session_free(&inb[i]);
        omega_gpu_session_unlock();
        CHECK(ok, "record two steps and submit (%s)", ok ? "" : omega_gpu_gdn_last_error());
        if (ok) {
            float *t = malloc((size_t)NT * 4), *s = malloc((size_t)NS * 4), *yo = malloc((size_t)VD * 4);
            double *td = malloc((size_t)NT * 8);
            ok = omega_gpu_gdn_get_state(g, 0, t, s) == 0 && omega_gpu_gdn_read_out(g, yo) == 0; /* the second step's output */
            for (size_t i = 0; i < NT; i++) td[i] = d.tail[i];
            Errs e = { nerr(yo, y, VD), nerr(t, td, NT), nerr(s, d.state, NS) };
            printf("chip %-40s err out %.3g tail %.3g state %.3g\n", "record path, 2 chained steps", e.out, e.tail, e.state);
            CHECK(ok && errs_ok(&e), "record path: two chained steps match the oracle");
            POINT("record path, 2 chained steps", e, "");
            free(t); free(s); free(yo); free(td);
        }
        free(tmp); free(y); dstate_free(&d); inp_free(&in[0]); inp_free(&in[1]);
    }
    /* timing */
    {
        Inp in; inp_make(&in, 950, 1.0f);
        uint64_t *ec = malloc(reps * 8), *ek = malloc(reps * 8);
        uint32_t done = 0;
        OmegaGpuGdnInfo info;
        for (uint32_t r = 0; r < reps; r++) {
            if (omega_gpu_gdn_step(g, 0, in.qkv, in.z, in.ab, EPS_L2, EPS_N, 1.0f, 1.0f, o1, &info) != 0) break;
            ek[done] = info.elapsed_ns; ec[done] = info.call_ns; done++;
        }
        CHECK(done == reps, "timing reps ran (%u of %u)", done, reps);
        if (done) {
            qsort(ek, done, 8, cmp_u64); qsort(ec, done, 8, cmp_u64);
            const double kus = ek[done / 2] / 1e3, cus = ec[done / 2] / 1e3, gbs = (double)info.state_bytes / (ek[done / 2] * 1e-9) / 1e9;
            printf("timing (medians of %u): chip %.1f us (state read + write %.2f MB -> %.1f GB/s; 273 GB/s nominal), call %.1f us; %u insns, %u registers\n",
                   done, kus, info.state_bytes / 1e6, gbs, cus, info.insns, info.gprs);
            if (out) fprintf(out, "%s{\"name\":\"timing\",\"reps\":%u,\"chip_us_median\":%.3f,\"call_us_median\":%.3f,\"state_bytes\":%" PRIu64 ",\"state_gbs\":%.2f,\"nominal_gbs\":273,\"insns\":%u,\"gprs\":%u}",
                             first ? "" : ",", done, kus, cus, info.state_bytes, gbs, info.insns, info.gprs);
            first = 0;
        }
        free(ec); free(ek); inp_free(&in);
    }
#undef POINT
    free(o1); free(o2);
    layer_free(&L[0]); layer_free(&L[1]);
    omega_gpu_gdn_free(g);
    if (out) { fprintf(out, "],\"checks\":%d,\"failed\":%d,\"verdict\":\"%s\"}\n", g_checks, g_failed, g_failed ? "FAIL" : "PASS"); fclose(out); }
    return 0;
}

int main(int argc, char **argv) {
    int host = 0, sim = 0; const char *out_path = NULL; uint32_t reps = 0, spin = 2000;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host-only") == 0) host = 1;
        else if (strcmp(argv[i], "--sim") == 0) sim = 1;
        else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) out_path = argv[++i];
        else if (strcmp(argv[i], "--reps") == 0 && i + 1 < argc) reps = (uint32_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--spin-us") == 0 && i + 1 < argc) spin = (uint32_t)atoi(argv[++i]);
        else { printf("unknown argument %s\n", argv[i]); return 2; }
    }
    if (host) host_only();
    if (sim) sim_battery();
    if (!host && !sim) {
        (void)omega_gpu_session_set_spin_us(spin);
        chip_run(out_path, reps ? reps : 50);
        omega_gpu_session_close();
    }
    printf("gpu_gdn_test: %d checks, %d failed: %s\n", g_checks, g_failed, g_failed ? "FAIL" : "PASS");
    return g_failed ? 1 : 0;
}
