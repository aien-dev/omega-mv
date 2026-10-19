#ifndef OMEGA_LIFETIME_H
#define OMEGA_LIFETIME_H
/*
 * omega_lifetime: lifetime-based buffer sharing for one recorded command list (FORTRAN idea 1, EQUIVALENCE; omega#569).
 *
 * The 1956 FORTRAN manual: EQUIVALENCE "permits him to economise on data storage requirements by causing storage
 * locations to be shared by two or more quantities, when the logic of his program permits", and sharing "cannot be
 * planned safely without a knowledge of" every statement that writes a location. This module is that knowledge for an
 * Omega command list:
 *
 *   trace   every op (one step, one or more launches, run in order behind the list's full barrier) declares the buffers
 *           it reads and writes: omega_lt_op() then omega_lt_use();
 *   live    a buffer is live from its first to its last access (both ends inclusive, so an op that reads A while it writes
 *           B keeps them apart); LIVE_IN starts it at step 0 (the host wrote it before the list), LIVE_OUT ends it after the
 *           last step (the host reads it after the list); PINNED is live for the whole list (contents kept across lists);
 *   plan    largest buffer first, each at the lowest aligned offset that overlaps no placed buffer live at the same time;
 *           a TAIL buffer is glued directly after the buffer before it (a kernel that reads both as one row);
 *   check   two independent rules, either one red is a refusal: (1) no two buffers live at the same time share a byte;
 *           (2) replay: a write to a buffer clobbers every buffer whose bytes it shares; reading a clobbered buffer, reading a
 *           buffer never written (and not LIVE_IN), or ending with a LIVE_OUT buffer clobbered is red;
 *   map     per slot (a run of buffers that share bytes), the buffers, their live steps and every writer (the danger list).
 *
 * Steps are ordered: the plan is only valid while the list keeps a barrier between ops (omega_gpu_cmdlist_mark_independent
 * lets two launches overlap; an op that does that must declare both launches as one step).
 * Freestanding: no allocation (the caller gives the access array), no device calls; host tests run it directly.
 */
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define OMEGA_LT_MAX_BUFS 48u
#define OMEGA_LT_NONE 0xffffffffu

enum {
    OMEGA_LT_LIVE_IN = 1u,  /* the host writes it before the list */
    OMEGA_LT_LIVE_OUT = 2u, /* the host reads it after the list */
    OMEGA_LT_PINNED = 4u,   /* live for the whole list and across lists (never shares) */
    OMEGA_LT_TAIL = 8u,     /* placed directly after the buffer declared before it (bytes of that one rounded to align) */
};

typedef struct {
    uint32_t step;    /* op index */
    uint16_t buf;     /* buffer id */
    uint8_t write;    /* 1 write, 0 read */
    uint8_t pad;
} OmegaLtAccess;

typedef struct {
    const char *name;
    uint64_t bytes;
    uint32_t align;   /* power of two, >= 1 */
    uint32_t flags;
    uint32_t first, last;   /* live steps (omega_lt_live); OMEGA_LT_NONE when never touched and not flagged */
} OmegaLtBuf;

typedef struct {
    uint32_t n_buf;
    OmegaLtBuf buf[OMEGA_LT_MAX_BUFS];
    uint32_t n_op;                 /* ops recorded; the current op is n_op - 1 */
    uint32_t op_cap;
    const char **op_name;          /* [op_cap], caller storage (may be NULL: names not kept) */
    uint32_t n_acc, acc_cap;
    OmegaLtAccess *acc;            /* [acc_cap], caller storage */
    int overflow;                  /* an access or op did not fit: every later call refuses */
} OmegaLt;

void omega_lt_init(OmegaLt *t, OmegaLtAccess *acc, uint32_t acc_cap, const char **op_name, uint32_t op_cap);
/* Returns the buffer id, or -1 (full, bytes 0, align not a power of two, TAIL as the first buffer). */
int omega_lt_buf(OmegaLt *t, const char *name, uint64_t bytes, uint32_t align, uint32_t flags);
/* Starts the next op (step). name: the writer named in the danger list (kept by pointer). 0 ok, -1 full. */
int omega_lt_op(OmegaLt *t, const char *name);
/* The current op reads (write 0) or writes (write 1) buffer id. 0 ok, -1 (no op, bad id, full). */
int omega_lt_use(OmegaLt *t, int id, int write);
/* Fills buf[].first / last. 0 ok, -1 (overflowed trace). */
int omega_lt_live(OmegaLt *t);
/* Bytes live at the busiest step (a lower bound for any plan) and the sum of every buffer (the plan with no sharing). */
uint64_t omega_lt_peak_live(const OmegaLt *t);
uint64_t omega_lt_sum_bytes(const OmegaLt *t);
/* Offsets (bytes, from 0) for every buffer; *arena = bytes the plan needs. Call after omega_lt_live. 0 ok, -1. */
int omega_lt_plan(const OmegaLt *t, uint64_t *off, uint64_t *arena);
/* The unshared plan (declaration order, each aligned): the layout before this module. */
int omega_lt_plan_flat(const OmegaLt *t, uint64_t *off, uint64_t *arena);
/* Both safety rules over a plan. 0 = safe; -1 = red, why (if non-NULL) names the first violation. */
int omega_lt_check(const OmegaLt *t, const uint64_t *off, char *why, size_t why_cap);
/* 1 if the buffer still holds its last written value after the last step under this plan (a host read is valid). */
int omega_lt_valid_at_end(const OmegaLt *t, const uint64_t *off, int id);
/* The storage map with the danger list as text. Returns the length written (truncated to cap - 1), -1 on bad args. */
int omega_lt_map(const OmegaLt *t, const uint64_t *off, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
#endif
