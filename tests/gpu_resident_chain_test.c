/* gpu_resident_chain_test: one resident kernel running N dependent FP8 GEMV stages with an on-chip grid barrier, versus
 * the same N stages as separate launches behind WAIT_FOR_IDLE (OWN-30B W-L2f Proof A, 2026-10-14).
 *
 * --host : codegen only (no chip): every spec builds, instruction / register counts, capacity at the default limits.
 * chip   : caller holds the GB10 lock.
 *   PROBE   co-residency: a 2-stage tiny chain launched with FORCED grids G = SMS * p (+1) and a small spin budget. The
 *           barrier can only complete when all G CTAs are resident at once, so error word 0 <=> co-resident. Tells the
 *           real capacity; the launcher's default capacity must not exceed it.
 *   CHAIN   per chain (tiny k=512 n=128 = barrier/launch cost alone; real A k=2048 -> n=4096, B k=4096 -> n=2048 =
 *           qkv / o_proj sized, alternating, own weights per stage) and per N in {2, 8, 16}, interleaved per rep:
 *             launched  N fp8 record launches (production list: WFI + invalidate between)
 *             resident  one launch, grid = capacity
 *             red       resident mutant 1 (no barrier): NEGATIVE CONTROL, must differ from launched on >= 1 rep
 *           every output buffer poisoned before every run; resident bit-identical to launched on every rep (all N
 *           buffers); control block error 0 and arrive == (N - 1) * grid.
 *           Per-stage cost = slope (T(16) - T(2)) / 14 per mode (host-clock list elapsed, median over reps).
 *   OVERSIZE  record(capacity + 1, force 0) refused with nothing recorded; then a FORCED grid of 4 x capacity with a
 *           small spin budget: the kernel must finish (no hang, no session latch) with error word != 0; then a normal
 *           resident run must again be bit-identical (recovery).
 * Lines "PROBE ...", "RES ...", "SLOPE ..." and a receipt JSON (--out). */
#include "omega_gpu_resident_api.h"
#include "omega_gpu_fp8_api.h"
#include "omega_gpu_cmdlist.h"
#include "omega_gpu_session.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks, g_failed;
#define CHECK(c, ...) do { g_checks++; if (!(c)) { g_failed++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } else { printf("ok: "); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define NMAX 16u
#define REPMAX 64u
static uint32_t lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s; }
static int cmp_u64(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }
static uint64_t median(const uint64_t *v0, uint32_t n) { uint64_t v[REPMAX]; memcpy(v, v0, n * sizeof *v); qsort(v, n, sizeof *v, cmp_u64); return v[n / 2]; }

typedef struct { uint32_t k, n; } Shape;
enum { M_LAUNCH, M_RES, M_RED, M_COUNT };
static const char *MODE[M_COUNT] = { "launched", "resident", "red" };

typedef struct {
    const char *name; uint32_t N; Shape sh[NMAX];
    OmegaGpuFp8Tensor *t[NMAX]; OmegaGpuMem tab[NMAX], buf[NMAX + 1]; size_t bytes[NMAX + 1];
    float *ref[NMAX + 1];
    OmegaGpuResident *res, *red;
} Chain;

static const OmegaResidentSpec SPEC = { 16, 1, 8, 0 }, SPEC_RED = { 16, 1, 8, 1 };

/* caller holds the lock */
static int setup(Chain *c, uint32_t *st) {
    for (uint32_t i = 0; i < c->N; i++) {
        const Shape *s = &c->sh[i];
        const size_t wn = (size_t)s->n * s->k, sn = (size_t)(s->n / 128) * (s->k / 128);
        uint8_t *w = malloc(wn); uint16_t *sc = malloc(sn * 2);
        for (size_t j = 0; j < wn; j++) { uint8_t b = (uint8_t)(lcg(st) >> 24); w[j] = (uint8_t)((b & 0x80) | (b & 0x3F)); }
        const uint16_t sbits = (uint16_t)((127u + 64u - (s->k >= 4096 ? 8u : 7u)) << 7);
        for (size_t j = 0; j < sn; j++) sc[j] = sbits;
        omega_gpu_session_unlock();
        int rc = omega_gpu_fp8_tensor_upload(1, s->n, s->k, w, sc, &c->t[i]);
        omega_gpu_session_lock();
        free(w); free(sc);
        if (rc != OMEGA_GPU_FP8_OK) { printf("upload %s[%u]: %s (%s)\n", c->name, i, omega_gpu_fp8_rc_name(rc), omega_gpu_fp8_last_error()); return -1; }
        if (omega_gpu_session_alloc(16u, &c->tab[i]) != 0) return -1;
        uint64_t *tw = c->tab[i].cpu;
        if (omega_gpu_fp8_tensor_entry(c->t[i], 0, &tw[0], &tw[1]) != 0 || omega_gpu_session_sync_to_gpu(&c->tab[i]) != 0) return -1;
    }
    for (uint32_t i = 0; i <= c->N; i++) {
        c->bytes[i] = i == 0 ? (size_t)c->sh[0].k * 4u : (size_t)c->sh[i - 1].n * 4u;
        if (omega_gpu_session_alloc(c->bytes[i], &c->buf[i]) != 0) return -1;
        c->ref[i] = malloc(c->bytes[i]);
    }
    float *x = malloc(c->bytes[0]);
    for (size_t j = 0; j < c->bytes[0] / 4; j++) x[j] = (float)(lcg(st) >> 8) / 16777216.0f * 2.0f - 1.0f;
    omega_fp8_pack_x_f32(x, c->sh[0].k, (float *)c->buf[0].cpu);
    free(x);
    if (omega_gpu_session_sync_to_gpu(&c->buf[0]) != 0) return -1;
    OmegaGpuResidentStage stg[NMAX];
    for (uint32_t i = 0; i < c->N; i++) {
        const uint64_t *tw = c->tab[i].cpu;
        stg[i] = (OmegaGpuResidentStage){ tw[0], tw[1], c->buf[i].va, c->buf[i + 1].va, c->sh[i].k, c->sh[i].n };
    }
    int rc = omega_gpu_resident_create(&SPEC, stg, c->N, &c->res);
    if (rc == 0) rc = omega_gpu_resident_create(&SPEC_RED, stg, c->N, &c->red);
    if (rc != 0) { printf("resident create %s: %d (%s)\n", c->name, rc, omega_gpu_session_last_error()); return -1; }
    return 0;
}
static void teardown(Chain *c) {
    for (uint32_t i = 0; i < c->N; i++) { omega_gpu_session_unlock(); omega_gpu_fp8_tensor_free(c->t[i]); omega_gpu_session_lock(); omega_gpu_session_free(&c->tab[i]); }
    for (uint32_t i = 0; i <= c->N; i++) { omega_gpu_session_free(&c->buf[i]); free(c->ref[i]); }
    omega_gpu_resident_free(c->res); omega_gpu_resident_free(c->red);
}

/* caller holds the lock. grid 0 = capacity. *ns list elapsed. 0 ok */
static int run(OmegaGpuCmdList *cl, Chain *c, int mode, uint32_t grid, uint32_t spin, int force, uint64_t *ns) {
    for (uint32_t i = 1; i <= c->N; i++) { uint32_t *p = c->buf[i].cpu; for (size_t j = 0; j < c->bytes[i] / 4; j++) p[j] = 0xC479C000u; /* -999.0f */
        if (omega_gpu_session_sync_to_gpu(&c->buf[i]) != 0) return -1; }
    omega_gpu_cmdlist_reset(cl);
    int rc = 0;
    if (mode == M_LAUNCH) {
        for (uint32_t i = 0; i < c->N && rc == 0; i++)
            if (omega_gpu_fp8_gemv_grouped_record(cl, c->t[i], 1, c->tab[i].va, c->buf[i].va, 1, c->buf[i + 1].va) != OMEGA_GPU_FP8_OK) { printf("record: %s\n", omega_gpu_fp8_last_error()); rc = -1; }
    } else rc = omega_gpu_resident_record(cl, mode == M_RES ? c->res : c->red, grid, spin, force);
    if (rc != 0) { printf("record %s %s: %d (%s)\n", MODE[mode], c->name, rc, omega_gpu_session_last_error()); return -1; }
    OmegaGpuCmdListOpts o; memset(&o, 0, sizeof o); o.mode = OMEGA_GPU_CMDLIST_PCAS; o.barrier = OMEGA_GPU_CMDLIST_BARRIER_WFI;
    OmegaGpuCmdListStats s; memset(&s, 0, sizeof s);
    if (omega_gpu_cmdlist_submit(cl, &o, &s) != 0) { printf("submit %s %s: %s\n", MODE[mode], c->name, omega_gpu_session_last_error()); return -1; }
    *ns = s.elapsed_ns;
    for (uint32_t i = 1; i <= c->N; i++) if (omega_gpu_session_sync_to_cpu(&c->buf[i]) != 0) return -1;
    return 0;
}
static int same(const Chain *c) { for (uint32_t i = 1; i <= c->N; i++) if (memcmp(c->buf[i].cpu, c->ref[i], c->bytes[i]) != 0) return 0; return 1; }

static void shapes(Chain *c, const char *name, uint32_t N) {
    memset(c, 0, sizeof *c); c->name = name; c->N = N;
    for (uint32_t i = 0; i < N; i++)
        c->sh[i] = strcmp(name, "tiny") == 0 ? (Shape){ 512, 512 } : (i % 2 == 0) ? (Shape){ 2048, 4096 } : (Shape){ 4096, 2048 };
}

static int host_only(void) {
    const OmegaResidentSpec specs[] = { { 16, 1, 8, 0 }, { 16, 1, 8, 1 }, { 8, 1, 8, 0 }, { 16, 1, 32, 0 }, { 16, 1, 16, 0 } };
    for (unsigned i = 0; i < sizeof specs / sizeof *specs; i++) {
        uint32_t insns = 0, gprs = 0;
        int rc = omega_resident_prog_stats(&specs[i], &insns, &gprs);
        uint32_t decl = omega_bw_gpr_declared(gprs), thr = 32u * specs[i].warps;
        uint32_t per = OMEGA_GPU_RESIDENT_BLOCKS_PER_SM; if (OMEGA_GPU_RESIDENT_THREADS_PER_SM / thr < per) per = OMEGA_GPU_RESIDENT_THREADS_PER_SM / thr;
        if (65536u / (thr * decl) < per) per = 65536u / (thr * decl);
        CHECK(rc == 0, "codegen lb=%u warps=%u mutant=%d: %u insns, peak %u regs (declared %u), %u CTAs/SM at default limits",
              specs[i].lane_bytes, specs[i].warps, specs[i].mutant, insns, gprs, decl, per);
    }
    const OmegaResidentSpec bad[] = { { 16, 2, 8, 0 }, { 4, 1, 8, 0 }, { 16, 1, 1, 0 }, { 16, 1, 8, 2 } };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) CHECK(omega_resident_prog_stats(&bad[i], NULL, NULL) != 0, "codegen refuses bad spec %u", i);
    /* encode the production kernel to a file for nvdisasm */
    static BlackwellIRProgram prog;
    if (omega_blackwell_codegen_resident_prog(&specs[0], &prog) == 0) {
        size_t need = omega_bw_program_code_bytes(&prog);
        uint8_t *buf = malloc(need); OmegaBlackwellKernel k;
        if (buf && omega_blackwell_kernel_encode_into(&prog, buf, need, &k) == 0) { FILE *f = fopen("/tmp/wl2f-resident.bin", "wb"); if (f) { fwrite(buf, 1, need, f); fclose(f); } }
        CHECK(buf != NULL, "production kernel encoded (%zu bytes) to /tmp/wl2f-resident.bin", need);
        free(buf);
    }
    printf("gpu_resident_chain_test --host: %d checks, %d failed: %s\n", g_checks, g_failed, g_failed ? "FAIL" : "PASS");
    return g_failed ? 1 : 0;
}

int main(int argc, char **argv) {
    uint32_t reps = 9; const char *out_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0) return host_only();
        else if (strcmp(argv[i], "--reps") == 0 && i + 1 < argc) reps = (uint32_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) out_path = argv[++i];
        else { printf("usage: %s [--host] [--reps R] [--out receipt.json]\n", argv[0]); return 2; }
    }
    if (reps < 3 || reps > REPMAX) reps = 9;
    (void)omega_gpu_session_set_spin_us(2000);
    omega_gpu_fp8_set_variant(1);
    omega_gpu_fp8_set_weight_cache(0);
    OmegaGpuCmdList *cl = NULL;
    if (omega_gpu_cmdlist_create(NMAX + 2, &cl) != 0) { printf("cmdlist create: %s\n", omega_gpu_session_last_error()); return 1; }
    FILE *out = out_path ? fopen(out_path, "w") : NULL;
    if (out) fprintf(out, "{\"test\":\"OMEGA_GPU_RESIDENT_CHAIN\",\"reps\":%u,\"spec\":{\"lane_bytes\":16,\"rows\":1,\"warps\":8}", reps);
    uint32_t st = 2026;
    omega_gpu_session_lock();
    if (!omega_gpu_session_open()) { printf("open: %s\n", omega_gpu_session_last_error()); return 1; }

    /* ---- PROBE: co-residency ---- */
    uint32_t cap = 0, max_ok = 0, min_bad = 0;
    {
        Chain c; shapes(&c, "tiny", 2);
        if (setup(&c, &st) != 0) { printf("probe setup failed\n"); return 1; }
        cap = omega_gpu_resident_capacity(c.res);
        printf("capacity at default limits: %u CTAs (declared regs %u)\n", cap, omega_gpu_resident_gpr_declared(c.res));
        if (out) fprintf(out, ",\"default_capacity\":%u,\"declared_regs\":%u,\"probe\":[", cap, omega_gpu_resident_gpr_declared(c.res));
        int firstp = 1;
        const uint32_t sms = OMEGA_GPU_RESIDENT_SMS;
        for (uint32_t p = 1; p <= 8 && !omega_gpu_session_is_blocked(); p++)
            for (uint32_t d = 0; d <= 1; d++) {
                const uint32_t G = sms * p + d;
                uint64_t ns = 0; uint32_t arr = 0, err = 0;
                int rc = run(cl, &c, M_RES, G, 4096, 1, &ns);
                if (rc == 0) rc = omega_gpu_resident_status(c.res, &arr, &err);
                printf("PROBE grid=%u per_sm=%u%s rc=%d error=%u arrive=%u elapsed_us=%.1f\n", G, p, d ? "+1" : "", rc, err, arr, ns / 1e3);
                if (out) { fprintf(out, "%s{\"grid\":%u,\"rc\":%d,\"error\":%u,\"arrive\":%u,\"ns\":%" PRIu64 "}", firstp ? "" : ",", G, rc, err, arr, ns); firstp = 0; }
                if (rc != 0) break;
                if (err == 0 && G > max_ok) max_ok = G;
                if (err != 0 && (min_bad == 0 || G < min_bad)) min_bad = G;
            }
        if (out) fprintf(out, "]");
        CHECK(max_ok > 0 && (min_bad == 0 || max_ok < min_bad), "probe: co-resident up to %u CTAs, first non-co-resident grid %u (monotonic)", max_ok, min_bad);
        CHECK(cap <= max_ok, "launcher capacity %u <= measured co-resident %u", cap, max_ok);
        if (cap > max_ok && max_ok) { omega_gpu_resident_set_limits(0, 0, max_ok / sms); cap = omega_gpu_resident_capacity(c.res); printf("capacity lowered to %u\n", cap); }
        teardown(&c);
    }
    if (out) fprintf(out, ",\"capacity\":%u,\"coresident_max\":%u,\"points\":[", cap, max_ok);

    /* ---- CHAIN ---- */
    int firstpt = 1;
    double tmed[2][M_COUNT][3]; memset(tmed, 0, sizeof tmed);
    const uint32_t NS[3] = { 2, 8, 16 };
    const char *chains[2] = { "tiny", "real" };
    for (int ci = 0; ci < 2 && !omega_gpu_session_is_blocked(); ci++)
        for (int ni = 0; ni < 3; ni++) {
            Chain c; shapes(&c, chains[ci], NS[ni]);
            if (setup(&c, &st) != 0) { g_failed++; printf("setup %s N=%u failed\n", c.name, c.N); continue; }
            uint64_t t[M_COUNT][REPMAX]; int ident = 1, red_differs = 0, ctl_ok = 1, ok = 1; uint64_t dummy;
            if (run(cl, &c, M_LAUNCH, 0, 1u << 22, 0, &dummy) != 0) ok = 0;
            for (uint32_t i = 1; i <= c.N && ok; i++) memcpy(c.ref[i], c.buf[i].cpu, c.bytes[i]);
            if (ok) { const float *y = c.ref[c.N]; size_t nz = 0, bad = 0;
                for (size_t j = 0; j < c.bytes[c.N] / 4; j++) { if (y[j] != y[j] || y[j] == -999.0f) bad++; else if (y[j] != 0) nz++; }
                CHECK(bad == 0 && nz > c.bytes[c.N] / 8, "%s N=%u: launched chain output finite and written (%zu non-zero)", c.name, c.N, nz); }
            for (uint32_t r = 0; r < reps && ok; r++)
                for (int m = 0; m < M_COUNT && ok; m++) {
                    const int mode = (m + (int)r) % M_COUNT;
                    if (run(cl, &c, mode, 0, 1u << 22, 0, &t[mode][r]) != 0) { ok = 0; break; }
                    const int eq = same(&c);
                    if (mode == M_RED) { if (!eq) red_differs = 1; }
                    else if (!eq) { ident = 0; if (mode == M_RES) printf("  rep %u resident differs\n", r); }
                    if (mode == M_RES) { uint32_t arr = 0, err = 0; omega_gpu_resident_status(c.res, &arr, &err);
                        if (err != 0 || arr != (c.N - 1) * cap) { ctl_ok = 0; printf("  rep %u ctl error=%u arrive=%u want %u\n", r, err, arr, (c.N - 1) * cap); } }
                    if (omega_gpu_session_is_blocked()) { printf("session latched\n"); ok = 0; }
                }
            CHECK(ok, "%s N=%u: %u reps x 3 modes ran", c.name, c.N, reps);
            if (ok) {
                CHECK(ident, "%s N=%u: resident bit-identical to launched on every rep (all %u stage outputs)", c.name, c.N, c.N);
                CHECK(ctl_ok, "%s N=%u: control block error 0, arrive == (N-1) x grid on every rep", c.name, c.N);
                CHECK(red_differs, "%s N=%u: NEGATIVE CONTROL red (no barrier) differs on >= 1 rep", c.name, c.N);
                for (int m = 0; m < M_COUNT; m++) {
                    tmed[ci][m][ni] = median(t[m], reps) / 1e3;
                    printf("RES chain=%s N=%u mode=%s median_us=%.1f per_stage_us=%.2f\n", c.name, c.N, MODE[m], tmed[ci][m][ni], tmed[ci][m][ni] / c.N);
                    if (out) { fprintf(out, "%s{\"chain\":\"%s\",\"N\":%u,\"mode\":\"%s\",\"median_ns\":%" PRIu64 ",\"times_ns\":[", firstpt ? "" : ",", c.name, c.N, MODE[m], median(t[m], reps));
                               for (uint32_t r = 0; r < reps; r++) { fprintf(out, "%s%" PRIu64, r ? "," : "", t[m][r]); } fprintf(out, "]}"); firstpt = 0; }
                }
            }
            teardown(&c);
        }
    if (out) fprintf(out, "],\"slopes\":[");
    for (int ci = 0; ci < 2; ci++)
        for (int m = 0; m < 2; m++) {
            const double sl = (tmed[ci][m][2] - tmed[ci][m][0]) / 14.0;
            printf("SLOPE chain=%s mode=%s per_stage_us=%.2f (T16 %.1f - T2 %.1f) / 14\n", chains[ci], MODE[m], sl, tmed[ci][m][2], tmed[ci][m][0]);
            if (out) fprintf(out, "%s{\"chain\":\"%s\",\"mode\":\"%s\",\"per_stage_us\":%.3f}", ci + m ? "," : "", chains[ci], MODE[m], sl);
        }
    if (out) fprintf(out, "]");

    /* ---- OVERSIZE ---- */
    if (!omega_gpu_session_is_blocked()) {
        Chain c; shapes(&c, "real", 2);
        if (setup(&c, &st) == 0) {
            uint64_t ns = 0, dummy; uint32_t arr = 0, err = 0;
            omega_gpu_cmdlist_reset(cl);
            int rc = omega_gpu_resident_record(cl, c.res, cap + 1, 1u << 22, 0);
            CHECK(rc == OMEGA_GPU_RESIDENT_TOO_LARGE && omega_gpu_cmdlist_count(cl) == 0, "oversize grid %u refused by the launcher (rc %d, nothing recorded): %s", cap + 1, rc, omega_gpu_session_last_error());
            int ok = run(cl, &c, M_LAUNCH, 0, 1, 0, &dummy) == 0;
            for (uint32_t i = 1; i <= c.N && ok; i++) memcpy(c.ref[i], c.buf[i].cpu, c.bytes[i]);
            const uint32_t big = (max_ok ? max_ok : cap) * 4u;
            rc = ok ? run(cl, &c, M_RES, big, 4096, 1, &ns) : -1;
            if (rc == 0) rc = omega_gpu_resident_status(c.res, &arr, &err);
            printf("OVERSIZE forced grid=%u rc=%d error=%u arrive=%u elapsed_us=%.1f latched=%d\n", big, rc, err, arr, ns / 1e3, omega_gpu_session_is_blocked());
            CHECK(rc == 0 && err != 0 && !omega_gpu_session_is_blocked() && ns < 2000000000ull, "forced oversize grid %u: kernel finished (%.1f ms, no hang, no latch), error word %u != 0", big, ns / 1e6, err);
            uint64_t ns2 = 0;
            rc = run(cl, &c, M_RES, 0, 1u << 22, 0, &ns2);
            if (rc == 0) rc = omega_gpu_resident_status(c.res, &arr, &err);
            CHECK(rc == 0 && err == 0 && same(&c), "after the oversize run: resident at capacity bit-identical again, error 0 (recovery)");
            if (out) fprintf(out, ",\"oversize\":{\"grid\":%u,\"error\":%u,\"ns\":%" PRIu64 "}", big, err, ns);
            teardown(&c);
        } else g_failed++;
    }
    omega_gpu_session_unlock();
    if (out) { fprintf(out, ",\"checks\":%d,\"failed\":%d,\"verdict\":\"%s\"}\n", g_checks, g_failed, g_failed ? "FAIL" : "PASS"); fclose(out); }
    printf("gpu_resident_chain_test: %d checks, %d failed: %s\n", g_checks, g_failed, g_failed ? "FAIL" : "PASS");
    omega_gpu_cmdlist_free(cl);
    return g_failed ? 1 : 0;
}
