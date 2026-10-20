/* omega_platform_services.h -- process-wide host services for Omega (L202, aienos#286 cut A).
 *
 * The resident seat (src/runtime/rx_resident_gpu.c) and the numeric launchers called pthreads, usleep, malloc,
 * getenv and stderr directly. They now ask this table instead. It names the host services those files need and
 * nothing about a device:
 *
 *   CLOCK   monotonic nanosecond clock                    (clock_gettime(CLOCK_MONOTONIC))
 *   WAIT    sleep at least a number of microseconds        (usleep)
 *   YIELD   give up the processor once                     (sched_yield)
 *   LOCK    mutual exclusion through typed lock handles    (pthread_mutex_*)
 *   TASK    start and join a thread of control             (pthread_create, pthread_join)
 *   NAME    name the calling task                          (pthread_setname_np)
 *   PIN     pin the calling task to one processor          (pthread_setaffinity_np)
 *   HEAP    CPU-only memory, never device-visible          (malloc, calloc, free)
 *   CONFIG  read one configuration value by key            (getenv)
 *   DIAG    write one line of diagnostic text              (fputs to stderr)
 *
 * Two implementations exist. The hosted one (src/omega_platform_services_hosted.c, never in the freestanding
 * build) makes exactly the libc call in brackets above, with the same arguments, so a hosted program behaves as
 * it did before the table existed. The freestanding stub (src/omega_platform_services.c) claims no service and
 * refuses every call with OMEGA_PSVC_REFUSED_UNSUPPORTED. A freestanding program has no default: the native
 * system installs its own table with omega_psvc_install before anything asks.
 *
 * Why this is a separate table from OmegaGpuPlatform (src/omega_gpu_platform.h). OmegaGpuPlatform is the
 * per-device platform the persistent GPU session negotiates: device, DMA, queues, completion, plus the host
 * clock, wait, lock and memory the session needs while it holds the device. This table is for code that runs
 * without a session (the seat, the one-shot launchers) and for services the session never needs (tasks, task
 * names and pins, configuration). The hosted OmegaGpuPlatform (src/omega_gpu_platform_linux.c) keeps its own
 * libc calls on purpose: its object code is on the token path and is left byte-identical by this cut.
 *
 * Refusal policy, which a caller must follow:
 *   - CLOCK, WAIT, LOCK, TASK and HEAP are REQUIRED. A refusal there is fatal to the operation that asked: the
 *     caller fails that operation, it never carries on without the service.
 *   - YIELD, NAME, PIN, CONFIG and DIAG are OPTIONAL. A refusal there means "go on without it": no yield, an
 *     unnamed or unpinned task, the key treated as not set, the text dropped. It never fails the operation.
 *   - CONFIG distinguishes a key that is not set (OMEGA_PSVC_NOT_SET) from a key set to the empty string
 *     (OMEGA_PSVC_OK with *value == ""), exactly as getenv returns NULL versus "".
 * Nothing in this table falls back to another service: a missing function is a refusal, never an emulation.
 *
 * Pure C, no libc header, builds in the freestanding link target.
 */
#ifndef OMEGA_PLATFORM_SERVICES_H
#define OMEGA_PLATFORM_SERVICES_H

#include <stddef.h>
#include <stdint.h>

#define OMEGA_PSVC_ABI_MAJOR 1u
#define OMEGA_PSVC_ABI_MINOR 0u

/* Typed handles: index + generation, generation 0 is never issued, a released handle is stale. */
typedef struct { uint32_t index; uint32_t generation; } OmegaPsvcLock;
typedef struct { uint32_t index; uint32_t generation; } OmegaPsvcTask;

enum {
    OMEGA_PSVC_CLOCK = 1u << 0,
    OMEGA_PSVC_WAIT = 1u << 1,
    OMEGA_PSVC_YIELD = 1u << 2,
    OMEGA_PSVC_LOCK = 1u << 3,
    OMEGA_PSVC_TASK = 1u << 4,
    OMEGA_PSVC_NAME = 1u << 5,
    OMEGA_PSVC_PIN = 1u << 6,
    OMEGA_PSVC_HEAP = 1u << 7,
    OMEGA_PSVC_CONFIG = 1u << 8,
    OMEGA_PSVC_DIAG = 1u << 9,
    OMEGA_PSVC_REQUIRED = OMEGA_PSVC_CLOCK | OMEGA_PSVC_WAIT | OMEGA_PSVC_LOCK | OMEGA_PSVC_TASK | OMEGA_PSVC_HEAP,
    OMEGA_PSVC_ALL = (1u << 10) - 1u
};

typedef enum {
    OMEGA_PSVC_OK = 0,
    OMEGA_PSVC_NOT_SET,                 /* CONFIG: the key has no value (not a refusal) */
    OMEGA_PSVC_REFUSED_ARGS,            /* NULL or malformed argument */
    OMEGA_PSVC_REFUSED_ABI,             /* major differs, or minor newer than this build knows */
    OMEGA_PSVC_REFUSED_UNSUPPORTED,     /* the service is not provided (the freestanding stub says this) */
    OMEGA_PSVC_REFUSED_NULL_SERVICE,    /* a claimed service has no function */
    OMEGA_PSVC_REFUSED_HIDDEN_SERVICE,  /* a function is present for a service that is not claimed */
    OMEGA_PSVC_REFUSED_MISSING_REQUIRED,/* a required service is not claimed */
    OMEGA_PSVC_REFUSED_STALE,           /* lock or task handle closed or never issued */
    OMEGA_PSVC_REFUSED_LIMIT,           /* no free lock or task slot */
    OMEGA_PSVC_REFUSED_HOST,            /* the host call itself failed (the hosted table: a nonzero pthread result) */
    OMEGA_PSVC_RESULT_COUNT
} OmegaPsvcResult;

const char *omega_psvc_result_name(int r); /* static strings, never NULL */

typedef void *(*OmegaPsvcTaskFn)(void *arg);

typedef struct {
    uint32_t abi_major;
    uint32_t abi_minor;
    uint32_t services; /* OMEGA_PSVC_* claimed */
    void *ctx;
    /* CLOCK: monotonic nanoseconds. */
    uint64_t (*clock_now_ns)(void *ctx);
    /* WAIT: sleep at least us microseconds. 0 slept, else a refusal. */
    int (*wait_us)(void *ctx, uint64_t us);
    /* YIELD */
    int (*yield)(void *ctx);
    /* LOCK */
    int (*lock_create)(void *ctx, OmegaPsvcLock *out);
    int (*lock_acquire)(void *ctx, OmegaPsvcLock l);
    int (*lock_release)(void *ctx, OmegaPsvcLock l);
    int (*lock_destroy)(void *ctx, OmegaPsvcLock l);
    /* TASK: fn(arg) runs on its own task; join waits for it to return and releases the handle. */
    int (*task_start)(void *ctx, OmegaPsvcTaskFn fn, void *arg, OmegaPsvcTask *out);
    int (*task_join)(void *ctx, OmegaPsvcTask t);
    /* NAME, PIN: act on the calling task. */
    int (*task_name_self)(void *ctx, const char *name);
    int (*task_pin_self)(void *ctx, uint32_t cpu);
    /* HEAP: NULL on refusal; heap_free(NULL) is a no-op. heap_alloc(0) behaves as the host malloc(0). */
    void *(*heap_alloc)(void *ctx, size_t bytes);
    void *(*heap_calloc)(void *ctx, size_t n, size_t size);
    void (*heap_free)(void *ctx, void *p);
    /* CONFIG: OMEGA_PSVC_OK and *value (never NULL, may be ""), or OMEGA_PSVC_NOT_SET and *value NULL. The string
     * belongs to the platform and stays valid until the value changes. */
    int (*config_get)(void *ctx, const char *key, const char **value);
    /* DIAG: write text as given (the caller supplies any newline). */
    int (*diag_write)(void *ctx, const char *text);
} OmegaPlatformServices;

/* Contract check: ABI, every claimed service has its functions, no function outside the claimed set, and the
 * services in `required` are claimed. Pure. */
int omega_psvc_check(const OmegaPlatformServices *s, uint32_t required);

/* The installed table. The hosted build installs omega_psvc_hosted() on first use; the freestanding build
 * returns omega_psvc_stub() until a table is installed. Never NULL. */
const OmegaPlatformServices *omega_psvc(void);
/* Install a table (checked against OMEGA_PSVC_REQUIRED). Install it before any task asks; the table must stay
 * valid for the life of the process. NULL restores the build's default. */
int omega_psvc_install(const OmegaPlatformServices *s);
/* The refusing table: services 0, every function present and refusing (so a caller never calls through NULL). */
const OmegaPlatformServices *omega_psvc_stub(void);
#ifndef OMEGA_GPU_NO_POSIX
const OmegaPlatformServices *omega_psvc_hosted(void); /* hosted builds only */
#endif

/* Convenience calls on the installed table. Required services return its result; optional ones are safe to
 * call when refused. */
uint64_t omega_psvc_now_ns(void);                  /* 0 when CLOCK is refused */
int omega_psvc_wait_us(uint64_t us);
int omega_psvc_yield(void);
int omega_psvc_task_start(OmegaPsvcTaskFn fn, void *arg, OmegaPsvcTask *out);
int omega_psvc_task_join(OmegaPsvcTask t);
int omega_psvc_task_name_self(const char *name);
int omega_psvc_task_pin_self(uint32_t cpu);
void *omega_psvc_alloc(size_t bytes);
void *omega_psvc_calloc(size_t n, size_t size);
void omega_psvc_free(void *p);
int omega_psvc_config(const char *key, const char **value);
/* Format with omega_gpu_fmt into a bounded line (OMEGA_PSVC_DIAG_LINE bytes, truncated, never overflowed) and
 * write it through DIAG. Conversions as omega_gpu_fmt. */
#define OMEGA_PSVC_DIAG_LINE 512u
int omega_psvc_diagf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif /* OMEGA_PLATFORM_SERVICES_H */
