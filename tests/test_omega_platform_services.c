/* Contract test of src/omega_platform_services.h (L202, cut A).
 * Hosted build: the default table is the hosted one and each service does what its libc call does.
 * -DOMEGA_PSVC_TEST_FREESTANDING (linked against the freestanding partial link): there is no default, the stub
 * refuses every service, an installed table is used, and install(NULL) restores the stub.
 * Both builds: every refusal of omega_psvc_check is reached. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "omega_platform_services.h"

#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int g_checks, g_fail;
#define CHECK(c, ...) do { g_checks++; if (!(c)) { g_fail++; fprintf(stderr, "  FAIL %s:%d ", __FILE__, __LINE__); \
        fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static uint64_t libc_now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

/* ---- a table made in the test (the shape a native platform would install) ----------------------------- */

static char g_diag[4096];
static size_t g_diag_n;
static int g_calls;
static uint64_t t_clock(void *c) { (void)c; g_calls++; return libc_now_ns(); }
static int t_yield(void *c) { (void)c; return OMEGA_PSVC_OK; }
static int t_wait(void *c, uint64_t us) { (void)c; g_calls++; usleep((useconds_t)us); return OMEGA_PSVC_OK; }
static int t_lock_create(void *c, OmegaPsvcLock *o) { (void)c; *o = (OmegaPsvcLock){ 0, 1 }; return OMEGA_PSVC_OK; }
static int t_lock_op(void *c, OmegaPsvcLock l) { (void)c; return l.generation == 1 ? OMEGA_PSVC_OK : OMEGA_PSVC_REFUSED_STALE; }
static pthread_t g_th;
static int t_task_start(void *c, OmegaPsvcTaskFn fn, void *a, OmegaPsvcTask *o) {
    (void)c;
    if (pthread_create(&g_th, NULL, fn, a) != 0) return OMEGA_PSVC_REFUSED_HOST;
    *o = (OmegaPsvcTask){ 0, 1 };
    return OMEGA_PSVC_OK;
}
static int t_task_join(void *c, OmegaPsvcTask t) { (void)c; (void)t; pthread_join(g_th, NULL); return OMEGA_PSVC_OK; }
static void *t_alloc(void *c, size_t b) { (void)c; g_calls++; return malloc(b); }
static void *t_calloc(void *c, size_t n, size_t s) { (void)c; g_calls++; return calloc(n, s); }
static void t_free(void *c, void *p) { (void)c; free(p); }
static int t_diag(void *c, const char *t) {
    (void)c;
    size_t n = strlen(t);
    if (g_diag_n + n < sizeof g_diag) { memcpy(g_diag + g_diag_n, t, n + 1); g_diag_n += n; }
    return OMEGA_PSVC_OK;
}

static OmegaPlatformServices test_table(void) {
    OmegaPlatformServices s;
    memset(&s, 0, sizeof s);
    s.abi_major = OMEGA_PSVC_ABI_MAJOR;
    s.abi_minor = OMEGA_PSVC_ABI_MINOR;
    s.services = OMEGA_PSVC_REQUIRED | OMEGA_PSVC_DIAG;
    s.clock_now_ns = t_clock;
    s.wait_us = t_wait;
    s.lock_create = t_lock_create; s.lock_acquire = t_lock_op; s.lock_release = t_lock_op; s.lock_destroy = t_lock_op;
    s.task_start = t_task_start; s.task_join = t_task_join;
    s.heap_alloc = t_alloc; s.heap_calloc = t_calloc; s.heap_free = t_free;
    s.diag_write = t_diag;
    return s;
}

/* ---- check refusals (both builds) --------------------------------------------------------------------- */

static void case_check(void) {
    OmegaPlatformServices s = test_table();
    CHECK(omega_psvc_check(&s, OMEGA_PSVC_REQUIRED) == OMEGA_PSVC_OK, "good table refused");
    CHECK(omega_psvc_check(NULL, 0) == OMEGA_PSVC_REFUSED_ARGS, "NULL");
    CHECK(omega_psvc_check(&s, 1u << 20) == OMEGA_PSVC_REFUSED_ARGS, "unknown required bit");
    OmegaPlatformServices b = s; b.abi_major = 2;
    CHECK(omega_psvc_check(&b, 0) == OMEGA_PSVC_REFUSED_ABI, "major");
    b = s; b.abi_minor = OMEGA_PSVC_ABI_MINOR + 1;
    CHECK(omega_psvc_check(&b, 0) == OMEGA_PSVC_REFUSED_ABI, "newer minor");
    b = s; b.services |= 1u << 15;
    CHECK(omega_psvc_check(&b, 0) == OMEGA_PSVC_REFUSED_ARGS, "unknown service bit");
    b = s; b.lock_release = NULL;
    CHECK(omega_psvc_check(&b, 0) == OMEGA_PSVC_REFUSED_NULL_SERVICE, "claimed LOCK with a hole");
    b = s; b.heap_calloc = NULL;
    CHECK(omega_psvc_check(&b, 0) == OMEGA_PSVC_REFUSED_NULL_SERVICE, "claimed HEAP with a hole");
    b = s; b.yield = t_yield; /* present, YIELD not claimed */
    CHECK(omega_psvc_check(&b, 0) == OMEGA_PSVC_REFUSED_HIDDEN_SERVICE, "hidden yield");
    b = s; b.services &= ~(uint32_t)OMEGA_PSVC_DIAG; /* diag_write still present */
    CHECK(omega_psvc_check(&b, 0) == OMEGA_PSVC_REFUSED_HIDDEN_SERVICE, "hidden diag");
    b = s; b.services &= ~(uint32_t)OMEGA_PSVC_TASK; b.task_start = NULL; b.task_join = NULL;
    CHECK(omega_psvc_check(&b, 0) == OMEGA_PSVC_OK, "TASK absent and not required");
    CHECK(omega_psvc_check(&b, OMEGA_PSVC_REQUIRED) == OMEGA_PSVC_REFUSED_MISSING_REQUIRED, "TASK required");
    const OmegaPlatformServices *before = omega_psvc();
    CHECK(omega_psvc_install(&b) == OMEGA_PSVC_REFUSED_MISSING_REQUIRED, "install of a table without TASK");
    CHECK(omega_psvc() == before, "a refused install changed the table");
    CHECK(omega_psvc_install(omega_psvc_stub()) == OMEGA_PSVC_REFUSED_MISSING_REQUIRED, "the stub cannot be installed");
    for (int r = 0; r < OMEGA_PSVC_RESULT_COUNT; r++) CHECK(strcmp(omega_psvc_result_name(r), "UNKNOWN") != 0, "name %d", r);
    CHECK(strcmp(omega_psvc_result_name(999), "UNKNOWN") == 0, "unknown name");
}

/* ---- the stub refuses everything, through the table and through the convenience calls ------------------- */

static void case_stub_direct(void) {
    const OmegaPlatformServices *s = omega_psvc_stub();
    CHECK(s->services == 0, "stub claims services");
    OmegaPsvcLock l = { 9, 9 };
    OmegaPsvcTask t = { 9, 9 };
    const char *v = "x";
    CHECK(s->clock_now_ns(s->ctx) == 0, "stub clock");
    CHECK(s->wait_us(s->ctx, 1) == OMEGA_PSVC_REFUSED_UNSUPPORTED, "stub wait");
    CHECK(s->yield(s->ctx) == OMEGA_PSVC_REFUSED_UNSUPPORTED, "stub yield");
    CHECK(s->lock_create(s->ctx, &l) == OMEGA_PSVC_REFUSED_UNSUPPORTED && l.generation == 0, "stub lock");
    CHECK(s->lock_acquire(s->ctx, l) == OMEGA_PSVC_REFUSED_UNSUPPORTED, "stub acquire");
    CHECK(s->task_start(s->ctx, NULL, NULL, &t) == OMEGA_PSVC_REFUSED_UNSUPPORTED && t.generation == 0, "stub task");
    CHECK(s->task_join(s->ctx, t) == OMEGA_PSVC_REFUSED_UNSUPPORTED, "stub join");
    CHECK(s->task_name_self(s->ctx, "x") == OMEGA_PSVC_REFUSED_UNSUPPORTED, "stub name");
    CHECK(s->task_pin_self(s->ctx, 0) == OMEGA_PSVC_REFUSED_UNSUPPORTED, "stub pin");
    CHECK(s->heap_alloc(s->ctx, 8) == NULL && s->heap_calloc(s->ctx, 1, 8) == NULL, "stub heap");
    CHECK(s->config_get(s->ctx, "PATH", &v) == OMEGA_PSVC_REFUSED_UNSUPPORTED && v == NULL, "stub config");
    CHECK(s->diag_write(s->ctx, "x") == OMEGA_PSVC_REFUSED_UNSUPPORTED, "stub diag");
}

static void *task_set(void *arg) { *(int *)arg = 42; return NULL; }

#ifdef OMEGA_PSVC_TEST_FREESTANDING
static void case_convenience_refuse(void) { /* the installed table is the stub */
    OmegaPsvcTask t = { 7, 7 };
    const char *v = "x";
    int flag = 0;
    CHECK(omega_psvc_now_ns() == 0, "clock");
    CHECK(omega_psvc_wait_us(1) == OMEGA_PSVC_REFUSED_UNSUPPORTED, "wait");
    CHECK(omega_psvc_yield() == OMEGA_PSVC_REFUSED_UNSUPPORTED, "yield");
    CHECK(omega_psvc_task_start(task_set, &flag, &t) == OMEGA_PSVC_REFUSED_UNSUPPORTED && t.generation == 0, "task");
    CHECK(flag == 0, "the refused task ran");
    CHECK(omega_psvc_task_name_self("x") == OMEGA_PSVC_REFUSED_UNSUPPORTED, "name");
    CHECK(omega_psvc_task_pin_self(0) == OMEGA_PSVC_REFUSED_UNSUPPORTED, "pin");
    CHECK(omega_psvc_alloc(8) == NULL && omega_psvc_calloc(1, 8) == NULL, "heap");
    omega_psvc_free(NULL);
    CHECK(omega_psvc_config("PATH", &v) == OMEGA_PSVC_REFUSED_UNSUPPORTED && v == NULL, "config");
    CHECK(omega_psvc_diagf("x %d\n", 1) == OMEGA_PSVC_REFUSED_UNSUPPORTED, "diag");
}
#endif

/* ---- an installed table is used, and its text goes through the bounded formatter ---------------------- */

static void case_installed(void) {
    static OmegaPlatformServices s;
    s = test_table();
    CHECK(omega_psvc_install(&s) == OMEGA_PSVC_OK, "install");
    CHECK(omega_psvc() == &s, "installed table not returned");
    g_calls = 0;
    uint64_t a = omega_psvc_now_ns();
    CHECK(a != 0 && g_calls == 1, "clock through the table");
    void *p = omega_psvc_calloc(4, 4);
    CHECK(p && ((uint32_t *)p)[3] == 0 && g_calls == 2, "calloc through the table");
    omega_psvc_free(p);
    int flag = 0;
    OmegaPsvcTask t;
    CHECK(omega_psvc_task_start(task_set, &flag, &t) == OMEGA_PSVC_OK && omega_psvc_task_join(t) == OMEGA_PSVC_OK, "task");
    CHECK(flag == 42, "task did not run");
    CHECK(omega_psvc_yield() == OMEGA_PSVC_REFUSED_UNSUPPORTED, "YIELD not claimed must refuse");
    const char *v = "x";
    CHECK(omega_psvc_config("PATH", &v) == OMEGA_PSVC_REFUSED_UNSUPPORTED && v == NULL, "CONFIG not claimed");
    /* every format the seat uses (src/runtime/rx_resident_gpu.c), compared with snprintf */
    g_diag_n = 0; g_diag[0] = 0;
    char want[1024];
    int w = 0;
    omega_psvc_diagf("seat build overflow %d instructions %zu vregs %d\n", -3, (size_t)70000, 12);
    w += snprintf(want + w, sizeof want - (size_t)w, "seat build overflow %d instructions %zu vregs %d\n", -3, (size_t)70000, 12);
    omega_psvc_diagf("seat lease: %llu passes in %u ms, budget %u (%u ms window)\n", 18446744073709551615ull, 50u, 20000u, 2000u);
    w += snprintf(want + w, sizeof want - (size_t)w, "seat lease: %llu passes in %u ms, budget %u (%u ms window)\n", 18446744073709551615ull, 50u, 20000u, 2000u);
    omega_psvc_diagf("seat: %s\n", "the graphics seat did not show a heartbeat");
    w += snprintf(want + w, sizeof want - (size_t)w, "seat: %s\n", "the graphics seat did not show a heartbeat");
    omega_psvc_diagf("seat lease: could not pin to cpu %s\n", "17");
    w += snprintf(want + w, sizeof want - (size_t)w, "seat lease: could not pin to cpu %s\n", "17");
    omega_psvc_diagf("seat heartbeat %u instructions %d gpr %u\n", 4294967295u, 0, 255u);
    w += snprintf(want + w, sizeof want - (size_t)w, "seat heartbeat %u instructions %d gpr %u\n", 4294967295u, 0, 255u);
    CHECK(strcmp(g_diag, want) == 0, "diag text differs:\n%s---\n%s", g_diag, want);
    /* a line longer than OMEGA_PSVC_DIAG_LINE is truncated, never overflowed */
    char big[2000];
    memset(big, 'a', sizeof big - 1); big[sizeof big - 1] = 0;
    g_diag_n = 0; g_diag[0] = 0;
    omega_psvc_diagf("%s", big);
    CHECK(g_diag_n == OMEGA_PSVC_DIAG_LINE - 1, "truncation length %zu", g_diag_n);
    CHECK(omega_psvc_install(NULL) == OMEGA_PSVC_OK, "install NULL");
}

#ifndef OMEGA_PSVC_TEST_FREESTANDING
/* ---- the hosted table does what libc does ------------------------------------------------------------- */

static void *task_name_and_pin(void *arg) {
    int *r = arg;
    r[0] = omega_psvc_task_name_self("psvc-test");
    char name[32] = { 0 };
    pthread_getname_np(pthread_self(), name, sizeof name);
    r[1] = strcmp(name, "psvc-test") == 0;
    r[2] = omega_psvc_task_pin_self(0);
    cpu_set_t set;
    CPU_ZERO(&set);
    pthread_getaffinity_np(pthread_self(), sizeof set, &set);
    r[3] = CPU_COUNT(&set) == 1 && CPU_ISSET(0, &set);
    r[4] = omega_psvc_task_pin_self(0xffffffffu); /* atoi("-1") in the seat: CPU_SET(-1) sets nothing, the call fails */
    return NULL;
}

static void case_hosted(void) {
    const OmegaPlatformServices *s = omega_psvc();
    CHECK(s == omega_psvc_hosted(), "hosted default not installed");
    CHECK(omega_psvc_check(s, OMEGA_PSVC_REQUIRED) == OMEGA_PSVC_OK, "hosted table fails its own check");
    CHECK(s->services == OMEGA_PSVC_ALL, "hosted claims 0x%x", s->services);
    uint64_t l0 = libc_now_ns(), a = omega_psvc_now_ns(), b = omega_psvc_now_ns(), l1 = libc_now_ns();
    CHECK(l0 <= a && a <= b && b <= l1, "clock not monotonic CLOCK_MONOTONIC");
    uint64_t t0 = libc_now_ns();
    CHECK(omega_psvc_wait_us(2000) == OMEGA_PSVC_OK, "wait");
    CHECK(libc_now_ns() - t0 >= 2000000ull, "wait_us(2000) slept less than 2 ms");
    CHECK(omega_psvc_yield() == OMEGA_PSVC_OK, "yield");
    /* locks: typed handles, stale after destroy, a limit */
    OmegaPsvcLock lk[64], extra;
    int made = 0;
    for (int i = 0; i < 64; i++) if (s->lock_create(s->ctx, &lk[i]) == OMEGA_PSVC_OK) made++;
    CHECK(made == 64, "made %d of 64 locks", made);
    CHECK(s->lock_create(s->ctx, &extra) == OMEGA_PSVC_REFUSED_LIMIT && extra.generation == 0, "65th lock");
    CHECK(s->lock_acquire(s->ctx, lk[3]) == OMEGA_PSVC_OK && s->lock_release(s->ctx, lk[3]) == OMEGA_PSVC_OK, "lock use");
    OmegaPsvcLock old = lk[3];
    for (int i = 0; i < 64; i++) CHECK(s->lock_destroy(s->ctx, lk[i]) == OMEGA_PSVC_OK, "destroy %d", i);
    CHECK(s->lock_acquire(s->ctx, old) == OMEGA_PSVC_REFUSED_STALE, "stale lock acquired");
    CHECK(s->lock_destroy(s->ctx, old) == OMEGA_PSVC_REFUSED_STALE, "stale lock destroyed");
    CHECK(s->lock_acquire(s->ctx, (OmegaPsvcLock){ 0, 0 }) == OMEGA_PSVC_REFUSED_STALE, "zero handle");
    CHECK(s->lock_acquire(s->ctx, (OmegaPsvcLock){ 64, 1 }) == OMEGA_PSVC_REFUSED_STALE, "index past the table");
    OmegaPsvcLock again;
    CHECK(s->lock_create(s->ctx, &again) == OMEGA_PSVC_OK && again.generation != old.generation, "slot reuse keeps the old handle stale");
    s->lock_destroy(s->ctx, again);
    /* tasks */
    int flag = 0;
    OmegaPsvcTask t;
    CHECK(omega_psvc_task_start(task_set, &flag, &t) == OMEGA_PSVC_OK, "task start");
    CHECK(omega_psvc_task_join(t) == OMEGA_PSVC_OK && flag == 42, "task join");
    CHECK(omega_psvc_task_join(t) == OMEGA_PSVC_REFUSED_STALE, "second join of one task");
    CHECK(omega_psvc_task_start(NULL, NULL, &t) == OMEGA_PSVC_REFUSED_ARGS, "NULL task function");
    int r[5] = { -1, 0, -1, 0, 0 };
    CHECK(omega_psvc_task_start(task_name_and_pin, r, &t) == OMEGA_PSVC_OK && omega_psvc_task_join(t) == OMEGA_PSVC_OK, "name/pin task");
    CHECK(r[0] == OMEGA_PSVC_OK && r[1], "task name");
    CHECK(r[2] == OMEGA_PSVC_OK && r[3], "pin to cpu 0");
    CHECK(r[4] == OMEGA_PSVC_REFUSED_HOST, "pin to cpu (int)-1 must fail like CPU_SET(-1)");
    /* heap */
    uint8_t *p = omega_psvc_calloc(16, 4);
    int zero = p != NULL;
    for (int i = 0; p && i < 64; i++) zero &= p[i] == 0;
    CHECK(zero, "calloc not zeroed");
    omega_psvc_free(p);
    void *m0 = omega_psvc_alloc(0);
    void *ref0 = malloc(0);
    CHECK((m0 == NULL) == (ref0 == NULL), "alloc(0) differs from malloc(0)");
    omega_psvc_free(m0); free(ref0);
    /* config: unset versus empty, the getenv pointer itself */
    const char *v = "x";
    unsetenv("OMEGA_PSVC_TEST_KEY");
    CHECK(omega_psvc_config("OMEGA_PSVC_TEST_KEY", &v) == OMEGA_PSVC_NOT_SET && v == NULL, "unset key");
    setenv("OMEGA_PSVC_TEST_KEY", "", 1);
    CHECK(omega_psvc_config("OMEGA_PSVC_TEST_KEY", &v) == OMEGA_PSVC_OK && v && v[0] == 0, "empty key");
    setenv("OMEGA_PSVC_TEST_KEY", "3", 1);
    CHECK(omega_psvc_config("OMEGA_PSVC_TEST_KEY", &v) == OMEGA_PSVC_OK && v == getenv("OMEGA_PSVC_TEST_KEY"), "set key");
    CHECK(omega_psvc_config(NULL, &v) == OMEGA_PSVC_REFUSED_ARGS && v == NULL, "NULL key");
    /* diag goes to stderr: capture fd 2 through a pipe */
    int fds[2];
    CHECK(pipe(fds) == 0, "pipe");
    fflush(stderr);
    int saved = dup(2);
    dup2(fds[1], 2);
    omega_psvc_diagf("seat build insns %d gpr %u\n", 133, 40u);
    fflush(stderr);
    dup2(saved, 2);
    close(saved);
    close(fds[1]);
    char got[128] = { 0 };
    ssize_t n = read(fds[0], got, sizeof got - 1);
    close(fds[0]);
    CHECK(n > 0 && strcmp(got, "seat build insns 133 gpr 40\n") == 0, "stderr got '%s'", got);
}
#endif

int main(void) {
    case_check();
    case_stub_direct();
#ifdef OMEGA_PSVC_TEST_FREESTANDING
    CHECK(omega_psvc() == omega_psvc_stub(), "freestanding default is not the stub");
    case_convenience_refuse();
    case_installed();
    CHECK(omega_psvc() == omega_psvc_stub(), "install(NULL) did not restore the stub");
    printf("test_omega_platform_services (freestanding objects): %d checks, %d failed\n", g_checks, g_fail);
#else
    case_hosted();
    case_installed();
    CHECK(omega_psvc() == omega_psvc_hosted(), "install(NULL) did not restore the hosted table");
    printf("test_omega_platform_services (hosted): %d checks, %d failed\n", g_checks, g_fail);
#endif
    return g_fail ? 1 : 0;
}
