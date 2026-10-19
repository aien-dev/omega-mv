/*
 * gpu_qmd_smem_test: the launch descriptor's shared-memory cap above 32 KiB (omega-mv#560, cascade/prefill-kernels,
 * 2026-10-19). omega_blackwell_build_qmd1 used to refuse shared_bytes > 32768 because word 36's MIN / TARGET
 * SM_CONFIG_SHARED_MEM_SIZE fields were fixed at 9 (32 KB); they now follow the size (17 = 64 KB, 26 = 100 KB).
 *   --host-only   word 36 bits for sizes at and below the old cap are byte-identical to the captured word; above it the
 *                 two fields carry the size code; the new cap (99 KiB) and the refusal past it.
 *   (default)     the chip: one probe kernel (256 threads) writes a CTA- and thread-specific word at the top and the
 *                 bottom of the CTA's shared memory, BAR.SYNC, reads its neighbour's words back and stores them to a
 *                 global buffer; run at 32 KiB (the old cap, control), 33 KiB, 64 KiB, 96 KiB and 99 KiB with 96 CTAs
 *                 (two per SM). Every word must come back right. The simulator runs the same program first.
 * Host and simulator passes are never chip results.
 */
#include "omega_blackwell_codegen.h"
#include "omega_blackwell_qmd.h"
#include "omega_gpu_session.h"
#include "bw_warp_sim_pa.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks, g_failed;
#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { g_failed++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define THREADS 256u
#define CTAS 96u
#define NOREG (-1)
#define CW(stall, wbar, rbar, wait) (((uint32_t)(stall) << 9) | ((uint32_t)(wbar) << 14) | ((uint32_t)(rbar) << 17) | ((uint32_t)(wait) << 20))
#define ARG_OUT 0x380u
#define ARG_TOP 0x388u   /* u32 byte offset of the top word block (shared_bytes - 1024) */

static BlackwellIRInsn I0(BlackwellIROpcode op) {
    BlackwellIRInsn i; memset(&i, 0, sizeof i);
    i.op = op; i.dst_vreg = NOREG; i.src1_vreg = NOREG; i.src2_vreg = NOREG; i.src3_vreg = NOREG; i.ureg = NOREG;
    return i;
}
/* every instruction waits for everything before it (SB0..SB5) and sets SB0 (gpr) / SB1 (rd); slow and safe for a probe */
static int app(BlackwellIRProgram *p, BlackwellIRInsn in, uint32_t stall, unsigned wbar, unsigned rbar) {
    in.control = CW(stall, wbar, rbar, 0x3f);
    return omega_bw_ir_append(p, &in) < 0 ? -1 : 0;
}
/* The probe program. Thread t of CTA c: hi = (c << 16) | t, lo = hi ^ 0xa5a5a5a5.
 *   sts32 [top + 4t] = hi; sts32 [4t] = lo; bar; a = lds32 [top + 4((t + 1) & 255)]; b = lds32 [4((t + 3) & 255)];
 *   out[(c * 256 + t) * 2 + 0] = a; out[.. + 1] = b. */
static int probe_prog(BlackwellIRProgram *p) {
    omega_bw_ir_init(p);
    int udesc = omega_bw_ir_alloc_uvreg64(p), pout = omega_bw_ir_alloc_vreg64(p), top = omega_bw_ir_alloc_vreg(p);
    int tid = omega_bw_ir_alloc_vreg(p), cx = omega_bw_ir_alloc_vreg(p), hi = omega_bw_ir_alloc_vreg(p), lo = omega_bw_ir_alloc_vreg(p);
    int t4 = omega_bw_ir_alloc_vreg(p), atop = omega_bw_ir_alloc_vreg(p), n1 = omega_bw_ir_alloc_vreg(p), n3 = omega_bw_ir_alloc_vreg(p);
    int a = omega_bw_ir_alloc_vreg(p), b = omega_bw_ir_alloc_vreg(p), idx = omega_bw_ir_alloc_vreg(p), po = omega_bw_ir_alloc_vreg64(p), mask = omega_bw_ir_alloc_vreg(p);
    if (udesc < 0 || pout < 0 || po < 0 || mask < 0) return -1;
    BlackwellIRInsn i;
    i = I0(BW_IR_LDCU64); i.dst_vreg = udesc; i.imm = 0x358; i.is_uniform = true; if (app(p, i, 6, 2, 7)) return -1;
    i = I0(BW_IR_LDC64); i.dst_vreg = pout; i.imm = ARG_OUT; if (app(p, i, 6, 0, 1)) return -1;
    i = I0(BW_IR_LDC); i.dst_vreg = top; i.imm = ARG_TOP; if (app(p, i, 6, 0, 1)) return -1;
    i = I0(BW_IR_S2R); i.dst_vreg = tid; i.imm = BW_SR_TID_X; if (app(p, i, 6, 0, 1)) return -1;
    i = I0(BW_IR_S2R); i.dst_vreg = cx; i.imm = BW_SR_CTAID_X; if (app(p, i, 6, 0, 1)) return -1;
    i = I0(BW_IR_IMAD); i.dst_vreg = hi; i.src1_vreg = cx; i.imm = 65536u; i.src3_vreg = tid; if (app(p, i, 6, 7, 7)) return -1;        /* hi = cx * 65536 + tid */
    i = I0(BW_IR_MOV_IMM); i.dst_vreg = mask; i.imm = 0xa5a5a5a5u; if (app(p, i, 6, 7, 7)) return -1;
    i = I0(BW_IR_LOP3_XOR); i.dst_vreg = lo; i.src1_vreg = hi; i.src2_vreg = mask; if (app(p, i, 6, 7, 7)) return -1;
    i = I0(BW_IR_IMAD); i.dst_vreg = t4; i.src1_vreg = tid; i.imm = 4u; i.src3_vreg = NOREG; if (app(p, i, 6, 7, 7)) return -1;
    i = I0(BW_IR_IADD3); i.dst_vreg = atop; i.src1_vreg = top; i.src2_vreg = t4; if (app(p, i, 6, 7, 7)) return -1;
    i = I0(BW_IR_STS32); i.src1_vreg = atop; i.src2_vreg = hi; if (app(p, i, 6, 7, 1)) return -1;
    i = I0(BW_IR_STS32); i.src1_vreg = t4; i.src2_vreg = lo; if (app(p, i, 6, 7, 1)) return -1;
    i = I0(BW_IR_BAR_SYNC); if (app(p, i, 6, 7, 7)) return -1;
    /* n1 = ((tid + 1) & 255) * 4 + top ; n3 = ((tid + 3) & 255) * 4 */
    i = I0(BW_IR_IMAD); i.dst_vreg = n1; i.src1_vreg = tid; i.imm = 1u; i.src3_vreg = NOREG; if (app(p, i, 6, 7, 7)) return -1;
    { BlackwellIRInsn one = I0(BW_IR_MOV_IMM); one.dst_vreg = n3; one.imm = 1u; if (app(p, one, 6, 7, 7)) return -1; }
    i = I0(BW_IR_IADD3); i.dst_vreg = n1; i.src1_vreg = tid; i.src2_vreg = n3; if (app(p, i, 6, 7, 7)) return -1;      /* tid + 1 */
    i = I0(BW_IR_LOP3_AND); i.dst_vreg = n1; i.src1_vreg = n1; i.imm = 255u; if (app(p, i, 6, 7, 7)) return -1;
    i = I0(BW_IR_IMAD); i.dst_vreg = n1; i.src1_vreg = n1; i.imm = 4u; i.src3_vreg = top; if (app(p, i, 6, 7, 7)) return -1;
    { BlackwellIRInsn three = I0(BW_IR_MOV_IMM); three.dst_vreg = n3; three.imm = 3u; if (app(p, three, 6, 7, 7)) return -1; }
    i = I0(BW_IR_IADD3); i.dst_vreg = n3; i.src1_vreg = tid; i.src2_vreg = n3; if (app(p, i, 6, 7, 7)) return -1;      /* tid + 3 */
    i = I0(BW_IR_LOP3_AND); i.dst_vreg = n3; i.src1_vreg = n3; i.imm = 255u; if (app(p, i, 6, 7, 7)) return -1;
    i = I0(BW_IR_IMAD); i.dst_vreg = n3; i.src1_vreg = n3; i.imm = 4u; i.src3_vreg = NOREG; if (app(p, i, 6, 7, 7)) return -1;
    i = I0(BW_IR_LDS32); i.dst_vreg = a; i.src1_vreg = n1; if (app(p, i, 6, 0, 1)) return -1;
    i = I0(BW_IR_LDS32); i.dst_vreg = b; i.src1_vreg = n3; if (app(p, i, 6, 0, 1)) return -1;
    /* out + ((cx * 256 + tid) * 8) */
    i = I0(BW_IR_IMAD); i.dst_vreg = idx; i.src1_vreg = cx; i.imm = THREADS; i.src3_vreg = tid; if (app(p, i, 6, 7, 7)) return -1;
    i = I0(BW_IR_IMAD_WIDE); i.dst_vreg = po; i.src1_vreg = idx; i.imm = 8u; i.src3_vreg = pout; if (app(p, i, 6, 7, 7)) return -1;
    i = I0(BW_IR_STG_E); i.src1_vreg = po; i.src2_vreg = a; i.imm = 0; i.ureg = udesc; if (app(p, i, 6, 7, 1)) return -1;
    i = I0(BW_IR_STG_E); i.src1_vreg = po; i.src2_vreg = b; i.imm = 4; i.ureg = udesc; if (app(p, i, 6, 7, 1)) return -1;
    i = I0(BW_IR_EXIT); if (app(p, i, 5, 7, 7)) return -1;
    { BlackwellIRInsn self = I0(BW_IR_BRA); self.control = 0x000fc000u; if (omega_bw_ir_append(p, &self) < 0) return -1; }
    return omega_bw_regalloc_solve(p);
}
static size_t check_out(const uint32_t *o, uint32_t ctas) {
    size_t bad = 0;
    for (uint32_t c = 0; c < ctas; c++) for (uint32_t t = 0; t < THREADS; t++) {
        const uint32_t hi = ((c << 16) | ((t + 1u) & 255u)), lo = ((c << 16) | ((t + 3u) & 255u)) ^ 0xa5a5a5a5u;
        if (o[(c * THREADS + t) * 2] != hi) bad++;
        if (o[(c * THREADS + t) * 2 + 1] != lo) bad++;
    }
    return bad;
}

static void host_only(void) {
    uint32_t w[OMEGA_BW_QMD_WORDS];
    OmegaBlackwellQmdConfig cfg; memset(&cfg, 0, sizeof cfg);
    cfg.threads_x = 256; cfg.grid_x = 1;
    static const uint32_t same[] = { 0, 1024, 16384, 20480, 32768 };
    for (size_t i = 0; i < sizeof same / sizeof same[0]; i++) {
        cfg.shared_bytes = same[i];
        int rc = omega_blackwell_build_qmd1(w, &cfg);
        const uint32_t smem7 = same[i] ? (same[i] + 127u) / 128u : 8u, want = (0x04b44808u & ~0x7ffu) | smem7;
        CHECK(rc == 0 && w[36] == want, "shared %u: word 36 %08x (want the captured word with the size, %08x)", same[i], w[36], want);
    }
    static const struct { uint32_t bytes, code; } up[] = { { 32769, 17 }, { 65536, 17 }, { 65537, 26 }, { 98304, 26 }, { 101376, 26 } };
    for (size_t i = 0; i < sizeof up / sizeof up[0]; i++) {
        cfg.shared_bytes = up[i].bytes;
        int rc = omega_blackwell_build_qmd1(w, &cfg);
        const uint32_t smem7 = (up[i].bytes + 127u) / 128u;
        const uint32_t min = (w[36] >> 11) & 0x3f, max = (w[36] >> 17) & 0x3f, tgt = (w[36] >> 23) & 0x3f;
        CHECK(rc == 0 && (w[36] & 0x7ff) == smem7 && min == up[i].code && tgt == up[i].code && max == 26 && (w[36] >> 29) == 0,
              "shared %u: word 36 %08x: size7 %u min %u max %u target %u (want %u, %u, 26, %u)", up[i].bytes, w[36], w[36] & 0x7ff, min, max, tgt, smem7, up[i].code, up[i].code);
        CHECK(omega_blackwell_qmd_sm_config_code(up[i].bytes) == up[i].code, "config code for %u is %u", up[i].bytes, up[i].code);
    }
    cfg.shared_bytes = OMEGA_BW_QMD_MAX_SHARED_BYTES + 1u;
    CHECK(omega_blackwell_build_qmd1(w, &cfg) != 0, "shared %u (cap + 1) refused", cfg.shared_bytes);
    CHECK(OMEGA_BW_QMD_MAX_SHARED_BYTES == 101376u, "the cap is 99 KiB");
    static BlackwellIRProgram p;
    CHECK(probe_prog(&p) == 0, "probe codegen");
    printf("probe: %zu instructions, %u registers\n", p.count, p.regalloc.peak_gpr_usage);
    /* simulator: the probe at 96 KiB */
    {
        static uint8_t smem[98304];
        uint32_t *out = calloc((size_t)4 * THREADS * 2, 4);
        WsSim s; memset(&s, 0, sizeof s);
        s.p = &p; s.threads = THREADS; s.smem = smem; s.smem_bytes = sizeof smem;
        uint64_t a = (uintptr_t)out; s.cbank[ARG_OUT / 4] = (uint32_t)a; s.cbank[ARG_OUT / 4 + 1] = (uint32_t)(a >> 32);
        s.cbank[ARG_TOP / 4] = sizeof smem - 1024u;
        s.nranges = 1; s.ranges[0].lo = (uintptr_t)out; s.ranges[0].hi = (uintptr_t)out + (size_t)4 * THREADS * 8;
        int err = ws_run(&s, 4, 1);
        size_t bad = err ? (size_t)-1 : check_out(out, 4);
        CHECK(err == 0 && bad == 0, "simulator probe at 96 KiB: %s (pc %d), %zu bad words", err ? s.msg : "ran", s.err_pc, bad);
        free(out);
    }
}

static int chip_size(uint32_t shared_bytes, const BlackwellIRProgram *p, FILE *json, int *first) {
    omega_gpu_session_lock();
    OmegaGpuMem code = { 0 }, out = { 0 };
    int rc = -1; uint64_t ns = 0; size_t bad = (size_t)-1; const char *err = "";
    if (!omega_gpu_session_open()) { err = omega_gpu_session_last_error(); goto done; }
    {
        size_t need = omega_bw_program_code_bytes(p); OmegaBlackwellKernel k;
        if (omega_gpu_session_alloc_code(need, &code) != 0 || omega_blackwell_kernel_encode_into(p, code.cpu, need, &k) != 0 || omega_gpu_session_sync_to_gpu(&code) != 0) { err = "code upload"; goto done; }
        if (omega_gpu_session_alloc((size_t)CTAS * THREADS * 8, &out) != 0) { err = "alloc"; goto done; }
        memset(out.cpu, 0xee, (size_t)CTAS * THREADS * 8);
        if (omega_gpu_session_sync_to_gpu(&out) != 0) { err = "sync"; goto done; }
        uint32_t args[4] = { (uint32_t)out.va, (uint32_t)(out.va >> 32), shared_bytes - 1024u, 0 };
        uint32_t decl = omega_bw_gpr_declared(p->regalloc.peak_gpr_usage);
        OmegaGpuLaunch L = { .code_va = code.va, .gpr_count = decl < 64 ? 64 : decl, .threads_x = THREADS, .threads_y = 1, .grid_x = CTAS, .grid_y = 1,
                             .num_elements = (size_t)CTAS * THREADS, .args = args, .n_args = 4, .timeout_ms = 10000, .shared_bytes = shared_bytes };
        if (omega_gpu_session_launch(&L, &ns, NULL) != 0) { err = omega_gpu_session_last_error(); goto done; }
        if (omega_gpu_session_sync_to_cpu(&out) != 0) { err = "sync back"; goto done; }
        bad = check_out(out.cpu, CTAS);
        rc = 0;
    }
done:
    if (code.cpu) omega_gpu_session_free(&code);
    if (out.cpu) omega_gpu_session_free(&out);
    omega_gpu_session_unlock();
    CHECK(rc == 0 && bad == 0, "chip shared %u: %s, %zu bad words", shared_bytes, rc ? err : "ran", bad);
    printf("chip shared %6u bytes (config code %u): %s, %zu bad words of %u, %" PRIu64 " ns\n", shared_bytes, omega_blackwell_qmd_sm_config_code(shared_bytes), rc ? err : "ran", bad, CTAS * THREADS * 2, ns);
    if (json) { fprintf(json, "%s  {\"shared_bytes\": %u, \"config_code\": %u, \"ran\": %d, \"bad_words\": %zu, \"ns\": %" PRIu64 "}", *first ? "" : ",\n", shared_bytes, omega_blackwell_qmd_sm_config_code(shared_bytes), rc == 0, bad, ns); *first = 0; }
    fflush(stdout);
    return rc == 0 && bad == 0;
}

int main(int argc, char **argv) {
    int host = 0; const char *jp = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--host-only")) host = 1;
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) jp = argv[++i];
        else { fprintf(stderr, "usage: %s [--host-only] [--out receipt.json]\n", argv[0]); return 2; }
    }
    host_only();
    if (!host) {
        static BlackwellIRProgram p;
        if (probe_prog(&p) == 0) {
            FILE *json = jp ? fopen(jp, "w") : NULL; int first = 1;
            if (json) fprintf(json, "{\"test\": \"gpu_qmd_smem\", \"ctas\": %u, \"threads\": %u, \"points\": [\n", CTAS, THREADS);
            static const uint32_t sizes[] = { 32768, 33792, 65536, 98304, 101376 };
            for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) if (!chip_size(sizes[i], &p, json, &first)) break;
            if (json) { fprintf(json, "\n], \"checks\": %d, \"failed\": %d}\n", g_checks, g_failed); fclose(json); }
        }
    }
    printf("gpu_qmd_smem_test: %d checks, %d failed: %s\n", g_checks, g_failed, g_failed ? "FAIL" : "PASS");
    return g_failed ? 1 : 0;
}
