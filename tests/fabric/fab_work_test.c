/*
 * fab_work_test.c -- conformance and negative tests for fab_work.h,
 * fab_auth_ed25519.h and fab_route.h (aienix#6, omega#536).
 *
 *   fab_work_test [VECTOR_DIR]        in-process suite; prints FAB_WORK_PASS
 *   fab_work_test --emit DIR          writes the HTK1/HTR1 golden vectors
 *   fab_work_test --oracle            prints the matmul oracle digests
 *   fab_work_test --executor JOURNAL  one executor process: PST1 frames on stdin/stdout
 *   fab_work_test --requester PEERHEX one requester process: drives an executor over stdin/stdout
 *
 * Frame on a pipe: u16 big-endian length, then that many bytes (one PST1
 * message). Two OS processes over pipes prove "separate process"; they are
 * not a network, not M6-A/M6-B, and qualify nothing physical.
 */
#include "fabric.h"
#include "fab_auth_ed25519.h"
#include "fab_loopback.h"
#include "fab_route.h"
#include "fab_work.h"
#include "searchtrace/sig/aienos_sig.h"
#include "sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int checks, failures;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static void hex(const uint8_t *b, size_t n, char *out) {
    static const char hx[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) { out[2 * i] = hx[b[i] >> 4]; out[2 * i + 1] = hx[b[i] & 15]; }
    out[2 * n] = 0;
}
static int unhex(const char *s, uint8_t *out, size_t n) {
    if (strlen(s) != 2 * n) return -1;
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        if (sscanf(s + 2 * i, "%2x", &v) != 1) return -1;
        out[i] = (uint8_t)v;
    }
    return 0;
}
static void fill(uint8_t *b, size_t n, const char *label) {
    uint8_t d[32];
    sha256_hash((const uint8_t *)label, strlen(label), d);
    for (size_t i = 0; i < n; i++) b[i] = d[i % 32];
}
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static uint16_t get16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

/* ---- 1. PST1 vectors: same verdict line as protocols tools/pst_verify.c ---- */
static void test_pst_vectors(const char *dir) {
    char path[2048];
    snprintf(path, sizeof path, "%s/expected.txt", dir);
    FILE *f = fopen(path, "r");
    CHECK(f != NULL, "open %s", path);
    if (!f) return;
    char line[1024];
    int n_vec = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char *sp = strchr(line, ' ');
        if (!sp) continue;
        *sp = 0;
        char *want = sp + 1;
        want[strcspn(want, "\n")] = 0;
        snprintf(path, sizeof path, "%s/%s", dir, line);
        FILE *v = fopen(path, "rb");
        CHECK(v != NULL, "open vector %s", path);
        if (!v) continue;
        uint8_t buf[PST_MAX_MSG + 64];
        size_t n = fread(buf, 1, sizeof buf, v);
        fclose(v);
        PstMsg m;
        int code = pst_decode(buf, n, &m);
        char got[1024];
        pst_verdict_line(code, &m, got, sizeof got);
        CHECK(strcmp(got, want) == 0, "%s:\n  want %s\n  got  %s", line, want, got);
        n_vec++;
    }
    fclose(f);
    CHECK(n_vec == 23, "23 vectors, saw %d", n_vec);
    printf("pst1 vectors: %d checked against expected.txt\n", n_vec);
}

/* ---- 2. HTK1 / HTR1 goldens ---- */
static void sample_matmul(HtkTask *t) {
    memset(t, 0, sizeof *t);
    t->kind = HTK_KIND_MATMUL_I64_SEEDED;
    t->deadline_ms = 30000;
    t->max_out_bytes = 128;
    t->max_mem_bytes = 64u << 20;
    t->max_in_bytes = 0;
    t->threads = 0;
    t->matmul.n = 4;
    t->matmul.seed = 1;
}
static void sample_tok(HtkTask *t) {
    memset(t, 0, sizeof *t);
    t->kind = HTK_KIND_TOKENIZE_FIXTURE;
    t->deadline_ms = 60000;
    t->max_out_bytes = 1u << 20;
    t->max_mem_bytes = 512u << 20;
    t->max_in_bytes = 4096;
    t->threads = 1;
    fill(t->tok.fixture_digest, 32, "fixture");
    t->tok.offset = 0;
    t->tok.length = 4096;
    fill(t->tok.tokenizer_digest, 32, "tokenizer");
}
static AienMachineId test_machine(const char *label) {
    uint8_t sk[32], pk[32];
    AienMachineId id;
    fill(sk, 32, label);
    aienos_ed25519_public_key(pk, sk);
    fab_ed_machine_id(pk, &id);
    return id;
}
static void sample_receipt(HtrReceipt *r) {
    memset(r, 0, sizeof *r);
    r->version = 1;
    r->kind = HTK_KIND_MATMUL_I64_SEEDED;
    r->flags = HTR_SANDBOX_OS_USER;
    fill(r->run_id, 16, "run");
    r->executor = test_machine("het-test-executor");
    fill(r->implementation_digest, 32, "impl");
    HtkTask t;
    sample_matmul(&t);
    htk_input_digest(&t, r->input_digest);
    fill(r->result_digest, 32, "result");
    r->t_accept_us = 1000; r->t_start_us = 1500; r->t_end_us = 9000;
    r->cpu_ns = 7000000; r->peak_rss_bytes = 2u << 20; r->threads_used = 1;
    memcpy(r->os_arch, "linux-aarch64", 13);
    memcpy(r->identity_class, "lab", 3);
}
static size_t read_file(const char *path, uint8_t *buf, size_t cap) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    return n;
}
static int write_file(const char *path, const uint8_t *b, size_t n) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int ok = fwrite(b, 1, n, f) == n;
    return fclose(f) == 0 && ok ? 0 : -1;
}
typedef struct { const char *name; uint8_t b[PST_MAX_PAY]; size_t n; } Golden;
static int goldens(Golden g[3]) {
    HtkTask t;
    sample_matmul(&t);
    g[0].name = "htk1-matmul-n4-seed1.bin";
    if (htk_encode(&t, g[0].b, sizeof g[0].b, &g[0].n) != HTK_OK) return -1;
    sample_tok(&t);
    g[1].name = "htk1-tokenize-fixture.bin";
    if (htk_encode(&t, g[1].b, sizeof g[1].b, &g[1].n) != HTK_OK) return -1;
    HtrReceipt r;
    sample_receipt(&r);
    g[2].name = "htr1-sample.bin";
    if (htr_encode(&r, g[2].b) != HTR_OK) return -1;
    g[2].n = HTR_BYTES;
    return 0;
}
static int emit(const char *dir) {
    Golden g[3];
    if (goldens(g) != 0) return 1;
    for (int i = 0; i < 3; i++) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dir, g[i].name);
        if (write_file(path, g[i].b, g[i].n) != 0) { perror(path); return 1; }
        printf("wrote %s (%zu bytes)\n", path, g[i].n);
    }
    return 0;
}
static void test_goldens(const char *dir) {
    Golden g[3];
    CHECK(goldens(g) == 0, "goldens encode");
    for (int i = 0; i < 3; i++) {
        char path[1024];
        uint8_t buf[PST_MAX_PAY];
        snprintf(path, sizeof path, "%s/../%s", dir, g[i].name);
        size_t n = read_file(path, buf, sizeof buf);
        CHECK(n == g[i].n && memcmp(buf, g[i].b, n) == 0, "golden %s differs from encode (%zu vs %zu bytes)", g[i].name, n, g[i].n);
    }
    HtkTask t, u;
    sample_matmul(&t);
    CHECK(htk_decode(g[0].b, g[0].n, &u) == HTK_OK && u.kind == 1 && u.matmul.n == 4 && u.matmul.seed == 1 &&
          u.deadline_ms == 30000 && u.max_out_bytes == 128, "htk1 matmul roundtrip");
    sample_tok(&t);
    CHECK(htk_decode(g[1].b, g[1].n, &u) == HTK_OK && u.kind == 2 && u.tok.length == 4096 &&
          memcmp(u.tok.tokenizer_digest, t.tok.tokenizer_digest, 32) == 0, "htk1 tokenize roundtrip");
    HtrReceipt r, s;
    sample_receipt(&r);
    CHECK(htr_decode(g[2].b, HTR_BYTES, &s) == HTR_OK && aien_mid_equal(&s.executor, &r.executor) &&
          s.t_end_us == 9000 && s.threads_used == 1 && memcmp(s.os_arch, "linux-aarch64", 13) == 0 &&
          memcmp(s.identity_class, "lab", 3) == 0 && s.flags == HTR_SANDBOX_OS_USER, "htr1 roundtrip");
    /* negatives on the codecs */
    uint8_t b[PST_MAX_PAY];
    memcpy(b, g[0].b, g[0].n);
    b[5] = 9;                                   CHECK(htk_decode(b, g[0].n, &u) == HTK_BAD_KIND, "htk unknown kind");
    memcpy(b, g[0].b, g[0].n); b[6] = 1;        CHECK(htk_decode(b, g[0].n, &u) == HTK_RESERVED_NONZERO, "htk reserved");
    memcpy(b, g[0].b, g[0].n); b[4] = 2;        CHECK(htk_decode(b, g[0].n, &u) == HTK_BAD_VERSION, "htk version");
    memcpy(b, g[0].b, g[0].n);                  CHECK(htk_decode(b, g[0].n - 1, &u) == HTK_BAD_LENGTH, "htk short");
    memcpy(b, g[0].b, g[0].n); b[66] = 3;       CHECK(htk_decode(b, g[0].n, &u) == HTK_BAD_BOUNDS, "htk n=0x0304 > 512 is bounds");
    memcpy(b, g[0].b, g[0].n); b[75] = 2;       CHECK(htk_decode(b, g[0].n, &u) == HTK_INPUT_DIGEST_MISMATCH, "htk seed changed, digest stale");
    memcpy(b, g[0].b, g[0].n); b[11] = 0; b[10] = 0; b[9] = 0; b[8] = 0; CHECK(htk_decode(b, g[0].n, &u) == HTK_BAD_BOUNDS, "htk deadline 0");
    memcpy(b, g[2].b, HTR_BYTES); b[7] = 4;     CHECK(htr_decode(b, HTR_BYTES, &s) == HTR_BAD_FLAGS, "htr flags");
    memcpy(b, g[2].b, HTR_BYTES); b[207] = 1;   CHECK(htr_decode(b, HTR_BYTES, &s) == HTR_RESERVED_NONZERO, "htr reserved");
    memcpy(b, g[2].b, HTR_BYTES); b[24] ^= 0xff; CHECK(htr_decode(b, HTR_BYTES, &s) == HTR_BAD_MACHINE, "htr machine record");
    CHECK(htr_decode(g[2].b, HTR_BYTES - 1, &s) == HTR_BAD_LENGTH, "htr short");
    printf("htk1/htr1 goldens: 3 files match, roundtrips and 11 negatives\n");
}

/* ---- 3. matmul oracle ---- */
typedef struct { uint32_t n; uint64_t seed; const char *digest; } Oracle;
/* Computed by this implementation on 2026-10-18 (first run); the benchmarks lane's
 * independent oracle and the Rust worker must reproduce them, or the definition is unclear. */
static const Oracle oracle[3] = {
    { 4, 1, "705d18096163be46e7cc44b757fe26787ed059045954ff99a2a94c6e575607cb" },
    { 64, 7, "f0d209befe915f5f624d48b784c3acd10f51ef2df8f41e0f92580e3770ae07d0" },
    { 256, 42, "74f59d70c133f9573943edf21b781e4e19c57e9ab6c822036542e8209c9211cc" },
};
static int64_t scratch[HTK_MATMUL_N_MAX * HTK_MATMUL_N_MAX];
static void print_oracle(void) {
    for (int i = 0; i < 3; i++) {
        uint8_t d[32];
        char h[65];
        htk_matmul_result_digest(oracle[i].n, oracle[i].seed, scratch, d);
        hex(d, 32, h);
        printf("matmul n=%u seed=%llu result_digest=%s\n", oracle[i].n, (unsigned long long)oracle[i].seed, h);
    }
}
static void test_oracle(void) {
    /* The definition is pinned by hand values first: n=1 is a single product. */
    int64_t one;
    CHECK(htk_matmul_i64(1, 5, &one) == HTK_OK, "n=1");
    uint64_t a = htk_splitmix(5, 0, 0), b = htk_splitmix(5, 1, 0);
    CHECK((uint64_t)one == a * b, "n=1 is a*b wrapping");
    CHECK(htk_splitmix(0, 0, 0) != htk_splitmix(0, 1, 0), "streams differ");
    CHECK(htk_matmul_i64(0, 1, scratch) == HTK_BAD_BOUNDS && htk_matmul_i64(513, 1, scratch) == HTK_BAD_BOUNDS, "n bounds");
    for (int i = 0; i < 3; i++) {
        uint8_t d[32];
        char h[65];
        CHECK(htk_matmul_result_digest(oracle[i].n, oracle[i].seed, scratch, d) == HTK_OK, "digest n=%u", oracle[i].n);
        hex(d, 32, h);
        CHECK(strcmp(h, oracle[i].digest) == 0, "oracle n=%u seed=%llu: want %s got %s", oracle[i].n,
              (unsigned long long)oracle[i].seed, oracle[i].digest, h);
    }
    printf("matmul oracle: 3 digests reproduced\n");
}

/* ---- in-memory executor used by the unit tests and the --executor process ---- */
typedef struct { FILE *f; int fail_next; uint64_t appended; } MemStore;
static int store_append(void *ctx, const uint8_t *rec, size_t len) {
    MemStore *s = ctx;
    if (s->fail_next) { s->fail_next = 0; return -1; }
    if (s->f) {
        if (fwrite(rec, 1, len, s->f) != len || fflush(s->f) != 0 || fsync(fileno(s->f)) != 0) return -1;
    }
    s->appended++;
    return 0;
}

typedef struct {
    FwExecutor x;
    FwExecLimits lim;
    MemStore store_ctx;
    FwJournalStore store;
    AienMachineId self;
    uint8_t impl_digest[32];
    uint32_t ordinal[FW_MAX_TASKS];     /* per-task send ordinal, keyed by task slot below */
    uint8_t task_ids[FW_MAX_TASKS][16];
    uint32_t n_tasks;
    uint64_t now_us;
} Exec;

static uint32_t exec_slot(Exec *e, const uint8_t task[16]) {
    for (uint32_t i = 0; i < e->n_tasks; i++) if (memcmp(e->task_ids[i], task, 16) == 0) return i;
    uint32_t i = e->n_tasks < FW_MAX_TASKS ? e->n_tasks++ : FW_MAX_TASKS - 1;
    memcpy(e->task_ids[i], task, 16);
    e->ordinal[i] = 0;
    return i;
}
static void exec_init(Exec *e, FILE *journal) {
    memset(e, 0, sizeof *e);
    e->store_ctx.f = journal;
    e->store.ctx = &e->store_ctx;
    e->store.append = store_append;
    fw_exec_init(&e->x, &e->store);
    e->lim.cpus = 4;
    e->lim.mem_bytes = 1ull << 30;
    e->lim.kinds = 1u << HTK_KIND_MATMUL_I64_SEEDED;   /* the C test executor serves matmul only */
    e->self = test_machine("het-test-executor");
    fill(e->impl_digest, 32, "fab_work_test executor");
    e->now_us = 1000000;
}
/* Builds one executor->requester message for task m with the given type/payload. */
static size_t exec_msg(Exec *e, const PstMsg *req, uint8_t type, const uint8_t *pl, size_t pn, uint8_t *out, size_t cap) {
    PstMsg m = *req;
    m.type = type;
    m.ordinal = e->ordinal[exec_slot(e, req->task)]++;
    size_t n = 0;
    if (pst_encode(&m, pl, pn, out, cap, &n) != PST_OK) return 0;
    return n;
}
/* Handles one decoded request; writes 0..3 reply messages into out (each length-prefixed u16). */
static size_t exec_handle(Exec *e, const PstMsg *req, uint8_t *out, size_t cap) {
    size_t used = 0;
    uint8_t pl[PST_MAX_PAY];
    size_t pn = 0;
#define EMIT(type, p, n) do { \
        size_t _m = exec_msg(e, req, (type), (p), (n), out + used + 2, cap - used - 2); \
        if (!_m) return used; \
        put16(out + used, (uint16_t)_m); \
        used += 2 + _m; \
    } while (0)
    if (req->type == PST_T_RESUME_REQUEST) {
        const FwJournalEntry *j = fw_exec_lookup(&e->x, req->task, req->idem);
        fw_resume_snapshot_payload(j, pl, sizeof pl, &pn);
        EMIT(PST_T_RESUME_SNAPSHOT, pl, pn);
        return used;
    }
    HtkTask t;
    const FwJournalEntry *replay = NULL;
    uint16_t code = 0;
    int a = fw_exec_admit(&e->x, req, &e->lim, &t, &replay, &code);
    if (a == FW_ADMIT_REPLAY) {
        fw_terminal_payload(replay, "replayed", pl, sizeof pl, &pn);
        EMIT(replay->terminal_type, pl, pn);
        return used;
    }
    if (a == FW_ADMIT_REFUSE) {
        if (!code) return 0;                    /* wrong side: nothing is sent */
        /* A code-7 answer is about the key's existing entry, not a new terminal
         * for the key: it is never journaled (the original bytes still replay). */
        if (code != FW_FAIL_DUPLICATE_DIFFERENT) {
            FwJournalEntry j = {0};
            memcpy(j.task, req->task, 16); memcpy(j.idem, req->idem, 16); memcpy(j.htk_digest, req->digest, 32);
            j.terminal_type = PST_T_FAILED; j.fail_code = code;
            if (fw_exec_record(&e->x, &j) != FW_X_OK) return 0;   /* not journaled: not sent */
        }
        fw_failed_payload(code, fw_exec_code_name(code == FW_FAIL_BOUNDS ? FW_X_BOUNDS : code == FW_FAIL_UNKNOWN_KIND ? FW_X_KIND : code == FW_FAIL_DUPLICATE_DIFFERENT ? FW_X_DUPLICATE_DIFFERENT : FW_X_BAD_HTK), pl, sizeof pl, &pn);
        EMIT(PST_T_FAILED, pl, pn);
        return used;
    }
    EMIT(PST_T_ACCEPTED, NULL, 0);
    EMIT(PST_T_RUNNING, NULL, 0);
    uint64_t t_accept = e->now_us, t_start = e->now_us + 10;
    uint8_t rd[32];
    htk_matmul_result_digest(t.matmul.n, t.matmul.seed, scratch, rd);
    e->now_us += 1000;
    HtrReceipt r = {0};
    r.version = 1; r.kind = t.kind; r.flags = HTR_SANDBOX_NONE;
    fill(r.run_id, 16, "run-id"); r.run_id[15] = (uint8_t)e->x.recorded;
    r.executor = e->self;
    memcpy(r.implementation_digest, e->impl_digest, 32);
    memcpy(r.input_digest, t.input_digest, 32);
    memcpy(r.result_digest, rd, 32);
    r.t_accept_us = t_accept; r.t_start_us = t_start; r.t_end_us = e->now_us;
    r.cpu_ns = 1000 * 1000; r.peak_rss_bytes = sizeof scratch; r.threads_used = 1;
    memcpy(r.os_arch, "linux-test", 10);
    memcpy(r.identity_class, "lab", 3);
    FwJournalEntry j = {0};
    memcpy(j.task, req->task, 16); memcpy(j.idem, req->idem, 16); memcpy(j.htk_digest, req->digest, 32);
    j.terminal_type = PST_T_COMPLETED; j.has_receipt = 1; j.receipt = r; memcpy(j.result_digest, rd, 32);
    if (fw_exec_record(&e->x, &j) != FW_X_OK) return used;    /* journal refused: no terminal is sent */
    fw_terminal_payload(&j, NULL, pl, sizeof pl, &pn);
    EMIT(PST_T_COMPLETED, pl, pn);
#undef EMIT
    return used;
}

/* ---- requester side helpers ---- */
static PstMsg make_request(const char *task_label, const char *idem_label, const HtkTask *t, uint8_t *pl, size_t *pn) {
    PstMsg m;
    memset(&m, 0, sizeof m);
    m.version = 1; m.type = PST_T_REQUEST; m.effect = PST_EFFECT_COMPUTE;
    fill(m.cause, 32, "cause"); fill(m.origin, 32, "origin"); fill(m.objective, 32, "objective"); fill(m.subject, 32, "subject");
    fill(m.task, 16, task_label); fill(m.idem, 16, idem_label);
    CHECK(htk_encode(t, pl, PST_MAX_PAY, pn) == HTK_OK, "encode htk");
    return m;
}
/* Feeds every length-prefixed message in buf to the requester; returns the last code. */
static int feed(FwRequester *r, const uint8_t *buf, size_t n, const AienMachineId *peer, int *count) {
    int last = FW_R_OK;
    size_t off = 0;
    while (off + 2 <= n) {
        uint16_t len = get16(buf + off);
        PstMsg m;
        CHECK(pst_decode(buf + off + 2, len, &m) == PST_OK, "reply decodes");
        last = fw_req_receive(r, &m, peer, 0);
        if (count) (*count)++;
        off += 2 + len;
    }
    return last;
}

/* ---- 4. requester <-> executor in one process, with the named negatives ---- */
static void test_state_machines(void) {
    Exec e;
    exec_init(&e, NULL);
    FwRequester r;
    fw_req_init(&r);
    AienMachineId peer = e.self, stranger = test_machine("stranger");
    uint8_t pl[PST_MAX_PAY], msg[PST_MAX_MSG], replies[8 * PST_MAX_MSG];
    size_t pn, mn, rn;
    HtkTask t;
    sample_matmul(&t);

    /* happy path: request -> accepted, running, completed(HTR1); result verifies */
    PstMsg q = make_request("task-1", "idem-1", &t, pl, &pn);
    uint8_t in_digest[32];
    htk_input_digest(&t, in_digest);
    CHECK(fw_req_send(&r, q.task, q.idem, in_digest, 5000000) == FW_R_OK, "send");
    CHECK(pst_encode(&q, pl, pn, msg, sizeof msg, &mn) == PST_OK, "encode request");
    PstMsg qd;
    CHECK(pst_decode(msg, mn, &qd) == PST_OK, "request decodes");
    CHECK(pst_side_check(qd.type, PST_ROLE_REQUESTER) == PST_OK && pst_side_check(qd.type, PST_ROLE_EXECUTOR) == PST_WRONG_SIDE, "side rule");
    rn = exec_handle(&e, &qd, replies, sizeof replies);
    int cnt = 0;
    CHECK(feed(&r, replies, rn, &peer, &cnt) == FW_R_OK && cnt == 3, "3 replies applied (%d)", cnt);
    FwTask *ft = fw_req_find(&r, q.task);
    CHECK(ft && ft->state == FW_ST_COMPLETED && ft->has_receipt && ft->next_ordinal == 3, "COMPLETED with receipt");
    uint8_t want[32];
    htk_matmul_result_digest(4, 1, scratch, want);
    CHECK(memcmp(ft->result_digest, want, 32) == 0, "result digest is the oracle's");
    uint8_t result[128];
    htk_matmul_i64(4, 1, scratch);
    htk_matmul_result_bytes(4, scratch, result);
    CHECK(fw_req_verify_result(ft, result, 128) == FW_R_OK, "result bytes verify");
    result[3] ^= 1;
    CHECK(fw_req_verify_result(ft, result, 128) == FW_R_RESULT_DIGEST, "NEG wrong result bytes refused");

    /* NEG duplicate terminal */
    size_t last_off = 0, off = 0;
    while (off + 2 <= rn) { last_off = off; off += 2u + get16(replies + off); }
    PstMsg dup;
    CHECK(pst_decode(replies + last_off + 2, get16(replies + last_off), &dup) == PST_OK, "dup decodes");
    CHECK(fw_req_receive(&r, &dup, &peer, 0) == FW_R_ORDINAL, "NEG replayed completed refused (ordinal)");
    dup.ordinal = 3;
    CHECK(fw_req_receive(&r, &dup, &peer, 0) == FW_R_TERMINAL, "NEG second terminal refused");
    CHECK(ft->state == FW_ST_COMPLETED && ft->next_ordinal == 3, "state unchanged");

    /* NEG reordered ordinal + receipt from the wrong peer, on a second task */
    PstMsg q2 = make_request("task-2", "idem-2", &t, pl, &pn);
    CHECK(fw_req_send(&r, q2.task, q2.idem, in_digest, 5000000) == FW_R_OK, "send 2");
    CHECK(pst_encode(&q2, pl, pn, msg, sizeof msg, &mn) == PST_OK && pst_decode(msg, mn, &qd) == PST_OK, "req 2");
    rn = exec_handle(&e, &qd, replies, sizeof replies);
    uint16_t l0 = get16(replies);
    PstMsg m1, m2;
    CHECK(pst_decode(replies + 2 + l0 + 2, get16(replies + 2 + l0), &m1) == PST_OK, "running decodes");
    CHECK(fw_req_receive(&r, &m1, &peer, 0) == FW_R_ORDINAL, "NEG ordinal 1 before 0 refused");
    off = 2u + l0 + 2u + get16(replies + 2 + l0);
    CHECK(pst_decode(replies + off + 2, get16(replies + off), &m2) == PST_OK, "completed decodes");
    PstMsg m0;
    CHECK(pst_decode(replies + 2, l0, &m0) == PST_OK && fw_req_receive(&r, &m0, &peer, 0) == FW_R_OK, "accepted ok");
    CHECK(fw_req_receive(&r, &m1, &peer, 0) == FW_R_OK, "running ok in order");
    CHECK(fw_req_receive(&r, &m2, &stranger, 0) == FW_R_RECEIPT_PEER, "NEG receipt executor != session peer");
    CHECK(fw_req_find(&r, q2.task)->state == FW_ST_RUNNING, "still RUNNING after refusal");
    /* NEG receipt result digest differs from the completed digest */
    uint8_t tampered[PST_MAX_MSG];
    memcpy(tampered, replies + off + 2, get16(replies + off));
    tampered[PST_HDR + 32 + 132] ^= 1;                       /* HTR1.result_digest inside the payload */
    sha256_hash(tampered + PST_HDR, m2.payload_len, tampered + 192);  /* keep PST1 digest valid */
    PstMsg m2t;
    CHECK(pst_decode(tampered, get16(replies + off), &m2t) == PST_OK, "tampered decodes as PST1");
    CHECK(fw_req_receive(&r, &m2t, &peer, 0) == FW_R_RESULT_DIGEST, "NEG receipt digest != completed digest");
    CHECK(fw_req_receive(&r, &m2, &peer, 0) == FW_R_OK && fw_req_find(&r, q2.task)->state == FW_ST_COMPLETED, "genuine completed ok");

    /* NEG unknown task, wrong side at the requester */
    PstMsg stray = m0;
    fill(stray.task, 16, "nobody");
    CHECK(fw_req_receive(&r, &stray, &peer, 0) == FW_R_UNKNOWN_TASK, "NEG unknown task");
    PstMsg ws = m0; ws.type = PST_T_CANCEL_REQUEST;
    CHECK(fw_req_receive(&r, &ws, &peer, 0) == FW_R_WRONG_SIDE, "NEG requester-side type refused");

    /* NEG oversized frame */
    uint8_t big[PST_MAX_MSG + 1];
    memset(big, 0, sizeof big);
    PstMsg bm;
    CHECK(pst_decode(big, sizeof big, &bm) == PST_BAD_LENGTH, "NEG 1025-byte frame refused");
    CHECK(pst_encode(&q, pl, PST_MAX_PAY + 1, big, sizeof big, &mn) == PST_BAD_LENGTH, "NEG 801-byte payload refused at encode");

    /* NEG unknown kind at the executor -> failed code 2 */
    HtkTask bad = t;
    uint8_t rawpl[PST_MAX_PAY];
    size_t rawn;
    CHECK(htk_encode(&bad, rawpl, sizeof rawpl, &rawn) == HTK_OK, "raw");
    rawpl[5] = 7;
    PstMsg q3 = make_request("task-3", "idem-3", &t, pl, &pn);
    CHECK(pst_encode(&q3, rawpl, rawn, msg, sizeof msg, &mn) == PST_OK && pst_decode(msg, mn, &qd) == PST_OK, "req 3");
    CHECK(fw_req_send(&r, q3.task, q3.idem, in_digest, 5000000) == FW_R_OK, "send 3");
    rn = exec_handle(&e, &qd, replies, sizeof replies);
    CHECK(feed(&r, replies, rn, &peer, NULL) == FW_R_OK && fw_req_find(&r, q3.task)->state == FW_ST_FAILED &&
          fw_req_find(&r, q3.task)->fail_code == FW_FAIL_UNKNOWN_KIND, "NEG unknown kind -> failed 2");
    /* kind 2 is well-formed but this executor does not serve it -> also code 2 */
    HtkTask tk; sample_tok(&tk);
    PstMsg q3b = make_request("task-3b", "idem-3b", &tk, pl, &pn);
    CHECK(pst_encode(&q3b, pl, pn, msg, sizeof msg, &mn) == PST_OK && pst_decode(msg, mn, &qd) == PST_OK, "req 3b");
    CHECK(fw_req_send(&r, q3b.task, q3b.idem, in_digest, 5000000) == FW_R_OK, "send 3b");
    rn = exec_handle(&e, &qd, replies, sizeof replies);
    CHECK(feed(&r, replies, rn, &peer, NULL) == FW_R_OK && fw_req_find(&r, q3b.task)->fail_code == FW_FAIL_UNKNOWN_KIND, "NEG unserved kind -> failed 2");

    /* NEG bounds exceeded -> failed code 1 (threads above the executor's cpus; result larger than max_out) */
    HtkTask tb = t; tb.threads = 64;
    PstMsg q4 = make_request("task-4", "idem-4", &tb, pl, &pn);
    CHECK(pst_encode(&q4, pl, pn, msg, sizeof msg, &mn) == PST_OK && pst_decode(msg, mn, &qd) == PST_OK, "req 4");
    CHECK(fw_req_send(&r, q4.task, q4.idem, in_digest, 5000000) == FW_R_OK, "send 4");
    rn = exec_handle(&e, &qd, replies, sizeof replies);
    CHECK(feed(&r, replies, rn, &peer, NULL) == FW_R_OK && fw_req_find(&r, q4.task)->fail_code == FW_FAIL_BOUNDS, "NEG threads > cpus -> failed 1");
    HtkTask tc = t; tc.max_out_bytes = 64;    /* n=4 needs 128 */
    PstMsg q4b = make_request("task-4b", "idem-4b", &tc, pl, &pn);
    CHECK(pst_encode(&q4b, pl, pn, msg, sizeof msg, &mn) == PST_OK && pst_decode(msg, mn, &qd) == PST_OK, "req 4b");
    CHECK(fw_req_send(&r, q4b.task, q4b.idem, in_digest, 5000000) == FW_R_OK, "send 4b");
    rn = exec_handle(&e, &qd, replies, sizeof replies);
    CHECK(feed(&r, replies, rn, &peer, NULL) == FW_R_OK && fw_req_find(&r, q4b.task)->fail_code == FW_FAIL_BOUNDS, "NEG max_out too small -> failed 1");

    /* idempotence: same bytes replayed from the journal (no re-run), different bytes -> code 7 */
    uint64_t admitted = e.x.admitted;
    CHECK(pst_encode(&q, pl, pn, msg, sizeof msg, &mn) == PST_OK, "re-encode task-1");
    sample_matmul(&t);
    CHECK(htk_encode(&t, pl, PST_MAX_PAY, &pn) == HTK_OK && pst_encode(&q, pl, pn, msg, sizeof msg, &mn) == PST_OK &&
          pst_decode(msg, mn, &qd) == PST_OK, "task-1 again");
    rn = exec_handle(&e, &qd, replies, sizeof replies);
    PstMsg rp;
    CHECK(rn > 2 && pst_decode(replies + 2, get16(replies), &rp) == PST_OK && rp.type == PST_T_COMPLETED &&
          rp.payload_len == 32 + HTR_BYTES && memcmp(rp.payload, want, 32) == 0, "replay returns the journaled completed");
    CHECK(e.x.admitted == admitted && e.x.replayed == 1, "no re-run on replay");
    HtkTask td = t; td.matmul.seed = 2;
    CHECK(htk_encode(&td, pl, PST_MAX_PAY, &pn) == HTK_OK && pst_encode(&q, pl, pn, msg, sizeof msg, &mn) == PST_OK &&
          pst_decode(msg, mn, &qd) == PST_OK, "task-1 other bytes");
    rn = exec_handle(&e, &qd, replies, sizeof replies);
    CHECK(rn > 2 && pst_decode(replies + 2, get16(replies), &rp) == PST_OK && rp.type == PST_T_FAILED &&
          get16(rp.payload) == FW_FAIL_DUPLICATE_DIFFERENT, "NEG repeat with different bytes -> failed 7");

    /* NEG journal store refuses: nothing is sent */
    HtkTask te = t; te.matmul.seed = 99;
    PstMsg q5 = make_request("task-5", "idem-5", &te, pl, &pn);
    CHECK(pst_encode(&q5, pl, pn, msg, sizeof msg, &mn) == PST_OK && pst_decode(msg, mn, &qd) == PST_OK, "req 5");
    e.store_ctx.fail_next = 1;
    rn = exec_handle(&e, &qd, replies, sizeof replies);
    cnt = 0; off = 0;
    while (off + 2 <= rn) { cnt++; off += 2u + get16(replies + off); }
    CHECK(cnt == 2 && e.x.refused[FW_X_STORE] == 1 && fw_exec_lookup(&e.x, q5.task, q5.idem) == NULL,
          "NEG store failure: accepted+running sent, no terminal, nothing journaled (%d msgs)", cnt);

    /* NEG session drop before terminal -> UNCERTAIN; resume_snapshot resolves it */
    HtkTask tf = t; tf.matmul.seed = 3;
    uint8_t in6[32];
    htk_input_digest(&tf, in6);
    PstMsg q6 = make_request("task-6", "idem-6", &tf, pl, &pn);
    CHECK(fw_req_send(&r, q6.task, q6.idem, in6, 5000000) == FW_R_OK, "send 6");
    CHECK(pst_encode(&q6, pl, pn, msg, sizeof msg, &mn) == PST_OK && pst_decode(msg, mn, &qd) == PST_OK, "req 6");
    rn = exec_handle(&e, &qd, replies, sizeof replies);          /* executor finished, replies "lost" */
    CHECK(fw_req_session_lost(&r) == 1 && fw_req_find(&r, q6.task)->state == FW_ST_UNCERTAIN, "NEG session lost -> UNCERTAIN");
    PstMsg lost;
    CHECK(pst_decode(replies + 2, get16(replies), &lost) == PST_OK && fw_req_receive(&r, &lost, &peer, 0) == FW_R_STATE,
          "late accepted refused in UNCERTAIN");
    PstMsg rr = q6; rr.type = PST_T_RESUME_REQUEST; rr.ordinal = 1;
    CHECK(pst_encode(&rr, NULL, 0, msg, sizeof msg, &mn) == PST_OK && pst_decode(msg, mn, &qd) == PST_OK, "resume_request");
    rn = exec_handle(&e, &qd, replies, sizeof replies);
    PstMsg snap;
    CHECK(pst_decode(replies + 2, get16(replies), &snap) == PST_OK && snap.type == PST_T_RESUME_SNAPSHOT, "snapshot");
    /* the executor's ordinals for task-6 continued at 3 (accepted, running, completed were sent); fast-forward the requester */
    fw_req_find(&r, q6.task)->next_ordinal = snap.ordinal;
    CHECK(fw_req_receive(&r, &snap, &peer, 0) == FW_R_OK && fw_req_find(&r, q6.task)->state == FW_ST_COMPLETED &&
          fw_req_find(&r, q6.task)->has_receipt, "resume_snapshot -> COMPLETED with receipt");
    /* unknown task in a snapshot stays UNCERTAIN with the flag */
    PstMsg q7 = make_request("task-7", "idem-7", &tf, pl, &pn);
    CHECK(fw_req_send(&r, q7.task, q7.idem, in6, 5000000) == FW_R_OK && fw_req_session_lost(&r) == 1, "task-7 uncertain");
    PstMsg rr7 = q7; rr7.type = PST_T_RESUME_REQUEST;
    CHECK(pst_encode(&rr7, NULL, 0, msg, sizeof msg, &mn) == PST_OK && pst_decode(msg, mn, &qd) == PST_OK, "resume 7");
    rn = exec_handle(&e, &qd, replies, sizeof replies);
    CHECK(pst_decode(replies + 2, get16(replies), &snap) == PST_OK && snap.payload_len == 1 && snap.payload[0] == 0, "unknown snapshot");
    CHECK(fw_req_receive(&r, &snap, &peer, 0) == FW_R_OK && fw_req_find(&r, q7.task)->state == FW_ST_UNCERTAIN &&
          fw_req_find(&r, q7.task)->resume_unknown == 1, "unknown task stays UNCERTAIN, flagged");

    /* NEG deadline passed -> UNCERTAIN */
    PstMsg q8 = make_request("task-8", "idem-8", &t, pl, &pn);
    CHECK(fw_req_send(&r, q8.task, q8.idem, in_digest, 2000) == FW_R_OK, "send 8");
    CHECK(fw_req_tick(&r, 1999) == 0 && fw_req_tick(&r, 2001) == 1 && fw_req_find(&r, q8.task)->state == FW_ST_UNCERTAIN,
          "NEG deadline passed -> UNCERTAIN");

    /* journal entries survive a reload (encode/decode) */
    uint8_t rec[FW_JOURNAL_ENTRY_BYTES];
    const FwJournalEntry *je = fw_exec_lookup(&e.x, q.task, q.idem);
    FwJournalEntry back;
    CHECK(je && fw_journal_encode(je, rec) == 0 && fw_journal_decode(rec, sizeof rec, &back) == 0 &&
          memcmp(&back.result_digest, je->result_digest, 32) == 0 && back.has_receipt &&
          aien_mid_equal(&back.receipt.executor, &e.self), "journal entry roundtrip");
    FwExecutor x2;
    CHECK(fw_exec_init(&x2, &e.store) == FW_X_OK && fw_exec_load(&x2, rec, sizeof rec) == FW_X_OK &&
          fw_exec_lookup(&x2, q.task, q.idem) != NULL, "journal reload");
    rec[64] = 5;
    CHECK(fw_exec_load(&x2, rec, sizeof rec) == FW_X_STORE, "NEG corrupt journal entry refused");
    printf("state machines: happy path, idempotence, UNCERTAIN/resume and %d-check negative set\n", checks);
}

/* ---- 5. Ed25519 Fabric membership over the loopback ---- */
#define LEASE 1000000ull
typedef struct {
    uint8_t sk[4][32];
    FabEdAuth auth[4];
    AienMachineId roster_ids[3];
    FabRoster roster;
    FabLoop loop;
    AienMachineId slots[4][8];
    AienMachineIndex index[4];
    CqCatalog cat[4];
    FabNode node[4];
} EdHouse;

static void ed_house_init(EdHouse *h) {
    memset(h, 0, sizeof *h);
    const char *labels[4] = { "het-seed-A", "het-seed-B", "het-seed-C", "het-seed-D" };
    for (int i = 0; i < 4; i++) {
        fill(h->sk[i], 32, labels[i]);
        CHECK(fab_ed_auth_init(&h->auth[i], h->sk[i]) == FAB_OK, "ed init");
    }
    /* A, B, C enrol each other; D is nobody. */
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            if (i != j) CHECK(fab_ed_auth_add_peer(&h->auth[i], h->auth[j].pk) == FAB_OK, "add peer");
    CHECK(fab_ed_auth_add_peer(&h->auth[0], h->auth[1].pk) == FAB_E_ARG, "duplicate peer refused");
    CHECK(fab_ed_auth_add_peer(&h->auth[0], h->auth[0].pk) == FAB_E_ARG, "own key as peer refused");
    uint32_t n = 0;
    CHECK(fab_ed_auth_roster(&h->auth[0], h->roster_ids, 3, &n) == FAB_OK && n == 3, "roster");
    h->roster.n = 3;
    h->roster.ids = h->roster_ids;
    fab_loop_init(&h->loop);
    for (int i = 0; i < 4; i++) CHECK(fab_loop_add(&h->loop, &h->auth[i].self) == FAB_OK, "loop add");
    for (int i = 0; i < 4; i++) {
        aien_mid_index_init(&h->index[i], h->slots[i], 8);
        CqCatalog *c = &h->cat[i];
        CHECK(cq_catalog_init_canonical(c, &h->index[i], &h->auth[i].self, 8, 16) == CQ_OK, "catalog");
        uint32_t dom = CQ_SRC(CQ_SRC_GRAPH) | CQ_SRC(CQ_SRC_FABRIC);
        CHECK(cq_op_define(c, 1, 0, dom) == CQ_OK, "op");
        FabConfig cfg = { h->auth[i].self, &h->roster, c, &h->loop.transport, &h->auth[i].auth, LEASE, 1, NULL, 0 };
        CHECK(fab_node_init(&h->node[i], &cfg) == FAB_OK, "node init %d", i);
    }
}
static void drain(FabNode *n, uint64_t now) { FabVerdict v; while (fab_poll(n, now, &v) > 0) {} }
/* The newest message waiting in `to`'s loopback box (broadcast seals one copy per
 * destination, so loop.last is the copy to the last roster peer, not to A). */
static size_t last_to(const FabLoop *l, const AienMachineId *to, uint8_t *out) {
    for (uint32_t i = 0; i < l->n; i++) {
        const FabLoopBox *b = &l->box[i];
        if (!aien_mid_equal(&b->id, to) || b->count == 0) continue;
        const FabLoopMsg *m = &b->q[(b->head + b->count - 1u) % FAB_LOOP_DEPTH];
        memcpy(out, m->b, m->len);
        return m->len;
    }
    return 0;
}

static void test_ed25519_fabric(void) {
    static EdHouse h;
    ed_house_init(&h);
    uint64_t now = 10;
    FabVerdict v;
    uint8_t join1[FAB_MSG_MAX], join2[FAB_MSG_MAX];
    size_t n1, n2;

    const AienMachineId *A = &h.auth[0].self;
    /* B joins at generation 1: keep A's copy of the bytes before it is applied */
    CHECK(fab_join(&h.node[1], LEASE, now) == FAB_OK, "B join gen 1");
    n1 = last_to(&h.loop, A, join1);
    CHECK(n1 == FAB_HDR_BYTES + FAB_JOIN_BODY + FAB_SIG_BYTES, "JOIN to A is %zu bytes", n1);
    drain(&h.node[0], now); drain(&h.node[2], now);              /* A, C see the gen-1 JOIN now */
    CHECK(fab_member_live(&h.node[0], &h.auth[1].self, now), "A holds B live after signed JOIN");
    /* B restarts at generation 2 */
    CHECK(fab_node_set_generation(&h.node[1], 2) == FAB_OK && fab_join(&h.node[1], LEASE, now) == FAB_OK, "B join gen 2");
    n2 = last_to(&h.loop, A, join2);
    drain(&h.node[0], now); drain(&h.node[2], now);
    CHECK(fab_receive(&h.node[0], join2, n2, now, &v) < 0 && v.code == FAB_E_REPLAY,
          "NEG replay of the gen-2 JOIN refused: %s", fab_strerror(v.code));
    CHECK(fab_receive(&h.node[0], join1, n1, now, &v) < 0 && v.code == FAB_E_STALE_GEN,
          "NEG stale generation refused: %s", fab_strerror(v.code));

    /* NEG forged: A's copy of a genuine B message with a fresh timestamp, signed by D's key */
    uint8_t forged[FAB_MSG_MAX];
    size_t nf = n2;
    memcpy(forged, join2, nf);
    forged[FAB_HDR_BYTES - 8] ^= 1;                               /* sent_us differs: not a byte replay */
    aienos_ed25519_sign(forged + nf - 64, forged, nf - 64, h.sk[3]);
    int rc = fab_receive(&h.node[0], forged, nf, now, &v);
    CHECK(rc < 0 && v.code == FAB_E_AUTH, "NEG wrong key refused: %s", fab_strerror(v.code));
    /* altered bytes under the genuine signature are refused the same way */
    memcpy(forged, join2, nf);
    forged[FAB_HDR_BYTES + 3] ^= 1;
    rc = fab_receive(&h.node[0], forged, nf, now, &v);
    CHECK(rc < 0 && v.code == FAB_E_AUTH, "NEG altered body refused: %s", fab_strerror(v.code));

    /* NEG unknown machine: D (not on A's roster) joins */
    CHECK(fab_join(&h.node[3], LEASE, now) == FAB_OK, "D join sends");
    uint8_t joind[FAB_MSG_MAX];
    size_t nd = last_to(&h.loop, A, joind);
    rc = fab_receive(&h.node[0], joind, nd, now, &v);
    CHECK(rc < 0 && v.code == FAB_E_NOT_ENROLLED, "NEG unknown machine refused: %s", fab_strerror(v.code));
    drain(&h.node[0], now); drain(&h.node[1], now); drain(&h.node[2], now);
    CHECK(!fab_member_live(&h.node[0], &h.auth[3].self, now), "D never live at A");

    /* positive: C joins and advertises, A sees it live; lease ends -> not live */
    CHECK(fab_join(&h.node[2], LEASE, now) == FAB_OK, "C join");
    drain(&h.node[0], now);
    CHECK(fab_member_live(&h.node[0], &h.auth[2].self, now) && fab_member_live(&h.node[0], &h.auth[1].self, now), "B and C live at A");
    (void)fab_tick(&h.node[0], now + LEASE + 1);
    CHECK(!fab_member_live(&h.node[0], &h.auth[2].self, now + LEASE + 1), "lease ended -> not live");
    /* a route check cannot name a non-member: fab_route_check with a null router is FAB_RT_ARG */
    AienMachineId tgt;
    CHECK(fab_route_check(&h.node[0], NULL, NULL, now, &tgt, NULL, NULL) == FAB_RT_ARG, "route check arg");
    CHECK(fab_route_pump(&h.node[0], now, NULL, NULL) == 0, "pump empty");
    for (int i = 0; i < 4; i++) fab_ed_auth_wipe(&h.auth[i]);
    uint8_t z[sizeof h.auth[0].sk] = {0};
    CHECK(memcmp(h.auth[0].sk, z, 32) == 0, "seed wiped");
    printf("ed25519 fabric: signed JOIN accepted; forged key, unknown machine, stale generation, replay refused\n");
}

/* ---- two-process mode over pipes ---- */
static int read_frame(FILE *in, uint8_t *buf, size_t cap, size_t *len) {
    uint8_t l[2];
    if (fread(l, 1, 2, in) != 2) return 0;
    size_t n = get16(l);
    if (n > cap) return -1;
    if (fread(buf, 1, n, in) != n) return -1;
    *len = n;
    return 1;
}
static int write_frames(FILE *out, const uint8_t *buf, size_t n) {
    if (fwrite(buf, 1, n, out) != n) return -1;
    return fflush(out);
}
static int run_executor(const char *journal_path) {
    FILE *j = fopen(journal_path, "ab");
    if (!j) { perror(journal_path); return 2; }
    Exec e;
    exec_init(&e, j);
    uint8_t in[PST_MAX_MSG], replies[8 * PST_MAX_MSG];
    size_t n;
    int rc;
    uint64_t frames = 0, refused = 0;
    while ((rc = read_frame(stdin, in, sizeof in, &n)) == 1) {
        frames++;
        PstMsg m;
        int c = pst_decode(in, n, &m);
        if (c != PST_OK) { refused++; fprintf(stderr, "executor: refuse %s\n", pst_code_name(c)); continue; }
        size_t rn = exec_handle(&e, &m, replies, sizeof replies);
        if (write_frames(stdout, replies, rn) != 0) return 3;
    }
    fclose(j);
    fprintf(stderr, "executor: frames=%llu refused=%llu admitted=%llu replayed=%llu journaled=%llu\n",
            (unsigned long long)frames, (unsigned long long)refused, (unsigned long long)e.x.admitted,
            (unsigned long long)e.x.replayed, (unsigned long long)e.x.recorded);
    return rc < 0 ? 4 : 0;
}
static int run_requester(const char *peer_hex) {
    uint8_t rec[AIEN_MID_RECORD_BYTES];
    AienMachineId peer;
    if (unhex(peer_hex, rec, sizeof rec) != 0 || aien_mid_decode(rec, sizeof rec, &peer) != AIEN_MID_OK) {
        fprintf(stderr, "requester: bad peer id\n");
        return 2;
    }
    FwRequester r;
    fw_req_init(&r);
    HtkTask t;
    sample_matmul(&t);
    t.matmul.n = 64; t.matmul.seed = 7; t.max_out_bytes = 64 * 64 * 8;
    uint8_t pl[PST_MAX_PAY], msg[PST_MAX_MSG], frame[2 + PST_MAX_MSG], in[PST_MAX_MSG], want[32], in_digest[32];
    size_t pn, mn, n;
    PstMsg q = make_request("proc-task-1", "proc-idem-1", &t, pl, &pn);
    htk_input_digest(&t, in_digest);
    htk_matmul_result_digest(64, 7, scratch, want);
    if (fw_req_send(&r, q.task, q.idem, in_digest, 60000000) != FW_R_OK) return 3;
    if (pst_encode(&q, pl, pn, msg, sizeof msg, &mn) != PST_OK) return 3;
    put16(frame, (uint16_t)mn); memcpy(frame + 2, msg, mn);
    if (write_frames(stdout, frame, 2 + mn) != 0) return 3;
    int got = 0;
    FwTask *ft = fw_req_find(&r, q.task);
    while (!fw_state_terminal(ft->state) && read_frame(stdin, in, sizeof in, &n) == 1) {
        PstMsg m;
        if (pst_decode(in, n, &m) != PST_OK) { fprintf(stderr, "requester: undecodable reply\n"); return 4; }
        int c = fw_req_receive(&r, &m, &peer, 0);
        if (c != FW_R_OK) { fprintf(stderr, "requester: refused %s\n", fw_req_code_name(c)); return 4; }
        got++;
    }
    if (ft->state != FW_ST_COMPLETED || !ft->has_receipt) { fprintf(stderr, "requester: not completed (%s)\n", fw_state_name(ft->state)); return 5; }
    if (memcmp(ft->result_digest, want, 32) != 0) { fprintf(stderr, "requester: result digest is not the oracle's\n"); return 6; }
    /* replay the same request: the executor must answer from its journal with the same terminal */
    if (write_frames(stdout, frame, 2 + mn) != 0) return 3;
    if (read_frame(stdin, in, sizeof in, &n) != 1) return 7;
    PstMsg rp;
    if (pst_decode(in, n, &rp) != PST_OK || rp.type != PST_T_COMPLETED || memcmp(rp.payload, want, 32) != 0) {
        fprintf(stderr, "requester: replay did not return the journaled completed\n"); return 7;
    }
    /* same key, other bytes: failed 7 */
    t.matmul.seed = 8;
    if (htk_encode(&t, pl, sizeof pl, &pn) != HTK_OK || pst_encode(&q, pl, pn, msg, sizeof msg, &mn) != PST_OK) return 3;
    put16(frame, (uint16_t)mn); memcpy(frame + 2, msg, mn);
    if (write_frames(stdout, frame, 2 + mn) != 0) return 3;
    if (read_frame(stdin, in, sizeof in, &n) != 1) return 8;
    if (pst_decode(in, n, &rp) != PST_OK || rp.type != PST_T_FAILED || get16(rp.payload) != FW_FAIL_DUPLICATE_DIFFERENT) {
        fprintf(stderr, "requester: different bytes under the same key were not refused with code 7\n"); return 8;
    }
    char h[65];
    hex(ft->receipt_reference, 32, h);
    fprintf(stderr, "requester: COMPLETED after %d messages; receipt_reference=%s os_arch=%.16s identity_class=%.4s\n",
           got, h, ft->receipt.os_arch, ft->receipt.identity_class);
    fprintf(stderr, "FAB_WORK_PROCS_REQUESTER_PASS\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 3 && strcmp(argv[1], "--emit") == 0) return emit(argv[2]);
    if (argc >= 2 && strcmp(argv[1], "--oracle") == 0) { print_oracle(); return 0; }
    if (argc >= 3 && strcmp(argv[1], "--executor") == 0) return run_executor(argv[2]);
    if (argc >= 3 && strcmp(argv[1], "--requester") == 0) return run_requester(argv[2]);
    if (argc >= 2 && strcmp(argv[1], "--peer-id") == 0) {
        AienMachineId id = test_machine("het-test-executor");
        uint8_t rec[AIEN_MID_RECORD_BYTES];
        char h[2 * AIEN_MID_RECORD_BYTES + 1];
        aien_mid_encode(&id, rec);
        hex(rec, sizeof rec, h);
        printf("%s\n", h);
        return 0;
    }
    const char *dir = argc >= 2 ? argv[1] : "tests/fabric/vectors/pst";
    test_pst_vectors(dir);
    test_goldens(dir);
    test_oracle();
    test_state_machines();
    test_ed25519_fabric();
    printf("checks=%d failures=%d\n", checks, failures);
    if (failures) return 1;
    printf("FAB_WORK_PASS\n");
    return 0;
}
