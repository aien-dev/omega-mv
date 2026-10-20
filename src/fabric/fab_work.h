/*
 * fab_work.h -- the work message of the heterogeneous compute program
 * (aienix#6, omega#536): PST1 task envelope, HTK1 task request, HTR1 receipt,
 * the requester's task state machine and the executor's idempotence journal.
 * Production-buildable. No socket, file, process or clock call lives here:
 * the caller moves bytes and supplies time.
 *
 * Wire stack (contract 2026-10-18 D1): UDP -> AIENOS M6-B (aienos_sec.h) ->
 * M6-A frame (aienos_ctl.h, <= 1024 bytes) -> one message told apart by its
 * first four bytes: "AFAB" (fabric.h) or "PST1" (this file). The peer and the
 * session are facts the transport proved; no field here names them, and a
 * receipt whose executor id disagrees with the session peer is refused.
 *
 * PST1 (aien-protocols specs/pi-spark-task/SPEC.md, wire version 1): the
 * 224-byte header and the single-message checks are decoded here in the exact
 * order and with the exact codes of the reference checker
 * (specs/pi-spark-task/tools/pst_verify.c); tests/fabric/vectors/pst/ holds
 * that checker's vectors and this decoder must give the same verdict line for
 * every one. Roles: the REQUESTER sends request, cancel_request and
 * resume_request (the spec's "Pi side"); the EXECUTOR sends every other type.
 *
 * HTK1 (request payload, effect_class 1 COMPUTE), big-endian:
 *    0  4 "HTK1"   4 1 version 1   5 1 kind   6 2 reserved 0
 *    8  4 deadline_ms (1..600000)        12 4 max_out_bytes (1..64 MiB)
 *   16  8 max_mem_bytes                  24 4 max_in_bytes   28 4 threads (0 = executor's choice)
 *   32 32 input_digest = SHA-256(canonical input bytes of the kind)
 *   64 .. kind 1 MATMUL_I64_SEEDED: u32 n (1..512), u64 seed            (12 bytes)
 *         kind 2 TOKENIZE_FIXTURE: 32 fixture_digest, u32 offset, u32 length (<= 1 MiB),
 *                                  32 tokenizer_digest                  (72 bytes)
 *   kind 1 input bytes = "HTK1-MATMUL" | u32 n | u64 seed;
 *          A[i][j] = splitmix(seed, stream 0, i*n+j), B[i][j] = splitmix(seed, stream 1, i*n+j) as i64,
 *          C = A x B in wrapping i64; result bytes = C row-major, each i64 big-endian.
 *   kind 2 input bytes = fixture_digest | offset | length | tokenizer_digest; result bytes = token ids u32 BE
 *          (sovereign-core BitNetTokenizer::encode, bos=false). The tokenizer itself is not in this repo.
 *   splitmix(seed, stream, index): z = seed + (index+1)*0x9E3779B97F4A7C15 + stream*0xD1B54A32D192ED03;
 *          z = (z ^ z>>30)*0xBF58476D1CE4E5B9; z = (z ^ z>>27)*0x94D049BB133111EB; return z ^ z>>31  (all wrapping)
 *
 * HTR1 (receipt; the bytes after the 32-byte result digest in `completed`;
 * named by receipt_reference = SHA-256(HTR1 bytes)), big-endian, 228 bytes:
 *    0 4 "HTR1"  4 1 version 1  5 1 kind  6 2 flags (bits 0-1 sandbox: 0 none, 1 OS user, 2 namespaces; rest 0)
 *    8 16 run_id   24 44 executor machine id record   68 32 implementation_digest
 *  100 32 input_digest   132 32 result_digest   164 8 t_accept_us  172 8 t_start_us  180 8 t_end_us
 *  188 8 cpu_ns   196 8 peak_rss_bytes   204 2 threads_used   206 2 reserved 0
 *  208 16 os_arch (NUL padded)   224 4 identity_class ("lab" or "own", NUL padded)
 * `failed` payload: u16 code | UTF-8 reason (<= 200 bytes). Codes FW_FAIL_*.
 *
 * Requester state (D6): SENT -> ACCEPTED -> RUNNING -> COMPLETED | FAILED | CANCELLED;
 * any non-terminal state + session lost or deadline passed -> UNCERTAIN, which only
 * a resume_snapshot can leave. Executor ordinals per task start at 0 and rise by
 * one; anything else is refused and counted, state unchanged. A second terminal
 * is refused. Executor (D7): key (task_id, idempotence_key); a repeat with the
 * same HTK1 bytes replays the journaled terminal, a repeat with other bytes is
 * `failed` code 7; the journal store must report durable before a terminal may
 * be sent (fw_exec_record fails otherwise and nothing is sent).
 */
#ifndef FAB_WORK_H
#define FAB_WORK_H

#include "runtime/aien_machine_id.h"

#include <stddef.h>
#include <stdint.h>

/* ---- PST1 ---- */
#define PST_HDR        224u
#define PST_MAX_PAY    800u
#define PST_MAX_MSG    1024u
#define PST_VERSION    1u
#define PST_MAX_TYPE   14u

enum {
    PST_T_REQUEST = 1, PST_T_ACCEPTED, PST_T_QUEUED, PST_T_RUNNING, PST_T_TEXT_CHUNK,
    PST_T_WAITING_FOR_APPROVAL, PST_T_COMPLETED, PST_T_FAILED, PST_T_CANCELLED,
    PST_T_UNCERTAIN_AFTER_DISCONNECT, PST_T_RESUME_SNAPSHOT, PST_T_RECEIPT_REFERENCE,
    PST_T_CANCEL_REQUEST, PST_T_RESUME_REQUEST
};
enum { PST_EFFECT_NONE = 0, PST_EFFECT_COMPUTE = 1, PST_EFFECT_EFFECT = 2 };
enum {
    PST_OK = 0, PST_BAD_MAGIC, PST_BAD_VERSION, PST_DOWNGRADE, PST_BAD_TYPE,
    PST_WRONG_SIDE, PST_BAD_LENGTH, PST_TRAILING_BYTES, PST_RESERVED_NONZERO,
    PST_NO_CAUSE, PST_NO_TASK, PST_NO_OBJECTIVE, PST_DIGEST_MISMATCH,
    PST_BAD_EFFECT_CLASS, PST_SUBJECT_MISMATCH, PST_CODE_COUNT
};
enum { PST_ROLE_REQUESTER = 1, PST_ROLE_EXECUTOR = 2 };

typedef struct {
    uint8_t version, type;
    uint8_t cause[32], origin[32], objective[32], subject[32];
    uint8_t parent[16], task[16], idem[16];
    uint32_t ordinal;
    uint8_t effect;
    uint16_t payload_len;
    uint8_t digest[32];
    const uint8_t *payload;     /* decode: points into the caller's buffer */
} PstMsg;

/* Decodes and checks one message in the spec's order. PST_OK or the first refusal. */
int  pst_decode(const uint8_t *b, size_t n, PstMsg *m);
/* Writes header + payload (digest computed here). The header fields are
 * checked with the decode rules first. cap >= PST_HDR + payload_len. */
int  pst_encode(const PstMsg *m, const uint8_t *payload, size_t payload_len,
                uint8_t *out, size_t cap, size_t *out_len);
/* The role that sends this type (PST_ROLE_*), 0 for an unknown type. */
int  pst_side_of(uint8_t type);
/* PST_OK when `sender_role` may send `type`, else PST_WRONG_SIDE. */
int  pst_side_check(uint8_t type, int sender_role);
const char *pst_code_name(int code);
/* The reference checker's verdict line ("ok type=... digest=..." or "refuse PST_..."). */
void pst_verdict_line(int code, const PstMsg *m, char *buf, size_t cap);

/* ---- HTK1 ---- */
#define HTK_HDR              64u
#define HTK_MAX_BYTES        (HTK_HDR + 72u)
#define HTK_INPUT_MAX        72u
#define HTK_KIND_MATMUL_I64_SEEDED  1u
#define HTK_KIND_TOKENIZE_FIXTURE   2u
#define HTK_MATMUL_N_MAX     512u
#define HTK_TOKENIZE_LEN_MAX (1u << 20)
#define HTK_DEADLINE_MS_MAX  600000u
#define HTK_OUT_BYTES_MAX    (64u << 20)
enum {
    HTK_OK = 0, HTK_BAD_MAGIC, HTK_BAD_VERSION, HTK_BAD_KIND, HTK_RESERVED_NONZERO,
    HTK_BAD_LENGTH, HTK_BAD_BOUNDS, HTK_INPUT_DIGEST_MISMATCH, HTK_CODE_COUNT
};

typedef struct {
    uint8_t kind;
    uint32_t deadline_ms, max_out_bytes;
    uint64_t max_mem_bytes;
    uint32_t max_in_bytes, threads;
    uint8_t input_digest[32];
    struct { uint32_t n; uint64_t seed; } matmul;                       /* kind 1 */
    struct { uint8_t fixture_digest[32]; uint32_t offset, length; uint8_t tokenizer_digest[32]; } tok; /* kind 2 */
} HtkTask;

/* Canonical input bytes of the task (digest preimage). Returns their length, 0 for an unknown kind. */
size_t htk_input_bytes(const HtkTask *t, uint8_t out[HTK_INPUT_MAX]);
void   htk_input_digest(const HtkTask *t, uint8_t out[32]);
/* Checks bounds, writes the bytes; input_digest is computed here (t->input_digest is ignored). */
int    htk_encode(const HtkTask *t, uint8_t *out, size_t cap, size_t *len);
/* Checks magic, version, kind, reserved, length, bounds, then input_digest. */
int    htk_decode(const uint8_t *b, size_t n, HtkTask *t);
/* Result size the kind defines (kind 1: n*n*8), 0 when the kind leaves it to the executor. */
uint64_t htk_result_bytes(const HtkTask *t);
const char *htk_code_name(int code);

/* The matmul definition (normative in C; the worker and the benchmarks oracle
 * re-implement it independently and must reproduce the digests). */
uint64_t htk_splitmix(uint64_t seed, uint64_t stream, uint64_t index);
/* c holds n*n values on return. HTK_BAD_BOUNDS for n outside 1..512. */
int  htk_matmul_i64(uint32_t n, uint64_t seed, int64_t *c);
/* Result bytes: row-major, each i64 big-endian; out holds n*n*8 bytes. */
void htk_matmul_result_bytes(uint32_t n, const int64_t *c, uint8_t *out);
/* Runs the matmul into scratch (n*n i64) and digests its result bytes in place. */
int  htk_matmul_result_digest(uint32_t n, uint64_t seed, int64_t *scratch, uint8_t out[32]);

/* ---- HTR1 ---- */
#define HTR_BYTES      228u
#define HTR_MAX_BYTES  768u
#define HTR_OS_ARCH    16u
enum { HTR_OK = 0, HTR_BAD_MAGIC, HTR_BAD_VERSION, HTR_BAD_LENGTH, HTR_RESERVED_NONZERO,
       HTR_BAD_FLAGS, HTR_BAD_MACHINE, HTR_BAD_KIND, HTR_CODE_COUNT };
enum { HTR_SANDBOX_NONE = 0, HTR_SANDBOX_OS_USER = 1, HTR_SANDBOX_NAMESPACES = 2 };

typedef struct {
    uint8_t version, kind;
    uint16_t flags;
    uint8_t run_id[16];
    AienMachineId executor;
    uint8_t implementation_digest[32], input_digest[32], result_digest[32];
    uint64_t t_accept_us, t_start_us, t_end_us, cpu_ns, peak_rss_bytes;
    uint16_t threads_used;
    char os_arch[HTR_OS_ARCH];      /* NUL padded, not necessarily terminated */
    char identity_class[4];         /* "lab" or "own", NUL padded */
} HtrReceipt;

int  htr_encode(const HtrReceipt *r, uint8_t out[HTR_BYTES]);
int  htr_decode(const uint8_t *b, size_t n, HtrReceipt *r);
/* receipt_reference = SHA-256 of the receipt bytes. */
void htr_reference(const uint8_t *bytes, size_t len, uint8_t out[32]);
const char *htr_code_name(int code);

/* ---- failure codes (failed payload: u16 code | reason) ---- */
enum {
    FW_FAIL_BOUNDS = 1, FW_FAIL_UNKNOWN_KIND = 2, FW_FAIL_DEADLINE = 3, FW_FAIL_MEMORY = 4,
    FW_FAIL_CANCELLED_BEFORE_START = 5, FW_FAIL_INTERNAL = 6, FW_FAIL_DUPLICATE_DIFFERENT = 7
};
#define FW_REASON_MAX 200u
/* completed payload: result_digest | HTR1 (r may be NULL: digest only). */
int fw_completed_payload(const uint8_t result_digest[32], const HtrReceipt *r,
                         uint8_t *out, size_t cap, size_t *len);
int fw_failed_payload(uint16_t code, const char *reason, uint8_t *out, size_t cap, size_t *len);

/* ---- requester ---- */
#define FW_MAX_TASKS 32u
enum { FW_ST_SENT = 1, FW_ST_ACCEPTED, FW_ST_RUNNING, FW_ST_COMPLETED, FW_ST_FAILED,
       FW_ST_CANCELLED, FW_ST_UNCERTAIN };
enum {
    FW_R_OK = 0,
    FW_R_WRONG_SIDE,         /* a requester-side type arrived */
    FW_R_UNKNOWN_TASK,       /* task id / idempotence key not ours */
    FW_R_ORDINAL,            /* not the next ordinal for this task */
    FW_R_TERMINAL,           /* task already terminal (duplicate terminal, late message) */
    FW_R_STATE,              /* type not allowed in the task's current state */
    FW_R_BAD_PAYLOAD,        /* payload too short or undecodable for the type */
    FW_R_RECEIPT_PEER,       /* receipt executor id is not the session peer */
    FW_R_RECEIPT_INPUT,      /* receipt input_digest is not the task's */
    FW_R_RESULT_DIGEST,      /* receipt result_digest differs from the completed digest, or result bytes differ */
    FW_R_UNSUPPORTED,        /* a type HTK work does not use (text_chunk, waiting_for_approval) */
    FW_R_FULL,
    FW_R_N
};

typedef struct {
    uint8_t task[16], idem[16];
    uint32_t state;
    uint32_t next_ordinal;          /* executor ordinal expected next */
    uint64_t deadline_us;           /* absolute; passing it non-terminal -> UNCERTAIN */
    uint8_t input_digest[32];
    uint8_t result_digest[32];
    uint8_t has_receipt;
    HtrReceipt receipt;
    uint8_t has_receipt_reference;
    uint8_t receipt_reference[32];
    uint16_t fail_code;
    uint8_t resume_unknown;         /* resume_snapshot said the executor never journaled it */
} FwTask;

typedef struct {
    uint32_t n;
    FwTask t[FW_MAX_TASKS];
    uint64_t applied;
    uint64_t refused[FW_R_N];
    int last_refusal;
} FwRequester;

void    fw_req_init(FwRequester *r);
/* Registers a sent request. FW_R_FULL, or FW_R_STATE for a task id already held. */
int     fw_req_send(FwRequester *r, const uint8_t task[16], const uint8_t idem[16],
                    const uint8_t input_digest[32], uint64_t deadline_us);
FwTask *fw_req_find(FwRequester *r, const uint8_t task[16]);
/* Applies one decoded executor message received on the session with `peer`. FW_R_*. */
int     fw_req_receive(FwRequester *r, const PstMsg *m, const AienMachineId *peer, uint64_t now_us);
/* Every non-terminal task -> UNCERTAIN. Returns how many changed. */
int     fw_req_session_lost(FwRequester *r);
/* Non-terminal tasks past their deadline -> UNCERTAIN. Returns how many changed. */
int     fw_req_tick(FwRequester *r, uint64_t now_us);
/* FW_R_OK when the task is COMPLETED and SHA-256(result) is its result digest. */
int     fw_req_verify_result(const FwTask *t, const uint8_t *result, size_t len);
int     fw_state_terminal(uint32_t state);
const char *fw_state_name(uint32_t state);
const char *fw_req_code_name(int code);

/* ---- executor ---- */
#define FW_JOURNAL_ENTRIES     256u
#define FW_JOURNAL_ENTRY_BYTES (16u + 16u + 32u + 1u + 2u + 1u + 32u + HTR_BYTES)   /* 328 */
enum { FW_ADMIT_RUN = 0, FW_ADMIT_REPLAY = 1, FW_ADMIT_REFUSE = 2 };
enum {
    FW_X_OK = 0,
    FW_X_WRONG_SIDE,         /* not a request: refuse without reply (session rule) */
    FW_X_BAD_HTK,            /* HTK1 undecodable -> failed code 6 */
    FW_X_KIND,               /* unknown kind -> failed code 2 */
    FW_X_BOUNDS,             /* outside this executor's limits -> failed code 1 */
    FW_X_DUPLICATE_DIFFERENT,/* same key, other bytes -> failed code 7 */
    FW_X_STORE,              /* journal store refused: nothing may be sent */
    FW_X_N
};

typedef struct {
    uint8_t task[16], idem[16];
    uint8_t htk_digest[32];         /* SHA-256 of the request payload (HTK1 bytes) */
    uint8_t terminal_type;          /* PST_T_COMPLETED, PST_T_FAILED or PST_T_CANCELLED */
    uint16_t fail_code;
    uint8_t has_receipt;
    uint8_t result_digest[32];
    HtrReceipt receipt;
} FwJournalEntry;

/* Durable append. 0 only once the record is on stable storage. */
typedef struct {
    void *ctx;
    int (*append)(void *ctx, const uint8_t *rec, size_t len);
} FwJournalStore;

typedef struct {
    uint32_t cpus;                  /* threads > cpus is refused */
    uint64_t mem_bytes;             /* max_mem_bytes above this is refused */
    uint32_t max_out_bytes_cap;     /* 0 = HTK_OUT_BYTES_MAX */
    uint32_t deadline_ms_cap;       /* 0 = HTK_DEADLINE_MS_MAX */
    uint32_t kinds;                 /* bit (1<<kind) set for each kind served */
} FwExecLimits;

typedef struct {
    const FwJournalStore *store;
    uint32_t head, count;
    FwJournalEntry e[FW_JOURNAL_ENTRIES];
    uint64_t admitted, replayed, recorded;
    uint64_t refused[FW_X_N];
} FwExecutor;

int  fw_exec_init(FwExecutor *x, const FwJournalStore *store);
/* Reloads one journaled entry (from the store's file image) into the ring. */
int  fw_exec_load(FwExecutor *x, const uint8_t *rec, size_t len);
const FwJournalEntry *fw_exec_lookup(const FwExecutor *x, const uint8_t task[16], const uint8_t idem[16]);
/* Admission of a decoded `request`. FW_ADMIT_RUN: *task filled, run it.
 * FW_ADMIT_REPLAY: *replay is the journaled terminal, send it again, no run.
 * FW_ADMIT_REFUSE: *fail_code is the `failed` code to send (0 = send nothing). */
int  fw_exec_admit(FwExecutor *x, const PstMsg *m, const FwExecLimits *lim, HtkTask *task,
                   const FwJournalEntry **replay, uint16_t *fail_code);
/* Journals a terminal: encodes, store->append must return 0, then it is held.
 * FW_X_STORE (nonzero) means nothing was journaled and nothing may be sent. */
int  fw_exec_record(FwExecutor *x, const FwJournalEntry *e);
int  fw_journal_encode(const FwJournalEntry *e, uint8_t out[FW_JOURNAL_ENTRY_BYTES]);
int  fw_journal_decode(const uint8_t *b, size_t n, FwJournalEntry *e);
/* The payload of the terminal message for a journal entry (completed: digest|HTR1;
 * failed: code|reason; cancelled: empty). */
int  fw_terminal_payload(const FwJournalEntry *e, const char *reason, uint8_t *out, size_t cap, size_t *len);
/* resume_snapshot payload: u8 terminal_type (0 = unknown task) | terminal payload. */
int  fw_resume_snapshot_payload(const FwJournalEntry *e, uint8_t *out, size_t cap, size_t *len);
const char *fw_exec_code_name(int code);

#endif /* FAB_WORK_H */
