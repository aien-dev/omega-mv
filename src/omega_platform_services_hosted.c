/* omega_platform_services_hosted.c -- the hosted (Linux, glibc) platform services (L202, cut A).
 *
 * Each entry makes exactly the libc call its caller made before the table existed, with the same arguments:
 * clock_gettime(CLOCK_MONOTONIC), usleep, sched_yield, pthread_mutex_*, pthread_create(NULL attributes) and
 * pthread_join(NULL result), pthread_setname_np(pthread_self()), pthread_setaffinity_np over a one-processor
 * set built with CPU_ZERO / CPU_SET((int)cpu), malloc / calloc / free, getenv, and fputs to stderr.
 * Hosted only: never in the freestanding closure.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* pthread_setname_np, pthread_setaffinity_np, CPU_SET */
#endif
#include "omega_platform_services.h"

#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define HOSTED_LOCKS 64u
#define HOSTED_TASKS 64u

typedef struct {
    pthread_mutex_t mu; /* the slot tables */
    pthread_mutex_t lock[HOSTED_LOCKS];
    uint32_t lock_gen[HOSTED_LOCKS]; /* next generation to issue; starts at 1 */
    uint8_t lock_live[HOSTED_LOCKS];
    pthread_t task[HOSTED_TASKS];
    uint32_t task_gen[HOSTED_TASKS];
    uint8_t task_live[HOSTED_TASKS];
} Hosted;

static Hosted g_h = { .mu = PTHREAD_MUTEX_INITIALIZER };

static uint32_t next_gen(uint32_t g) { return g + 1u ? g + 1u : 1u; } /* never issue 0 */

static uint64_t h_clock(void *c) {
    (void)c;
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

static int h_wait(void *c, uint64_t us) {
    (void)c;
    return usleep((useconds_t)us) == 0 ? OMEGA_PSVC_OK : OMEGA_PSVC_REFUSED_HOST;
}

static int h_yield(void *c) { (void)c; return sched_yield() == 0 ? OMEGA_PSVC_OK : OMEGA_PSVC_REFUSED_HOST; }

static int h_lock_create(void *c, OmegaPsvcLock *out) {
    (void)c;
    if (!out) return OMEGA_PSVC_REFUSED_ARGS;
    *out = (OmegaPsvcLock){ 0, 0 };
    pthread_mutex_lock(&g_h.mu);
    for (uint32_t i = 0; i < HOSTED_LOCKS; i++) {
        if (g_h.lock_live[i]) continue;
        if (pthread_mutex_init(&g_h.lock[i], NULL) != 0) break;
        if (g_h.lock_gen[i] == 0) g_h.lock_gen[i] = 1;
        g_h.lock_live[i] = 1;
        *out = (OmegaPsvcLock){ i, g_h.lock_gen[i] };
        pthread_mutex_unlock(&g_h.mu);
        return OMEGA_PSVC_OK;
    }
    pthread_mutex_unlock(&g_h.mu);
    return OMEGA_PSVC_REFUSED_LIMIT;
}

/* A lock handle is checked without the table mutex: index and generation are written before the handle is
 * returned and change only at destroy, which the caller must not race with its own acquire. */
static pthread_mutex_t *lock_of(OmegaPsvcLock l) {
    if (l.index >= HOSTED_LOCKS || l.generation == 0) return NULL;
    if (!__atomic_load_n(&g_h.lock_live[l.index], __ATOMIC_ACQUIRE)) return NULL;
    if (__atomic_load_n(&g_h.lock_gen[l.index], __ATOMIC_ACQUIRE) != l.generation) return NULL;
    return &g_h.lock[l.index];
}

static int h_lock_acquire(void *c, OmegaPsvcLock l) {
    (void)c;
    pthread_mutex_t *m = lock_of(l);
    if (!m) return OMEGA_PSVC_REFUSED_STALE;
    return pthread_mutex_lock(m) == 0 ? OMEGA_PSVC_OK : OMEGA_PSVC_REFUSED_HOST;
}

static int h_lock_release(void *c, OmegaPsvcLock l) {
    (void)c;
    pthread_mutex_t *m = lock_of(l);
    if (!m) return OMEGA_PSVC_REFUSED_STALE;
    return pthread_mutex_unlock(m) == 0 ? OMEGA_PSVC_OK : OMEGA_PSVC_REFUSED_HOST;
}

static int h_lock_destroy(void *c, OmegaPsvcLock l) {
    (void)c;
    pthread_mutex_lock(&g_h.mu);
    pthread_mutex_t *m = lock_of(l);
    int rc = OMEGA_PSVC_REFUSED_STALE;
    if (m) {
        rc = pthread_mutex_destroy(m) == 0 ? OMEGA_PSVC_OK : OMEGA_PSVC_REFUSED_HOST;
        if (rc == OMEGA_PSVC_OK) {
            __atomic_store_n(&g_h.lock_live[l.index], 0, __ATOMIC_RELEASE);
            __atomic_store_n(&g_h.lock_gen[l.index], next_gen(l.generation), __ATOMIC_RELEASE);
        }
    }
    pthread_mutex_unlock(&g_h.mu);
    return rc;
}

static int h_task_start(void *c, OmegaPsvcTaskFn fn, void *arg, OmegaPsvcTask *out) {
    (void)c;
    if (!fn || !out) return OMEGA_PSVC_REFUSED_ARGS;
    *out = (OmegaPsvcTask){ 0, 0 };
    pthread_mutex_lock(&g_h.mu);
    for (uint32_t i = 0; i < HOSTED_TASKS; i++) {
        if (g_h.task_live[i]) continue;
        if (pthread_create(&g_h.task[i], NULL, fn, arg) != 0) {
            pthread_mutex_unlock(&g_h.mu);
            return OMEGA_PSVC_REFUSED_HOST;
        }
        if (g_h.task_gen[i] == 0) g_h.task_gen[i] = 1;
        g_h.task_live[i] = 1;
        *out = (OmegaPsvcTask){ i, g_h.task_gen[i] };
        pthread_mutex_unlock(&g_h.mu);
        return OMEGA_PSVC_OK;
    }
    pthread_mutex_unlock(&g_h.mu);
    return OMEGA_PSVC_REFUSED_LIMIT;
}

static int h_task_join(void *c, OmegaPsvcTask t) {
    (void)c;
    pthread_mutex_lock(&g_h.mu);
    int ok = t.index < HOSTED_TASKS && t.generation != 0 && g_h.task_live[t.index] &&
             g_h.task_gen[t.index] == t.generation;
    pthread_t th = ok ? g_h.task[t.index] : (pthread_t)0;
    pthread_mutex_unlock(&g_h.mu);
    if (!ok) return OMEGA_PSVC_REFUSED_STALE;
    int rc = pthread_join(th, NULL); /* outside the table mutex: the task may start or join others */
    pthread_mutex_lock(&g_h.mu);
    g_h.task_live[t.index] = 0;
    g_h.task_gen[t.index] = next_gen(t.generation);
    pthread_mutex_unlock(&g_h.mu);
    return rc == 0 ? OMEGA_PSVC_OK : OMEGA_PSVC_REFUSED_HOST;
}

static int h_name(void *c, const char *name) {
    (void)c;
    if (!name) return OMEGA_PSVC_REFUSED_ARGS;
    return pthread_setname_np(pthread_self(), name) == 0 ? OMEGA_PSVC_OK : OMEGA_PSVC_REFUSED_HOST;
}

static int h_pin(void *c, uint32_t cpu) {
    (void)c;
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET((int)cpu, &set);
    return pthread_setaffinity_np(pthread_self(), sizeof set, &set) == 0 ? OMEGA_PSVC_OK : OMEGA_PSVC_REFUSED_HOST;
}

static void *h_alloc(void *c, size_t b) { (void)c; return malloc(b); }
static void *h_calloc(void *c, size_t n, size_t s) { (void)c; return calloc(n, s); }
static void h_free(void *c, void *p) { (void)c; free(p); }

static int h_config(void *c, const char *key, const char **value) {
    (void)c;
    if (!key || !value) return OMEGA_PSVC_REFUSED_ARGS;
    *value = getenv(key);
    return *value ? OMEGA_PSVC_OK : OMEGA_PSVC_NOT_SET;
}

static int h_diag(void *c, const char *text) {
    (void)c;
    if (!text) return OMEGA_PSVC_REFUSED_ARGS;
    return fputs(text, stderr) >= 0 ? OMEGA_PSVC_OK : OMEGA_PSVC_REFUSED_HOST;
}

static const OmegaPlatformServices g_hosted = {
    .abi_major = OMEGA_PSVC_ABI_MAJOR, .abi_minor = OMEGA_PSVC_ABI_MINOR, .services = OMEGA_PSVC_ALL, .ctx = NULL,
    .clock_now_ns = h_clock, .wait_us = h_wait, .yield = h_yield,
    .lock_create = h_lock_create, .lock_acquire = h_lock_acquire, .lock_release = h_lock_release,
    .lock_destroy = h_lock_destroy,
    .task_start = h_task_start, .task_join = h_task_join, .task_name_self = h_name, .task_pin_self = h_pin,
    .heap_alloc = h_alloc, .heap_calloc = h_calloc, .heap_free = h_free,
    .config_get = h_config, .diag_write = h_diag,
};

const OmegaPlatformServices *omega_psvc_hosted(void) { return &g_hosted; }
