/*
 * omega_gpu_roll: the GPU engine plays more than one note at a time (omega-mv#580, "teach it more notes").
 *
 * omega_gpu_execute (src/omega_gpu_engine.c + src/omega_blackwell_engine.c) runs ONE job: one layout (VECTOR_1D),
 * one channel, submit, wait, free. This module keeps the same per-job checks and adds four things:
 *
 *   roll     up to `depth` groups in flight per channel: group i+1 is written and its doorbell rung while group i
 *            still runs; a group's slot is only reused after the group retired (completion seen, words checked).
 *   melody   any codegen program: a node is a kernel (code uploaded once per channel), its launch shape, up to
 *            OMEGA_ROLL_MAX_ARGS argument words and up to OMEGA_ROLL_MAX_BUFS buffers whose GPU addresses are
 *            patched into named argument words. Not only VECTOR_1D.
 *   chord    up to OMEGA_ROLL_MAX_NODES nodes in ONE submission with no barrier between them (ORDER_NONE): the
 *            grids may run at the same time on different SMs. Or several channels (`voices`).
 *   harmony  a dependency graph in one submission: node j names the earlier nodes it needs (deps bitmask), and the
 *            ring orders them: ORDER_SEM puts a host semaphore ACQUIRE (NVC96F_SEM_EXECUTE, clc96f.h:75-88) on each
 *            dependency's QMD release semaphore before the node's launch; ORDER_WAVE puts WAIT_FOR_IDLE only where a
 *            dependency is still in the current wave; ORDER_SERIAL puts WAIT_FOR_IDLE before every node.
 *
 * Per-node checks, the same as the engine's vecadd path: every output buffer is filled with its poison word before
 * submit and scanned after readback (unwritten words counted), each group ends with the first marker (release with
 * WFI), L2_FLUSH_DIRTY and the second marker on an uncached page, and every node's QMD release semaphore must read
 * DONE. A node that never ran is caught by name (first_missing). The module never judges the math; the caller does.
 *
 * Launch words: the PCAS sequence of src/omega_gpu_cmdlist.c (chip-proven, W-L2): SEND_PCAS_A + SEND_SIGNALING_PCAS2_B
 * with a standalone QMD in memory, per-node constant bank and semaphore. Every buffer is GPU-uncached
 * (nvrm_alloc_gpu_uncached; nvos.h:1117: GPU-cacheable system memory is not coherent with CPU mappings on GB10).
 *
 * Uncertain completion: a marker timeout latches the roll (omega_gpu_roll_is_latched): nothing is freed, nothing is
 * closed, every later submit is refused, as the engine does.
 *
 * Threading: one caller thread. Linux reference (m16_native + nvrm), hosted build only.
 */
#ifndef OMEGA_GPU_ROLL_H
#define OMEGA_GPU_ROLL_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define OMEGA_ROLL_MAX_VOICES 8u
#define OMEGA_ROLL_MAX_DEPTH 256u
#define OMEGA_ROLL_MAX_NODES 64u
#define OMEGA_ROLL_MAX_BUFS 4u
#define OMEGA_ROLL_MAX_ARGS 32u
#define OMEGA_ROLL_NO_ARG 0xffu

/* return codes */
#define OMEGA_ROLL_OK 0
#define OMEGA_ROLL_ERR -1      /* refused or failed; omega_gpu_roll_error names it */
#define OMEGA_ROLL_FULL -2     /* every slot of the voice is in flight: retire first */
#define OMEGA_ROLL_EMPTY -3    /* retire: nothing in flight on that voice */
#define OMEGA_ROLL_LATCHED -4  /* an earlier completion was uncertain; nothing more runs */
#define OMEGA_ROLL_CHECK -5    /* retired, but a node's semaphore was not DONE or an output word kept its poison */

typedef enum {
    OMEGA_ROLL_ORDER_SERIAL = 0, /* WAIT_FOR_IDLE + invalidate before every node (the engine's one-note order) */
    OMEGA_ROLL_ORDER_NONE = 1,   /* chord: no barrier between nodes (they must be independent) */
    OMEGA_ROLL_ORDER_SEM = 2,    /* harmony: semaphore ACQUIRE on each dependency before the node */
    OMEGA_ROLL_ORDER_WAVE = 3,   /* harmony: WAIT_FOR_IDLE only before a node whose dependency is in the current wave */
    OMEGA_ROLL_ORDER_COUNT
} OmegaRollOrder;

typedef struct {
    uint32_t voices;        /* channels, 1..OMEGA_ROLL_MAX_VOICES */
    uint32_t depth;         /* group slots per voice (groups in flight), 1..OMEGA_ROLL_MAX_DEPTH */
    uint32_t max_nodes;     /* nodes per group, 1..OMEGA_ROLL_MAX_NODES */
    size_t data_bytes;      /* per slot: bytes for every buffer of one group (each buffer rounded up to 256) */
    uint64_t timeout_ms;    /* per marker wait; 0 = 20000 */
} OmegaRollConfig;

/* A program resident on every voice. */
typedef struct {
    uint64_t va[OMEGA_ROLL_MAX_VOICES];
    uint32_t gpr;           /* registers the QMD declares (>= 64, multiple of 8) */
    size_t code_bytes;
    int index;              /* internal */
} OmegaRollKernel;

typedef struct {
    size_t bytes;           /* > 0, multiple of 4 */
    const void *in;         /* copied into the device buffer before submit, or NULL */
    void *out;              /* copied out at retire, or NULL */
    int poisoned;           /* 1: output; every word set to `poison` before submit and scanned after */
    uint32_t poison;
    uint8_t arg_lo;         /* argument word that receives the buffer's GPU address (low; high at arg_lo + 1), or NO_ARG */
    int8_t from_node;       /* -1: this node's own buffer; >= 0: the buffer `from_buf` of an EARLIER node of the same
                             * group (harmony data edge; the device address is shared, nothing is copied) */
    uint8_t from_buf;
} OmegaRollBuf;

typedef struct {
    const OmegaRollKernel *k;
    uint32_t threads_x, threads_y, grid_x, grid_y, shared_bytes, num_elements;
    uint32_t n_args;
    uint32_t args[OMEGA_ROLL_MAX_ARGS];
    uint32_t nbufs;
    OmegaRollBuf buf[OMEGA_ROLL_MAX_BUFS];
    uint64_t deps;          /* bit i: this node needs node i (i < this node's index) to finish first */
} OmegaRollNode;

typedef struct {
    uint32_t n;
    const OmegaRollNode *nodes;  /* copied at submit; out pointers must stay valid until retire */
    int order;                   /* OmegaRollOrder */
    int skip_node;               /* TEST ONLY: >= 0 drops that node's launch (its semaphore and outputs must be caught) */
    int test_ignore_deps;        /* TEST ONLY: 1 emits no ordering for the deps (negative control of ORDER_SEM / WAVE) */
    uint64_t tag;                /* caller's id, returned by retire */
} OmegaRollGroup;

typedef struct {
    uint64_t tag;
    uint32_t voice;
    uint32_t nodes;
    uint32_t sems_done;
    int32_t first_missing;       /* first node whose semaphore is not DONE, -1 if none */
    uint64_t unwritten_words;    /* output words still equal to their poison, over all nodes */
    int32_t first_unwritten;     /* first node with an unwritten word, -1 if none */
    uint32_t marker, marker2, words;
    uint64_t submit_ns, done_ns; /* CLOCK_MONOTONIC: doorbell, second marker seen */
    int rc;                      /* OK or CHECK */
} OmegaRollResult;

typedef struct OmegaRoll OmegaRoll;

int omega_gpu_roll_open(const OmegaRollConfig *cfg, OmegaRoll **out);
/* Uploads code to every voice (zero padded per omega_gpu_code_alloc_bytes). gpr = registers the kernel uses. */
int omega_gpu_roll_kernel(OmegaRoll *r, const uint8_t *code, size_t bytes, uint32_t gpr, OmegaRollKernel *out);
/* Writes the group into the next free slot of `voice` and rings the doorbell. OK, FULL, ERR or LATCHED. */
int omega_gpu_roll_submit(OmegaRoll *r, uint32_t voice, const OmegaRollGroup *g);
/* Waits for the oldest group in flight on `voice`, checks it, copies outputs out. OK, CHECK, EMPTY, ERR or LATCHED. */
int omega_gpu_roll_retire(OmegaRoll *r, uint32_t voice, OmegaRollResult *res);
uint32_t omega_gpu_roll_in_flight(const OmegaRoll *r, uint32_t voice);
/* Retires everything, frees, closes. Refuses (ERR) and keeps everything when latched. */
int omega_gpu_roll_close(OmegaRoll *r);
int omega_gpu_roll_is_latched(void);
const char *omega_gpu_roll_error(const OmegaRoll *r);

/* Host-only pieces (no device), for tests. */
/* 0 when the group is well formed for cfg: node count, args, buffers (sizes fit data_bytes), deps only on earlier
 * nodes, aliases only to earlier nodes' buffers, ORDER_NONE without deps. Else -1 and *why. */
int omega_gpu_roll_validate(const OmegaRollConfig *cfg, const OmegaRollGroup *g, const char **why);
/* The method words a group gets between the setup prefix and the completion tail, with symbolic addresses
 * (node i: qmd = 0x100000 + i * 0x400, sem = 0x200000 + i * 16). Returns the word count or -1 (cap). */
int omega_gpu_roll_order_words(const OmegaRollGroup *g, uint32_t *pb, uint32_t cap);

#ifdef __cplusplus
}
#endif
#endif
