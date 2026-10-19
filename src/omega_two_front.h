#ifndef OMEGA_TWO_FRONT_H
#define OMEGA_TWO_FRONT_H
/*
 * omega_two_front: one arena, two fronts (omega#571, FORTRAN 1956 idea 3). The 1956 manual: "The instructions and constants
 * of the object program begin in lower memory and extend upwards. Data ... begin at location 77777, and extend downwards."
 * Nobody guesses where the boundary falls; the two fronts meet.
 *
 * Here the arena holds the Qwen3.8-27B per-conversation state:
 *   LOW front (offset 0 upwards): KV extents. One extent = one conversation's KV of the 16 full-attention layers in the
 *        block layout of omega_gpu_decode_attn256_api.h (cache_bind_kv): a block of 256 positions every block_bytes
 *        (16 MiB for 16 layers). Only the TOP extent grows (in place, a block at a time); the ones below it are sealed
 *        until they are closed or moved to the top. New blocks are handed out zeroed by the caller (finite rows: the
 *        prefill attention reads keys past its last one and masks them).
 *   HIGH front (arena end downwards): records of fixed size: a conversation's live DeltaNet set (48 layers of conv tail
 *        + recurrent state, omega_gpu_gdn_state_set_bytes), per-turn snapshots of it, or engine scratch.
 *   GAP: between the fronts, the only slack. Every move that would cross is refused with OMEGA_TF_OVERFLOW and changes
 *        nothing (the error names both fronts and the shortfall).
 * Holes: closing or releasing an item that is not at its front's edge leaves a hole that is reclaimed when the front
 * retreats through it (LIFO, like the 704's two ends). A sealed extent that must grow is moved to the top
 * (omega_tf_kv_move_top: the caller copies its rows; the old place becomes a hole).
 *
 * This file is bookkeeping only (offsets, no device): the caller owns one allocation of omega_tf_alloc_bytes(arena) bytes
 * (arena bytes + the attention prefetch slack) and maps offsets to addresses. Composes with:
 *   omega_lifetime (idea 1, omega#569): a record of kind SCRATCH is one region; omega_lt_plan places buffers inside it.
 *   the storage map (idea 6, sc#678): omega_tf_map prints every item: name, kind, lifetime class, offset, bytes, owner,
 *   position, and the gap, in front order.
 */
#include <stddef.h>
#include <stdint.h>

enum {
    OMEGA_TF_OK = 0,
    OMEGA_TF_BAD_ARGS = -1,
    OMEGA_TF_OVERFLOW = -2,  /* the fronts would cross */
    OMEGA_TF_NOT_TOP = -3,   /* only the top KV extent grows in place */
    OMEGA_TF_FULL_TABLE = -4 /* OMEGA_TF_MAX_ITEMS reached */
};

#define OMEGA_TF_MAX_ITEMS 256u
#define OMEGA_TF_ALIGN 4096u          /* every item starts on a 4 KiB page */
#define OMEGA_TF_BLOCK_POSITIONS 256u /* positions per KV block (the attention chunk) */

enum { OMEGA_TF_KV = 1, OMEGA_TF_GDN_LIVE = 2, OMEGA_TF_GDN_SNAPSHOT = 3, OMEGA_TF_SCRATCH = 4 };

typedef struct {
    uint64_t off, bytes;
    uint32_t kind;       /* OMEGA_TF_* above */
    uint32_t owner;      /* conversation id (caller's numbering) */
    uint32_t positions;  /* KV: positions covered (a multiple of 256); snapshot: the position it was taken at */
    int live;            /* 0 = released (a hole until its front retreats past it) */
    char name[32];
} OmegaTfItem;

typedef struct {
    uint64_t bytes;          /* usable arena bytes (a multiple of OMEGA_TF_ALIGN) */
    uint64_t block_bytes;    /* KV bytes per 256 positions */
    uint64_t low, high;      /* low front top (exclusive), high front bottom: [0, low) KV, [high, bytes) records */
    uint64_t min_gap;        /* smallest gap seen (high-water of the two fronts together) */
    uint32_t n;              /* items in table order (creation order; front order is by offset) */
    OmegaTfItem item[OMEGA_TF_MAX_ITEMS];
    uint32_t refusals;       /* OVERFLOW refusals so far */
    char err[256];
} OmegaTf;

/* bytes = arena size (rounded down to OMEGA_TF_ALIGN); block_bytes: KV bytes per block (a multiple of OMEGA_TF_ALIGN). */
int omega_tf_init(OmegaTf *tf, uint64_t bytes, uint64_t block_bytes);
/* The allocation the caller makes: bytes + the attention prefetch slack (reads past the last row, never written). */
uint64_t omega_tf_alloc_bytes(const OmegaTf *tf);
uint64_t omega_tf_gap(const OmegaTf *tf);

/* KV (low front). open: a new extent of ceil(positions / 256) blocks (0 allowed: an empty extent that grows) at the top;
 * the previous top extent is sealed. grow: the top extent to cover `positions` (never shrinks). shrink: rewind to
 * `positions` (a snapshot resume); whole blocks above are returned when it is the top extent. close: release. move_top:
 * a new top extent with the same positions; *old_off gets the old offset (the caller copies, then the old one is closed
 * here already: its bytes stay valid until the next low-front move). *id gets the item index. */
int omega_tf_kv_open(OmegaTf *tf, uint32_t owner, uint32_t positions, const char *name, uint32_t *id);
int omega_tf_kv_grow(OmegaTf *tf, uint32_t id, uint32_t positions);
int omega_tf_kv_shrink(OmegaTf *tf, uint32_t id, uint32_t positions);
int omega_tf_kv_move_top(OmegaTf *tf, uint32_t id, uint64_t *old_off, uint32_t *new_id);
/* Records (high front): push `bytes` (rounded up to OMEGA_TF_ALIGN) below the current high front. */
int omega_tf_rec_push(OmegaTf *tf, uint32_t kind, uint32_t owner, uint64_t bytes, uint32_t position, const char *name, uint32_t *id);
/* Release any item (KV extent or record); its front retreats through every released item at its edge. */
int omega_tf_release(OmegaTf *tf, uint32_t id);
const OmegaTfItem *omega_tf_item(const OmegaTf *tf, uint32_t id);

/* Independent check of the whole table (not the code paths above): live items inside their front, KV extents below
 * low and records at or above high, every item aligned, no two live items share a byte, low <= high <= bytes.
 * 0 ok; -1 and tf->err says what is wrong. */
int omega_tf_check(OmegaTf *tf);
/* The printed storage map (idea 6): one line per item in address order plus the gap; returns the length written
 * (truncated to cap - 1). */
size_t omega_tf_map(const OmegaTf *tf, char *buf, size_t cap);
const char *omega_tf_rc_name(int rc);

/* ---- the comparison (host, exact): today's fixed-maximum allocation vs the two-front arena ----
 * fixed:  every conversation reserves KV for max_pos in the plane layout (omega_gpu_decode_attn256_cache_alloc:
 *         per layer 2 planes of 4 * max_pos * 512 + OMEGA_DATTN256_PLANE_SLACK bytes) and one live DeltaNet set.
 * arena:  KV ceil(positions / 256) blocks of attn_layers MiB + the live set + `snapshots` snapshot sets, each item
 *         rounded to OMEGA_TF_ALIGN. */
uint64_t omega_tf_fixed_conv_bytes(uint32_t attn_layers, uint32_t max_pos, uint64_t gdn_set_bytes);
uint64_t omega_tf_arena_conv_bytes(uint32_t attn_layers, uint32_t positions, uint64_t gdn_set_bytes, uint32_t snapshots);

void omega_tf_test_set_mutant(int mutant); /* TEST ONLY: 1 = the overflow check is skipped (the fronts may cross) */

#endif
