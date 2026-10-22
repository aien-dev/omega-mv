/* gpu_isolation_test.c -- row C5 of docs/OMEGA.md: two processes on one GPU, and a never-ending kernel, on the GB10.
 *
 *   --host-only  codegen + encode of the work kernel, its mutant and the never-ending kernel; the simulator runs the work
 *                kernel exactly and catches the mutant; the never-ending kernel never reaches its exit in the simulator
 *   (no flag)    the chip, under the GB10 lock, every part in its own child process:
 *     hang       a work launch (values checked), then a never-ending kernel (one warp: count, branch back) with a
 *                HANG_MS timeout: the launch fails "timed out" and the process latches; red check: a work launch is
 *                refused "process latched"; omega_gpu_session_recover() closes the device (the RM client close destroys
 *                the channel and the grid on it); the work kernel uploaded again on the new device runs, values right.
 *     two        two processes, each its own device, ROUNDS work launches with their own inputs: both all right.
 *     two-hang   process 1 runs ROUNDS work launches while process 2 runs the hang part: process 1 all right (unharmed),
 *                process 2 recovers and is right too.
 * Kernels from Omega's IR (omega_bw_em.h), our own sm_121 encoder; no CUDA. */
#include "omega_gpu_session.h"
#include "bw_warp_sim.h" /* brings math.h, whose fadd() / ffma() would hide the emitter's */
#define fadd em_fadd
#define ffma em_ffma
#include "omega_bw_em.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define THREADS 128u
#define CTAS 32u
#define NEL (THREADS * CTAS)
#define ROUNDS 300
#define HANG_MS 2000ull
#define POISON 0x7fa00badu
#define ARGS 5u /* in u64, out u64, tag f32 */

static int g_failed, g_checks;
static void check(int ok, const char *what) {
    g_checks++;
    if (!ok) g_failed++;
    printf("[%d] %s: %s\n", (int)getpid(), ok ? "ok" : "FAIL", what);
    fflush(stdout);
}
static uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static uint64_t now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec; }

/* ------------------------------------------------------------------ the kernels */

typedef struct { int bssy, bra; } Region;
static Region skip_if_p0(Em *e) { Region r; r.bssy = bssy_fwd(e); r.bra = bra_if_p0_fwd(e); return r; }
static void region_end(Em *e, Region r) { int at = here(e); bsync(e); patch_bra(e, r.bra, at); patch_bra(e, r.bssy, at + 1); }

/* work: out[gid] = in[gid] * 2 + tag (mutant 1: * 2 + tag + 1) */
static int gen_work(Em *e, int mutant) {
    int udesc = UV64(e), pin = V64(e), pout = V64(e), a64 = V64(e), tag = V(e), n = V(e), tid = V(e), cta = V(e), gid = V(e), two = V(e), v = V(e), one = V(e);
    if (e->err) return -1;
    ldcu64(e, udesc, 0x358);
    ldc64(e, pin, 0x380); ldc64(e, pout, 0x388); ldc32(e, tag, 0x390);
    s2r(e, tid, BW_SR_TID_X); s2r(e, cta, BW_SR_CTAID_X);
    imadi(e, gid, cta, 0, THREADS, tid);
    movi(e, n, NEL); movi(e, two, 0x40000000u); movi(e, one, 0x3f800000u);
    isetp_ge_u32(e, gid, n);
    {
        Region r = skip_if_p0(e);
        imadw(e, a64, gid, 4u, pin); ldg32(e, v, a64, udesc, 1);
        ffma(e, v, v, two, 0, tag);
        if (mutant == 1) fadd(e, v, v, one);
        imadw(e, a64, gid, 4u, pout); stg(e, a64, 0, v, udesc);
        region_end(e, r);
    }
    tail(e);
    return e->err;
}

/* never-ending: x = x + 1, branch back, for ever; the exit after the loop is never reached */
static int gen_hang(Em *e) {
    int x = V(e), one = V(e);
    if (e->err) return -1;
    movi(e, x, 0); movi(e, one, 1);
    const int top = here(e);
    loop_begin(e);
    iadd(e, x, x, one);
    bra(e, top);
    loop_end(e);
    tail(e);
    return e->err;
}

static int codegen(int hang, int mutant, BlackwellIRProgram *prog) {
    omega_bw_ir_init(prog);
    Em em; memset(&em, 0, sizeof em); em.p = prog;
    if ((hang ? gen_hang(&em) : gen_work(&em, mutant)) != 0 || em.err || em.depth != 0) return -1;
    return omega_bw_regalloc_solve(prog);
}

static float input(unsigned i, unsigned seed) { return (float)((i * 2654435761u + seed * 40503u) % 100000u); }
static float expect(unsigned i, unsigned seed, float tag) { return input(i, seed) * 2.0f + tag; }

/* ------------------------------------------------------------------ host */

static const char *g_dump; /* --dump-hang PATH: the never-ending kernel's code bytes, for the disassembler */

static void host_only(void) {
    char w[200];
    for (int k = 0; k < 3; k++) {
        BlackwellIRProgram *p = calloc(1, sizeof *p);
        size_t need = 0;
        int ok = p && codegen(k == 2, k == 1, p) == 0 && (need = omega_bw_program_code_bytes(p)) > 0;
        void *code = ok ? calloc(1, need) : NULL;
        OmegaBlackwellKernel kk;
        ok = ok && code && omega_blackwell_kernel_encode_into(p, code, need, &kk) == 0 && kk.code_size == need;
        if (ok && k == 2 && g_dump) { FILE *f = fopen(g_dump, "wb"); ok = f && fwrite(code, 1, need, f) == need; if (f) fclose(f); }
        snprintf(w, sizeof w, "codegen + encode: %s (%zu bytes)", k == 0 ? "work kernel" : k == 1 ? "work kernel mutant 1" : "never-ending kernel", need);
        check(ok, w);
        free(code); free(p);
    }
    static float in[NEL], out[NEL];
    WsRange rg[2] = { { (uintptr_t)in, (uintptr_t)(in + NEL) }, { (uintptr_t)out, (uintptr_t)(out + NEL) } };
    for (int mut = 0; mut < 2; mut++) {
        BlackwellIRProgram *p = calloc(1, sizeof *p);
        static WsSim s;
        memset(&s, 0, sizeof s);
        for (unsigned i = 0; i < NEL; i++) { in[i] = input(i, 7); uint32_t pz = POISON; memcpy(&out[i], &pz, 4); }
        int rc = !p || codegen(0, mut, p) != 0;
        if (!rc) {
            const uint64_t a = (uintptr_t)in, b = (uintptr_t)out;
            s.p = p; s.threads = THREADS;
            s.cbank[0x380 / 4] = (uint32_t)a; s.cbank[0x384 / 4] = (uint32_t)(a >> 32);
            s.cbank[0x388 / 4] = (uint32_t)b; s.cbank[0x38c / 4] = (uint32_t)(b >> 32); s.cbank[0x390 / 4] = f2u(3.0f);
            memcpy(s.ranges, rg, sizeof rg); s.nranges = 2;
            rc = ws_run(&s, CTAS, 1);
        }
        unsigned bad = 0;
        for (unsigned i = 0; i < NEL; i++) bad += f2u(out[i]) != f2u(expect(i, 7, 3.0f));
        snprintf(w, sizeof w, mut ? "simulator red check: work mutant caught (%u wrong)" : "simulator: work kernel exact on all %u elements", mut ? bad : NEL);
        check(mut ? rc != 0 || bad > 0 : rc == 0 && bad == 0, w);
        free(p);
    }
}

/* ------------------------------------------------------------------ the chip */

typedef struct { BlackwellIRProgram prog; OmegaGpuMem code; uint32_t gpr; } Kern;

/* lock held */
static int upload(Kern *k, int hang) {
    memset(&k->code, 0, sizeof k->code);
    if (codegen(hang, 0, &k->prog) != 0) return -1;
    size_t need = omega_bw_program_code_bytes(&k->prog);
    if (need == 0 || omega_gpu_session_alloc_code(need, &k->code) != 0) return -1;
    OmegaBlackwellKernel kk;
    if (omega_blackwell_kernel_encode_into(&k->prog, k->code.cpu, need, &kk) != 0 || omega_gpu_session_sync_to_gpu(&k->code) != 0) return -1;
    k->gpr = omega_bw_gpr_declared(kk.gpr_count);
    if (k->gpr < 64u) k->gpr = 64u;
    return 0;
}

static Kern g_work, g_hang;
static OmegaGpuMem g_buf; /* [in | out] */

/* lock held, device open: device buffer + work kernel */
static int setup(void) {
    if (!omega_gpu_session_open()) return -1;
    if (omega_gpu_session_alloc(2u * NEL * 4u, &g_buf) != 0) return -1;
    return upload(&g_work, 0);
}

/* lock held: one work launch with inputs from seed; returns the count of wrong outputs, or -1 if the launch failed */
static long work_once(unsigned seed, float tag, uint64_t *ns) {
    float *in = g_buf.cpu, *out = in + NEL;
    for (unsigned i = 0; i < NEL; i++) { in[i] = input(i, seed); uint32_t pz = POISON; memcpy(&out[i], &pz, 4); }
    if (omega_gpu_session_sync_to_gpu(&g_buf) != 0) return -1;
    uint32_t a[ARGS];
    const uint64_t vi = g_buf.va, vo = g_buf.va + NEL * 4u;
    a[0] = (uint32_t)vi; a[1] = (uint32_t)(vi >> 32); a[2] = (uint32_t)vo; a[3] = (uint32_t)(vo >> 32); a[4] = f2u(tag);
    OmegaGpuLaunch L = { .code_va = g_work.code.va, .gpr_count = g_work.gpr, .threads_x = THREADS, .threads_y = 1, .grid_x = CTAS, .grid_y = 1,
                         .num_elements = NEL, .args = a, .n_args = ARGS, .timeout_ms = 20000 };
    uint64_t t = 0;
    if (omega_gpu_session_launch(&L, &t, NULL) != 0) return -1;
    if (ns) *ns = t;
    if (omega_gpu_session_sync_to_cpu(&g_buf) != 0) return -1;
    long bad = 0;
    for (unsigned i = 0; i < NEL; i++) bad += f2u(out[i]) != f2u(expect(i, seed, tag));
    return bad;
}

static int part_hang(void) {
    char w[300];
    omega_gpu_session_lock();
    if (setup() != 0) { check(0, "hang part: device, buffer, work kernel"); printf("  %s\n", omega_gpu_session_last_error()); omega_gpu_session_unlock(); return 1; }
    uint64_t ns0 = 0;
    long bad = work_once(11, 1.0f, &ns0);
    snprintf(w, sizeof w, "hang part: a work launch before the hang is right (%ld wrong, %.3f ms)", bad, ns0 / 1e6);
    check(bad == 0, w);
    check(upload(&g_hang, 1) == 0, "hang part: never-ending kernel uploaded");
    OmegaGpuLaunch H = { .code_va = g_hang.code.va, .gpr_count = g_hang.gpr, .threads_x = 32, .threads_y = 1, .grid_x = 1, .grid_y = 1,
                         .num_elements = 32, .timeout_ms = HANG_MS };
    const uint64_t t0 = now_ns();
    int rc = omega_gpu_session_launch(&H, NULL, NULL);
    const double waited = (now_ns() - t0) / 1e6;
    snprintf(w, sizeof w, "the never-ending kernel times out after %.0f ms (rc %d: %s), the process latches", waited, rc, omega_gpu_session_last_error());
    check(rc != 0 && strstr(omega_gpu_session_last_error(), "timed out") && omega_gpu_session_is_blocked() && waited >= (double)HANG_MS * 0.9, w);
    bad = work_once(12, 1.0f, NULL);
    snprintf(w, sizeof w, "red check: while latched a work launch is refused (%s)", omega_gpu_session_last_error());
    check(bad == -1 && strstr(omega_gpu_session_last_error(), "process latched") != NULL, w);
    omega_gpu_session_unlock();

    const uint64_t r0 = now_ns();
    rc = omega_gpu_session_recover();
    const double rec_ms = (now_ns() - r0) / 1e6;
    snprintf(w, sizeof w, "recover: the device closed (channel and grid destroyed) in %.1f ms (rc %d %s)", rec_ms, rc, rc ? omega_gpu_session_last_error() : "");
    check(rc == 0 && !omega_gpu_session_is_blocked() && omega_gpu_session_recoveries() == 1, w);
    memset(&g_buf, 0, sizeof g_buf); /* handles of the old device are gone */
    omega_gpu_session_lock();
    if (setup() != 0) { check(0, "after recover: new device, buffer, work kernel"); printf("  %s\n", omega_gpu_session_last_error()); omega_gpu_session_unlock(); return 1; }
    uint64_t ns1 = 0, ns2 = 0;
    bad = work_once(13, 4.0f, &ns1);
    long bad2 = work_once(14, 5.0f, &ns2);
    snprintf(w, sizeof w, "after recover, same process: work launches right (%ld, %ld wrong; %.3f, %.3f ms vs %.3f ms before the hang), device opens %u",
             bad, bad2, ns1 / 1e6, ns2 / 1e6, ns0 / 1e6, omega_gpu_session_open_count());
    check(bad == 0 && bad2 == 0, w);
    omega_gpu_session_unlock();
    omega_gpu_session_close();
    return g_failed ? 1 : 0;
}

static int part_rounds(unsigned seed_base, const char *who) {
    char w[300];
    omega_gpu_session_lock();
    if (setup() != 0) { check(0, "rounds: device, buffer, work kernel"); printf("  %s\n", omega_gpu_session_last_error()); omega_gpu_session_unlock(); return 1; }
    long wrong_total = 0; int failed = 0;
    uint64_t ns, worst = 0;
    for (int r = 0; r < ROUNDS; r++) {
        long bad = work_once(seed_base + (unsigned)r, (float)(r % 7), &ns);
        if (bad < 0) { failed++; printf("  [%d] round %d: %s\n", (int)getpid(), r, omega_gpu_session_last_error()); break; }
        wrong_total += bad;
        if (ns > worst) worst = ns;
    }
    snprintf(w, sizeof w, "%s: %d work launches, own inputs: %ld wrong values, %d failed launches, slowest %.3f ms", who, ROUNDS, wrong_total, failed, worst / 1e6);
    check(wrong_total == 0 && failed == 0, w);
    omega_gpu_session_unlock();
    omega_gpu_session_close();
    return g_failed ? 1 : 0;
}

static pid_t spawn(int (*fn)(unsigned, const char *), unsigned seed, const char *who) {
    fflush(stdout);
    pid_t p = fork();
    if (p == 0) _exit(fn ? fn(seed, who) : part_hang());
    return p;
}
static int part_hang_adapter(unsigned s, const char *w) { (void)s; (void)w; return part_hang(); }
static int reap(pid_t p) { int st = 0; if (p < 0 || waitpid(p, &st, 0) != p) return -1; return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + (WIFSIGNALED(st) ? WTERMSIG(st) : 0); }

static void chip_tier(void) {
    char w[200];
    int rc = reap(spawn(part_hang_adapter, 0, "hang"));
    snprintf(w, sizeof w, "C5 hang: never-ending kernel timed out, recovered, correct launch in the same process (child exit %d)", rc);
    check(rc == 0, w);

    pid_t a = spawn(part_rounds, 1000, "process 1"), b = spawn(part_rounds, 5000, "process 2");
    int ra = reap(a), rb = reap(b);
    snprintf(w, sizeof w, "C5 two processes on one GPU: both right (exits %d, %d)", ra, rb);
    check(ra == 0 && rb == 0, w);

    a = spawn(part_rounds, 9000, "process 1 beside a hang");
    b = spawn(part_hang_adapter, 0, "hang");
    ra = reap(a); rb = reap(b);
    snprintf(w, sizeof w, "C5 a hang in process 2 leaves process 1 unharmed (exits %d, %d)", ra, rb);
    check(ra == 0 && rb == 0, w);
}

int main(int argc, char **argv) {
    const char *tier = "chip";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--host-only")) tier = "host";
        else if (!strcmp(argv[i], "--dump-hang") && i + 1 < argc) { tier = "host"; g_dump = argv[++i]; }
        else { fprintf(stderr, "usage: %s [--host-only] [--dump-hang PATH]\n", argv[0]); return 2; }
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (!strcmp(tier, "host")) host_only();
    else chip_tier();
    if (g_failed) printf("C5 isolation %s: FAIL (%d of %d)\n", tier, g_failed, g_checks);
    else printf("C5 isolation %s: PASS (%d checks)\n", tier, g_checks);
    return g_failed ? 1 : 0;
}
