/* omega_platform_services.c -- contract check, the refusing stub, install and the convenience calls
 * (L202, cut A). Freestanding: no libc call; the hosted table lives in omega_platform_services_hosted.c. */
#include "omega_platform_services.h"
#include "omega_gpu_fmt.h"

#include <stdarg.h>

const char *omega_psvc_result_name(int r) {
    switch (r) {
    case OMEGA_PSVC_OK: return "OK";
    case OMEGA_PSVC_NOT_SET: return "NOT_SET";
    case OMEGA_PSVC_REFUSED_ARGS: return "REFUSED_ARGS";
    case OMEGA_PSVC_REFUSED_ABI: return "REFUSED_ABI";
    case OMEGA_PSVC_REFUSED_UNSUPPORTED: return "REFUSED_UNSUPPORTED";
    case OMEGA_PSVC_REFUSED_NULL_SERVICE: return "REFUSED_NULL_SERVICE";
    case OMEGA_PSVC_REFUSED_HIDDEN_SERVICE: return "REFUSED_HIDDEN_SERVICE";
    case OMEGA_PSVC_REFUSED_MISSING_REQUIRED: return "REFUSED_MISSING_REQUIRED";
    case OMEGA_PSVC_REFUSED_STALE: return "REFUSED_STALE";
    case OMEGA_PSVC_REFUSED_LIMIT: return "REFUSED_LIMIT";
    case OMEGA_PSVC_REFUSED_HOST: return "REFUSED_HOST";
    default: return "UNKNOWN";
    }
}

/* One service bit against the presence of its functions: all present when claimed, all absent when not. */
static int svc_matches(uint32_t claimed, uint32_t bit, int present, int all) {
    if (claimed & bit) return all ? OMEGA_PSVC_OK : OMEGA_PSVC_REFUSED_NULL_SERVICE;
    return present ? OMEGA_PSVC_REFUSED_HIDDEN_SERVICE : OMEGA_PSVC_OK;
}

int omega_psvc_check(const OmegaPlatformServices *s, uint32_t required) {
    if (!s || (required & ~(uint32_t)OMEGA_PSVC_ALL)) return OMEGA_PSVC_REFUSED_ARGS;
    if (s->abi_major != OMEGA_PSVC_ABI_MAJOR || s->abi_minor > OMEGA_PSVC_ABI_MINOR) return OMEGA_PSVC_REFUSED_ABI;
    if (s->services & ~(uint32_t)OMEGA_PSVC_ALL) return OMEGA_PSVC_REFUSED_ARGS;
#define ONE(bit, a) { bit, (a) != 0, (a) != 0 }
#define TWO(bit, a, b) { bit, (a) != 0 || (b) != 0, (a) != 0 && (b) != 0 }
    const struct { uint32_t bit; int present, all; } t[] = {
        ONE(OMEGA_PSVC_CLOCK, s->clock_now_ns),
        ONE(OMEGA_PSVC_WAIT, s->wait_us),
        ONE(OMEGA_PSVC_YIELD, s->yield),
        { OMEGA_PSVC_LOCK, s->lock_create || s->lock_acquire || s->lock_release || s->lock_destroy,
          s->lock_create && s->lock_acquire && s->lock_release && s->lock_destroy },
        TWO(OMEGA_PSVC_TASK, s->task_start, s->task_join),
        ONE(OMEGA_PSVC_NAME, s->task_name_self),
        ONE(OMEGA_PSVC_PIN, s->task_pin_self),
        { OMEGA_PSVC_HEAP, s->heap_alloc || s->heap_calloc || s->heap_free,
          s->heap_alloc && s->heap_calloc && s->heap_free },
        ONE(OMEGA_PSVC_CONFIG, s->config_get),
        ONE(OMEGA_PSVC_DIAG, s->diag_write),
    };
#undef ONE
#undef TWO
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        int rc = svc_matches(s->services, t[i].bit, t[i].present, t[i].all);
        if (rc != OMEGA_PSVC_OK) return rc;
    }
    if ((s->services & required) != required) return OMEGA_PSVC_REFUSED_MISSING_REQUIRED;
    return OMEGA_PSVC_OK;
}

/* ---- the refusing stub ------------------------------------------------------------------------------------ */

/* The stub claims no service, so omega_psvc_check would call its functions hidden. It is never checked: it is
 * the table a caller reaches when nothing is installed, and every entry refuses so no call goes through NULL. */
static uint64_t st_clock(void *c) { (void)c; return 0; }
static int st_wait(void *c, uint64_t us) { (void)c; (void)us; return OMEGA_PSVC_REFUSED_UNSUPPORTED; }
static int st_yield(void *c) { (void)c; return OMEGA_PSVC_REFUSED_UNSUPPORTED; }
static int st_lock_create(void *c, OmegaPsvcLock *o) {
    (void)c;
    if (o) *o = (OmegaPsvcLock){ 0, 0 };
    return OMEGA_PSVC_REFUSED_UNSUPPORTED;
}
static int st_lock_op(void *c, OmegaPsvcLock l) { (void)c; (void)l; return OMEGA_PSVC_REFUSED_UNSUPPORTED; }
static int st_task_start(void *c, OmegaPsvcTaskFn fn, void *arg, OmegaPsvcTask *o) {
    (void)c; (void)fn; (void)arg;
    if (o) *o = (OmegaPsvcTask){ 0, 0 };
    return OMEGA_PSVC_REFUSED_UNSUPPORTED;
}
static int st_task_join(void *c, OmegaPsvcTask t) { (void)c; (void)t; return OMEGA_PSVC_REFUSED_UNSUPPORTED; }
static int st_name(void *c, const char *n) { (void)c; (void)n; return OMEGA_PSVC_REFUSED_UNSUPPORTED; }
static int st_pin(void *c, uint32_t cpu) { (void)c; (void)cpu; return OMEGA_PSVC_REFUSED_UNSUPPORTED; }
static void *st_alloc(void *c, size_t b) { (void)c; (void)b; return (void *)0; }
static void *st_calloc(void *c, size_t n, size_t s) { (void)c; (void)n; (void)s; return (void *)0; }
static void st_free(void *c, void *p) { (void)c; (void)p; }
static int st_config(void *c, const char *k, const char **v) {
    (void)c; (void)k;
    if (v) *v = (const char *)0;
    return OMEGA_PSVC_REFUSED_UNSUPPORTED;
}
static int st_diag(void *c, const char *t) { (void)c; (void)t; return OMEGA_PSVC_REFUSED_UNSUPPORTED; }

static const OmegaPlatformServices g_stub = {
    .abi_major = OMEGA_PSVC_ABI_MAJOR, .abi_minor = OMEGA_PSVC_ABI_MINOR, .services = 0, .ctx = (void *)0,
    .clock_now_ns = st_clock, .wait_us = st_wait, .yield = st_yield,
    .lock_create = st_lock_create, .lock_acquire = st_lock_op, .lock_release = st_lock_op, .lock_destroy = st_lock_op,
    .task_start = st_task_start, .task_join = st_task_join, .task_name_self = st_name, .task_pin_self = st_pin,
    .heap_alloc = st_alloc, .heap_calloc = st_calloc, .heap_free = st_free,
    .config_get = st_config, .diag_write = st_diag,
};

const OmegaPlatformServices *omega_psvc_stub(void) { return &g_stub; }

/* ---- install and lookup ------------------------------------------------------------------------------------ */

static const OmegaPlatformServices *g_installed; /* atomic load / store */
#define LOAD() __atomic_load_n(&g_installed, __ATOMIC_ACQUIRE)

static const OmegaPlatformServices *build_default(void) {
#ifdef OMEGA_GPU_NO_POSIX
    return &g_stub;
#else
    return omega_psvc_hosted();
#endif
}

const OmegaPlatformServices *omega_psvc(void) {
    const OmegaPlatformServices *s = LOAD();
    return s ? s : build_default();
}

int omega_psvc_install(const OmegaPlatformServices *s) {
    if (s) {
        int rc = omega_psvc_check(s, OMEGA_PSVC_REQUIRED);
        if (rc != OMEGA_PSVC_OK) return rc;
    }
    __atomic_store_n(&g_installed, s, __ATOMIC_RELEASE);
    return OMEGA_PSVC_OK;
}

/* ---- convenience calls -------------------------------------------------------------------------------------- */

/* A table claims a service, or the call refuses without reaching the function (an installed table is checked;
 * the stub claims nothing and refuses either way). */
#define HAS(s, bit) (((s)->services & (bit)) != 0)

uint64_t omega_psvc_now_ns(void) {
    const OmegaPlatformServices *s = omega_psvc();
    return HAS(s, OMEGA_PSVC_CLOCK) ? s->clock_now_ns(s->ctx) : 0;
}
int omega_psvc_wait_us(uint64_t us) {
    const OmegaPlatformServices *s = omega_psvc();
    return HAS(s, OMEGA_PSVC_WAIT) ? s->wait_us(s->ctx, us) : OMEGA_PSVC_REFUSED_UNSUPPORTED;
}
int omega_psvc_yield(void) {
    const OmegaPlatformServices *s = omega_psvc();
    return HAS(s, OMEGA_PSVC_YIELD) ? s->yield(s->ctx) : OMEGA_PSVC_REFUSED_UNSUPPORTED;
}
int omega_psvc_task_start(OmegaPsvcTaskFn fn, void *arg, OmegaPsvcTask *out) {
    const OmegaPlatformServices *s = omega_psvc();
    if (out) *out = (OmegaPsvcTask){ 0, 0 };
    if (!fn || !out) return OMEGA_PSVC_REFUSED_ARGS;
    return HAS(s, OMEGA_PSVC_TASK) ? s->task_start(s->ctx, fn, arg, out) : OMEGA_PSVC_REFUSED_UNSUPPORTED;
}
int omega_psvc_task_join(OmegaPsvcTask t) {
    const OmegaPlatformServices *s = omega_psvc();
    return HAS(s, OMEGA_PSVC_TASK) ? s->task_join(s->ctx, t) : OMEGA_PSVC_REFUSED_UNSUPPORTED;
}
int omega_psvc_task_name_self(const char *name) {
    const OmegaPlatformServices *s = omega_psvc();
    if (!name) return OMEGA_PSVC_REFUSED_ARGS;
    return HAS(s, OMEGA_PSVC_NAME) ? s->task_name_self(s->ctx, name) : OMEGA_PSVC_REFUSED_UNSUPPORTED;
}
int omega_psvc_task_pin_self(uint32_t cpu) {
    const OmegaPlatformServices *s = omega_psvc();
    return HAS(s, OMEGA_PSVC_PIN) ? s->task_pin_self(s->ctx, cpu) : OMEGA_PSVC_REFUSED_UNSUPPORTED;
}
void *omega_psvc_alloc(size_t bytes) {
    const OmegaPlatformServices *s = omega_psvc();
    return HAS(s, OMEGA_PSVC_HEAP) ? s->heap_alloc(s->ctx, bytes) : (void *)0;
}
void *omega_psvc_calloc(size_t n, size_t size) {
    const OmegaPlatformServices *s = omega_psvc();
    return HAS(s, OMEGA_PSVC_HEAP) ? s->heap_calloc(s->ctx, n, size) : (void *)0;
}
void omega_psvc_free(void *p) {
    const OmegaPlatformServices *s = omega_psvc();
    if (HAS(s, OMEGA_PSVC_HEAP)) s->heap_free(s->ctx, p);
}
int omega_psvc_config(const char *key, const char **value) {
    const OmegaPlatformServices *s = omega_psvc();
    if (value) *value = (const char *)0;
    if (!key || !value) return OMEGA_PSVC_REFUSED_ARGS;
    return HAS(s, OMEGA_PSVC_CONFIG) ? s->config_get(s->ctx, key, value) : OMEGA_PSVC_REFUSED_UNSUPPORTED;
}

int omega_psvc_diag(const char *text) {
    const OmegaPlatformServices *s = omega_psvc();
    if (!text) return OMEGA_PSVC_REFUSED_ARGS;
    if (!HAS(s, OMEGA_PSVC_DIAG)) return OMEGA_PSVC_REFUSED_UNSUPPORTED;
    return s->diag_write(s->ctx, text);
}
int omega_psvc_diagf(const char *fmt, ...) {
    const OmegaPlatformServices *s = omega_psvc();
    if (!fmt) return OMEGA_PSVC_REFUSED_ARGS;
    if (!HAS(s, OMEGA_PSVC_DIAG)) return OMEGA_PSVC_REFUSED_UNSUPPORTED;
    char line[OMEGA_PSVC_DIAG_LINE];
    va_list ap;
    va_start(ap, fmt);
    omega_gpu_vfmt(line, sizeof line, fmt, ap);
    va_end(ap);
    return s->diag_write(s->ctx, line);
}
