/* fab_work.c -- PST1 / HTK1 / HTR1 codec, requester state machine, executor journal (fab_work.h). */
#include "fab_work.h"
#include "sha256.h"

#include <stdio.h>
#include <string.h>

/* ---- byte helpers (big-endian on the wire) ---- */
static uint16_t g16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t g32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint64_t g64(const uint8_t *p) { return ((uint64_t)g32(p) << 32) | g32(p + 4); }
static void p16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void p32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void p64(uint8_t *p, uint64_t v) { p32(p, (uint32_t)(v >> 32)); p32(p + 4, (uint32_t)v); }
static int all_zero(const uint8_t *p, size_t n) { uint8_t a = 0; for (size_t i = 0; i < n; i++) a |= p[i]; return a == 0; }

/* ================= PST1 ================= */
enum {
    O_MAGIC = 0, O_VERSION = 4, O_TYPE = 5, O_RES0 = 6, O_CAUSE = 8, O_ORIGIN = 40,
    O_OBJECTIVE = 72, O_SUBJECT = 104, O_PARENT = 136, O_TASK = 152, O_IDEM = 168,
    O_ORDINAL = 184, O_EFFECT = 188, O_RES1 = 189, O_PLEN = 190, O_DIGEST = 192
};

static const char *const pst_names[PST_CODE_COUNT] = {
    "OK", "PST_BAD_MAGIC", "PST_BAD_VERSION", "PST_DOWNGRADE", "PST_BAD_TYPE",
    "PST_WRONG_SIDE", "PST_BAD_LENGTH", "PST_TRAILING_BYTES", "PST_RESERVED_NONZERO",
    "PST_NO_CAUSE", "PST_NO_TASK", "PST_NO_OBJECTIVE", "PST_DIGEST_MISMATCH",
    "PST_BAD_EFFECT_CLASS", "PST_SUBJECT_MISMATCH"
};

const char *pst_code_name(int code) {
    return code >= 0 && code < PST_CODE_COUNT ? pst_names[code] : "PST_UNKNOWN_CODE";
}

/* Field rules shared by decode (on the wire bytes) and encode (on the struct):
 * steps 3, 4, 8, 9, 10 and 11 of SPEC.md section 4.1. */
static int pst_field_rules(uint8_t version, uint8_t type, uint8_t effect, const uint8_t *cause,
                           const uint8_t *task, const uint8_t *idem, const uint8_t *objective) {
    if (version < PST_VERSION) return PST_DOWNGRADE;
    if (version > PST_VERSION) return PST_BAD_VERSION;
    if (type == 0 || type > PST_MAX_TYPE) return PST_BAD_TYPE;
    if (effect > PST_EFFECT_EFFECT) return PST_BAD_EFFECT_CLASS;
    if (all_zero(cause, 32)) return PST_NO_CAUSE;
    if (all_zero(task, 16)) return PST_NO_TASK;
    if (all_zero(idem, 16)) return PST_NO_TASK;
    if (type == PST_T_REQUEST && all_zero(objective, 32)) return PST_NO_OBJECTIVE;
    return PST_OK;
}

int pst_decode(const uint8_t *b, size_t n, PstMsg *m) {
    if (!b || !m) return PST_BAD_LENGTH;
    memset(m, 0, sizeof *m);
    /* 1. length bounds */
    if (n < PST_HDR) return PST_BAD_LENGTH;
    if (n > PST_MAX_MSG) return PST_BAD_LENGTH;
    /* 2. magic */
    if (memcmp(b + O_MAGIC, "PST1", 4) != 0) return PST_BAD_MAGIC;
    /* 3. version  4. type (field rules continue below after the length checks, as the checker orders them) */
    if (b[O_VERSION] < PST_VERSION) return PST_DOWNGRADE;
    if (b[O_VERSION] > PST_VERSION) return PST_BAD_VERSION;
    if (b[O_TYPE] == 0 || b[O_TYPE] > PST_MAX_TYPE) return PST_BAD_TYPE;
    /* 5. reserved */
    if (!all_zero(b + O_RES0, 2)) return PST_RESERVED_NONZERO;
    if (b[O_RES1] != 0) return PST_RESERVED_NONZERO;
    /* 6, 7. payload length against the bytes present */
    m->payload_len = g16(b + O_PLEN);
    size_t body = n - PST_HDR;
    if (body < m->payload_len) return PST_BAD_LENGTH;
    if (body > m->payload_len) return PST_TRAILING_BYTES;
    /* 8..11. field rules */
    int rc = pst_field_rules(b[O_VERSION], b[O_TYPE], b[O_EFFECT], b + O_CAUSE, b + O_TASK, b + O_IDEM, b + O_OBJECTIVE);
    if (rc != PST_OK) return rc;
    /* 12. payload digest */
    uint8_t d[32];
    sha256_hash(b + PST_HDR, m->payload_len, d);
    if (memcmp(d, b + O_DIGEST, 32) != 0) return PST_DIGEST_MISMATCH;

    m->version = b[O_VERSION];
    m->type = b[O_TYPE];
    memcpy(m->cause, b + O_CAUSE, 32);
    memcpy(m->origin, b + O_ORIGIN, 32);
    memcpy(m->objective, b + O_OBJECTIVE, 32);
    memcpy(m->subject, b + O_SUBJECT, 32);
    memcpy(m->parent, b + O_PARENT, 16);
    memcpy(m->task, b + O_TASK, 16);
    memcpy(m->idem, b + O_IDEM, 16);
    m->ordinal = g32(b + O_ORDINAL);
    m->effect = b[O_EFFECT];
    memcpy(m->digest, d, 32);
    m->payload = b + PST_HDR;
    return PST_OK;
}

int pst_encode(const PstMsg *m, const uint8_t *payload, size_t payload_len,
               uint8_t *out, size_t cap, size_t *out_len) {
    if (!m || !out || !out_len || (!payload && payload_len)) return PST_BAD_LENGTH;
    if (payload_len > PST_MAX_PAY || cap < PST_HDR + payload_len) return PST_BAD_LENGTH;
    int rc = pst_field_rules(m->version, m->type, m->effect, m->cause, m->task, m->idem, m->objective);
    if (rc != PST_OK) return rc;
    memset(out, 0, PST_HDR);
    memcpy(out + O_MAGIC, "PST1", 4);
    out[O_VERSION] = m->version;
    out[O_TYPE] = m->type;
    memcpy(out + O_CAUSE, m->cause, 32);
    memcpy(out + O_ORIGIN, m->origin, 32);
    memcpy(out + O_OBJECTIVE, m->objective, 32);
    memcpy(out + O_SUBJECT, m->subject, 32);
    memcpy(out + O_PARENT, m->parent, 16);
    memcpy(out + O_TASK, m->task, 16);
    memcpy(out + O_IDEM, m->idem, 16);
    p32(out + O_ORDINAL, m->ordinal);
    out[O_EFFECT] = m->effect;
    p16(out + O_PLEN, (uint16_t)payload_len);
    sha256_hash(payload ? payload : out, payload_len, out + O_DIGEST);   /* out: any pointer for len 0 */
    if (payload_len) memcpy(out + PST_HDR, payload, payload_len);
    *out_len = PST_HDR + payload_len;
    return PST_OK;
}

int pst_side_of(uint8_t type) {
    if (type == 0 || type > PST_MAX_TYPE) return 0;
    return (type == PST_T_REQUEST || type == PST_T_CANCEL_REQUEST || type == PST_T_RESUME_REQUEST)
               ? PST_ROLE_REQUESTER : PST_ROLE_EXECUTOR;
}

int pst_side_check(uint8_t type, int sender_role) {
    int s = pst_side_of(type);
    return s && s == sender_role ? PST_OK : PST_WRONG_SIDE;
}

static char *hexw(char *o, const uint8_t *p, size_t n) {
    static const char hx[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) { *o++ = hx[p[i] >> 4]; *o++ = hx[p[i] & 15]; }
    *o = 0;
    return o;
}

void pst_verdict_line(int code, const PstMsg *m, char *buf, size_t cap) {
    char c[65], o[65], ob[65], s[65], pa[33], t[33], k[33], d[65];
    if (!buf || !cap) return;
    if (code != PST_OK || !m) {
        snprintf(buf, cap, "refuse %s", pst_code_name(code));
        return;
    }
    hexw(c, m->cause, 32); hexw(o, m->origin, 32); hexw(ob, m->objective, 32); hexw(s, m->subject, 32);
    hexw(pa, m->parent, 16); hexw(t, m->task, 16); hexw(k, m->idem, 16); hexw(d, m->digest, 32);
    snprintf(buf, cap, "ok type=%u version=%u cause=%s origin=%s objective=%s subject=%s parent=%s task=%s idem=%s ordinal=%lu effect=%u len=%u digest=%s",
             m->type, m->version, c, o, ob, s, pa, t, k, (unsigned long)m->ordinal, m->effect, m->payload_len, d);
}

/* ================= HTK1 ================= */
static const char *const htk_names[HTK_CODE_COUNT] = {
    "HTK_OK", "HTK_BAD_MAGIC", "HTK_BAD_VERSION", "HTK_BAD_KIND", "HTK_RESERVED_NONZERO",
    "HTK_BAD_LENGTH", "HTK_BAD_BOUNDS", "HTK_INPUT_DIGEST_MISMATCH"
};
const char *htk_code_name(int code) { return code >= 0 && code < HTK_CODE_COUNT ? htk_names[code] : "HTK_UNKNOWN_CODE"; }

static size_t htk_param_len(uint8_t kind) {
    if (kind == HTK_KIND_MATMUL_I64_SEEDED) return 12;
    if (kind == HTK_KIND_TOKENIZE_FIXTURE) return 72;
    return 0;
}

size_t htk_input_bytes(const HtkTask *t, uint8_t out[HTK_INPUT_MAX]) {
    if (!t || !out) return 0;
    if (t->kind == HTK_KIND_MATMUL_I64_SEEDED) {
        memcpy(out, "HTK1-MATMUL", 11);
        p32(out + 11, t->matmul.n);
        p64(out + 15, t->matmul.seed);
        return 23;
    }
    if (t->kind == HTK_KIND_TOKENIZE_FIXTURE) {
        memcpy(out, t->tok.fixture_digest, 32);
        p32(out + 32, t->tok.offset);
        p32(out + 36, t->tok.length);
        memcpy(out + 40, t->tok.tokenizer_digest, 32);
        return 72;
    }
    return 0;
}

void htk_input_digest(const HtkTask *t, uint8_t out[32]) {
    uint8_t in[HTK_INPUT_MAX];
    size_t n = htk_input_bytes(t, in);
    sha256_hash(in, n, out);
}

uint64_t htk_result_bytes(const HtkTask *t) {
    if (t && t->kind == HTK_KIND_MATMUL_I64_SEEDED) return (uint64_t)t->matmul.n * t->matmul.n * 8u;
    return 0;
}

static int htk_bounds(const HtkTask *t) {
    if (t->deadline_ms == 0 || t->deadline_ms > HTK_DEADLINE_MS_MAX) return HTK_BAD_BOUNDS;
    if (t->max_out_bytes == 0 || t->max_out_bytes > HTK_OUT_BYTES_MAX) return HTK_BAD_BOUNDS;
    if (t->kind == HTK_KIND_MATMUL_I64_SEEDED) {
        if (t->matmul.n == 0 || t->matmul.n > HTK_MATMUL_N_MAX) return HTK_BAD_BOUNDS;
    } else if (t->kind == HTK_KIND_TOKENIZE_FIXTURE) {
        if (t->tok.length == 0 || t->tok.length > HTK_TOKENIZE_LEN_MAX) return HTK_BAD_BOUNDS;
        if ((uint64_t)t->tok.offset + t->tok.length > UINT32_MAX) return HTK_BAD_BOUNDS;
    } else {
        return HTK_BAD_KIND;
    }
    return HTK_OK;
}

int htk_encode(const HtkTask *t, uint8_t *out, size_t cap, size_t *len) {
    if (!t || !out || !len) return HTK_BAD_LENGTH;
    size_t pl = htk_param_len(t->kind);
    if (!pl) return HTK_BAD_KIND;
    if (cap < HTK_HDR + pl) return HTK_BAD_LENGTH;
    int rc = htk_bounds(t);
    if (rc != HTK_OK) return rc;
    memset(out, 0, HTK_HDR + pl);
    memcpy(out, "HTK1", 4);
    out[4] = 1;
    out[5] = t->kind;
    p32(out + 8, t->deadline_ms);
    p32(out + 12, t->max_out_bytes);
    p64(out + 16, t->max_mem_bytes);
    p32(out + 24, t->max_in_bytes);
    p32(out + 28, t->threads);
    htk_input_digest(t, out + 32);
    if (t->kind == HTK_KIND_MATMUL_I64_SEEDED) {
        p32(out + 64, t->matmul.n);
        p64(out + 68, t->matmul.seed);
    } else {
        memcpy(out + 64, t->tok.fixture_digest, 32);
        p32(out + 96, t->tok.offset);
        p32(out + 100, t->tok.length);
        memcpy(out + 104, t->tok.tokenizer_digest, 32);
    }
    *len = HTK_HDR + pl;
    return HTK_OK;
}

int htk_decode(const uint8_t *b, size_t n, HtkTask *t) {
    if (!b || !t) return HTK_BAD_LENGTH;
    memset(t, 0, sizeof *t);
    if (n < HTK_HDR) return HTK_BAD_LENGTH;
    if (memcmp(b, "HTK1", 4) != 0) return HTK_BAD_MAGIC;
    if (b[4] != 1) return HTK_BAD_VERSION;
    size_t pl = htk_param_len(b[5]);
    if (!pl) return HTK_BAD_KIND;
    if (!all_zero(b + 6, 2)) return HTK_RESERVED_NONZERO;
    if (n != HTK_HDR + pl) return HTK_BAD_LENGTH;
    t->kind = b[5];
    t->deadline_ms = g32(b + 8);
    t->max_out_bytes = g32(b + 12);
    t->max_mem_bytes = g64(b + 16);
    t->max_in_bytes = g32(b + 24);
    t->threads = g32(b + 28);
    memcpy(t->input_digest, b + 32, 32);
    if (t->kind == HTK_KIND_MATMUL_I64_SEEDED) {
        t->matmul.n = g32(b + 64);
        t->matmul.seed = g64(b + 68);
    } else {
        memcpy(t->tok.fixture_digest, b + 64, 32);
        t->tok.offset = g32(b + 96);
        t->tok.length = g32(b + 100);
        memcpy(t->tok.tokenizer_digest, b + 104, 32);
    }
    int rc = htk_bounds(t);
    if (rc != HTK_OK) return rc;
    uint8_t d[32];
    htk_input_digest(t, d);
    if (memcmp(d, t->input_digest, 32) != 0) return HTK_INPUT_DIGEST_MISMATCH;
    return HTK_OK;
}

uint64_t htk_splitmix(uint64_t seed, uint64_t stream, uint64_t index) {
    uint64_t z = seed + (index + 1u) * 0x9E3779B97F4A7C15ull + stream * 0xD1B54A32D192ED03ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

int htk_matmul_i64(uint32_t n, uint64_t seed, int64_t *c) {
    if (!c || n == 0 || n > HTK_MATMUL_N_MAX) return HTK_BAD_BOUNDS;
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t j = 0; j < n; j++) {
            uint64_t acc = 0;                       /* wrapping i64 == wrapping u64 bit-for-bit */
            for (uint32_t k = 0; k < n; k++) {
                uint64_t a = htk_splitmix(seed, 0, (uint64_t)i * n + k);
                uint64_t b = htk_splitmix(seed, 1, (uint64_t)k * n + j);
                acc += a * b;
            }
            c[(size_t)i * n + j] = (int64_t)acc;
        }
    return HTK_OK;
}

void htk_matmul_result_bytes(uint32_t n, const int64_t *c, uint8_t *out) {
    size_t total = (size_t)n * n;
    for (size_t i = 0; i < total; i++) p64(out + 8 * i, (uint64_t)c[i]);
}

int htk_matmul_result_digest(uint32_t n, uint64_t seed, int64_t *scratch, uint8_t out[32]) {
    if (!scratch || !out) return HTK_BAD_BOUNDS;
    int rc = htk_matmul_i64(n, seed, scratch);
    if (rc != HTK_OK) return rc;
    htk_matmul_result_bytes(n, scratch, (uint8_t *)scratch);   /* in place: 8 bytes per value */
    sha256_hash((const uint8_t *)scratch, (size_t)n * n * 8u, out);
    return HTK_OK;
}

/* ================= HTR1 ================= */
static const char *const htr_names[HTR_CODE_COUNT] = {
    "HTR_OK", "HTR_BAD_MAGIC", "HTR_BAD_VERSION", "HTR_BAD_LENGTH", "HTR_RESERVED_NONZERO",
    "HTR_BAD_FLAGS", "HTR_BAD_MACHINE", "HTR_BAD_KIND"
};
const char *htr_code_name(int code) { return code >= 0 && code < HTR_CODE_COUNT ? htr_names[code] : "HTR_UNKNOWN_CODE"; }

int htr_encode(const HtrReceipt *r, uint8_t out[HTR_BYTES]) {
    if (!r || !out) return HTR_BAD_LENGTH;
    if (r->version != 1) return HTR_BAD_VERSION;
    if (!htk_param_len(r->kind)) return HTR_BAD_KIND;
    if (r->flags & ~3u) return HTR_BAD_FLAGS;
    memset(out, 0, HTR_BYTES);
    memcpy(out, "HTR1", 4);
    out[4] = r->version;
    out[5] = r->kind;
    p16(out + 6, r->flags);
    memcpy(out + 8, r->run_id, 16);
    if (aien_mid_encode(&r->executor, out + 24) != AIEN_MID_OK) return HTR_BAD_MACHINE;
    memcpy(out + 68, r->implementation_digest, 32);
    memcpy(out + 100, r->input_digest, 32);
    memcpy(out + 132, r->result_digest, 32);
    p64(out + 164, r->t_accept_us);
    p64(out + 172, r->t_start_us);
    p64(out + 180, r->t_end_us);
    p64(out + 188, r->cpu_ns);
    p64(out + 196, r->peak_rss_bytes);
    p16(out + 204, r->threads_used);
    memcpy(out + 208, r->os_arch, HTR_OS_ARCH);
    memcpy(out + 224, r->identity_class, 4);
    return HTR_OK;
}

int htr_decode(const uint8_t *b, size_t n, HtrReceipt *r) {
    if (!b || !r) return HTR_BAD_LENGTH;
    memset(r, 0, sizeof *r);
    if (n != HTR_BYTES) return HTR_BAD_LENGTH;
    if (memcmp(b, "HTR1", 4) != 0) return HTR_BAD_MAGIC;
    if (b[4] != 1) return HTR_BAD_VERSION;
    if (!htk_param_len(b[5])) return HTR_BAD_KIND;
    uint16_t flags = g16(b + 6);
    if (flags & ~3u) return HTR_BAD_FLAGS;
    if (!all_zero(b + 206, 2)) return HTR_RESERVED_NONZERO;
    if (aien_mid_decode(b + 24, AIEN_MID_RECORD_BYTES, &r->executor) != AIEN_MID_OK) return HTR_BAD_MACHINE;
    r->version = b[4];
    r->kind = b[5];
    r->flags = flags;
    memcpy(r->run_id, b + 8, 16);
    memcpy(r->implementation_digest, b + 68, 32);
    memcpy(r->input_digest, b + 100, 32);
    memcpy(r->result_digest, b + 132, 32);
    r->t_accept_us = g64(b + 164);
    r->t_start_us = g64(b + 172);
    r->t_end_us = g64(b + 180);
    r->cpu_ns = g64(b + 188);
    r->peak_rss_bytes = g64(b + 196);
    r->threads_used = g16(b + 204);
    memcpy(r->os_arch, b + 208, HTR_OS_ARCH);
    memcpy(r->identity_class, b + 224, 4);
    return HTR_OK;
}

void htr_reference(const uint8_t *bytes, size_t len, uint8_t out[32]) { sha256_hash(bytes, len, out); }

/* ================= payloads ================= */
int fw_completed_payload(const uint8_t result_digest[32], const HtrReceipt *r,
                         uint8_t *out, size_t cap, size_t *len) {
    if (!result_digest || !out || !len) return -1;
    size_t need = 32u + (r ? HTR_BYTES : 0u);
    if (cap < need) return -1;
    memcpy(out, result_digest, 32);
    if (r && htr_encode(r, out + 32) != HTR_OK) return -1;
    *len = need;
    return 0;
}

int fw_failed_payload(uint16_t code, const char *reason, uint8_t *out, size_t cap, size_t *len) {
    if (!out || !len || cap < 2) return -1;
    size_t rl = reason ? strlen(reason) : 0;
    if (rl > FW_REASON_MAX) rl = FW_REASON_MAX;
    if (cap < 2 + rl) return -1;
    p16(out, code);
    if (rl) memcpy(out + 2, reason, rl);
    *len = 2 + rl;
    return 0;
}

/* ================= requester ================= */
static const char *const req_names[FW_R_N] = {
    "ok", "wrong side", "unknown task", "ordinal", "already terminal", "state", "bad payload",
    "receipt peer", "receipt input", "result digest", "unsupported type", "full"
};
const char *fw_req_code_name(int code) { return code >= 0 && code < FW_R_N ? req_names[code] : "?"; }

int fw_state_terminal(uint32_t s) { return s == FW_ST_COMPLETED || s == FW_ST_FAILED || s == FW_ST_CANCELLED; }

const char *fw_state_name(uint32_t s) {
    switch (s) {
    case FW_ST_SENT: return "SENT";
    case FW_ST_ACCEPTED: return "ACCEPTED";
    case FW_ST_RUNNING: return "RUNNING";
    case FW_ST_COMPLETED: return "COMPLETED";
    case FW_ST_FAILED: return "FAILED";
    case FW_ST_CANCELLED: return "CANCELLED";
    case FW_ST_UNCERTAIN: return "UNCERTAIN";
    default: return "?";
    }
}

void fw_req_init(FwRequester *r) { if (r) memset(r, 0, sizeof *r); }

FwTask *fw_req_find(FwRequester *r, const uint8_t task[16]) {
    if (!r || !task) return NULL;
    for (uint32_t i = 0; i < r->n; i++)
        if (memcmp(r->t[i].task, task, 16) == 0) return &r->t[i];
    return NULL;
}

int fw_req_send(FwRequester *r, const uint8_t task[16], const uint8_t idem[16],
                const uint8_t input_digest[32], uint64_t deadline_us) {
    if (!r || !task || !idem || !input_digest) return FW_R_BAD_PAYLOAD;
    if (fw_req_find(r, task)) return FW_R_STATE;
    if (r->n >= FW_MAX_TASKS) return FW_R_FULL;
    FwTask *t = &r->t[r->n++];
    memset(t, 0, sizeof *t);
    memcpy(t->task, task, 16);
    memcpy(t->idem, idem, 16);
    memcpy(t->input_digest, input_digest, 32);
    t->deadline_us = deadline_us;
    t->state = FW_ST_SENT;
    return FW_R_OK;
}

static int req_refuse(FwRequester *r, int why) {
    r->refused[why]++;
    r->last_refusal = why;
    return why;
}

/* Applies a terminal payload (completed / failed / cancelled bytes) to t. */
static int apply_terminal(FwTask *t, uint8_t type, const uint8_t *p, size_t len, const AienMachineId *peer) {
    if (type == PST_T_COMPLETED) {
        if (len < 32) return FW_R_BAD_PAYLOAD;
        if (len != 32 && len != 32 + HTR_BYTES) return FW_R_BAD_PAYLOAD;
        HtrReceipt rc;
        if (len == 32 + HTR_BYTES) {
            if (htr_decode(p + 32, HTR_BYTES, &rc) != HTR_OK) return FW_R_BAD_PAYLOAD;
            if (!aien_mid_equal(&rc.executor, peer)) return FW_R_RECEIPT_PEER;
            if (memcmp(rc.input_digest, t->input_digest, 32) != 0) return FW_R_RECEIPT_INPUT;
            if (memcmp(rc.result_digest, p, 32) != 0) return FW_R_RESULT_DIGEST;
            t->receipt = rc;
            t->has_receipt = 1;
            htr_reference(p + 32, HTR_BYTES, t->receipt_reference);
            t->has_receipt_reference = 1;
        }
        memcpy(t->result_digest, p, 32);
        t->state = FW_ST_COMPLETED;
        return FW_R_OK;
    }
    if (type == PST_T_FAILED) {
        if (len < 2 || len > 2 + FW_REASON_MAX) return FW_R_BAD_PAYLOAD;
        t->fail_code = g16(p);
        t->state = FW_ST_FAILED;
        return FW_R_OK;
    }
    if (type == PST_T_CANCELLED) {
        if (len != 0) return FW_R_BAD_PAYLOAD;
        t->state = FW_ST_CANCELLED;
        return FW_R_OK;
    }
    return FW_R_BAD_PAYLOAD;
}

int fw_req_receive(FwRequester *r, const PstMsg *m, const AienMachineId *peer, uint64_t now_us) {
    if (!r || !m || !peer) return FW_R_BAD_PAYLOAD;
    if (pst_side_check(m->type, PST_ROLE_EXECUTOR) != PST_OK) return req_refuse(r, FW_R_WRONG_SIDE);
    FwTask *t = fw_req_find(r, m->task);
    if (!t || memcmp(t->idem, m->idem, 16) != 0) return req_refuse(r, FW_R_UNKNOWN_TASK);
    if (m->type == PST_T_TEXT_CHUNK || m->type == PST_T_WAITING_FOR_APPROVAL) return req_refuse(r, FW_R_UNSUPPORTED);
    if (m->ordinal != t->next_ordinal) return req_refuse(r, FW_R_ORDINAL);
    if (fw_state_terminal(t->state) && m->type != PST_T_RECEIPT_REFERENCE) return req_refuse(r, FW_R_TERMINAL);

    /* A copy: nothing in t changes unless the message is accepted. */
    FwTask c = *t;
    int rc = FW_R_OK;
    switch (m->type) {
    case PST_T_ACCEPTED:
        if (c.state != FW_ST_SENT) rc = FW_R_STATE; else c.state = FW_ST_ACCEPTED;
        break;
    case PST_T_QUEUED:
        if (c.state != FW_ST_SENT && c.state != FW_ST_ACCEPTED) rc = FW_R_STATE; else c.state = FW_ST_ACCEPTED;
        break;
    case PST_T_RUNNING:
        if (c.state != FW_ST_SENT && c.state != FW_ST_ACCEPTED) rc = FW_R_STATE; else c.state = FW_ST_RUNNING;
        break;
    case PST_T_COMPLETED:
    case PST_T_FAILED:
    case PST_T_CANCELLED:
        if (c.state == FW_ST_UNCERTAIN) rc = FW_R_STATE;    /* only a resume_snapshot leaves UNCERTAIN */
        else rc = apply_terminal(&c, m->type, m->payload, m->payload_len, peer);
        break;
    case PST_T_UNCERTAIN_AFTER_DISCONNECT:
        if (m->payload_len) rc = FW_R_BAD_PAYLOAD; else c.state = FW_ST_UNCERTAIN;
        break;
    case PST_T_RESUME_SNAPSHOT:
        if (c.state != FW_ST_UNCERTAIN) { rc = FW_R_STATE; break; }
        if (m->payload_len < 1) { rc = FW_R_BAD_PAYLOAD; break; }
        if (m->payload[0] == 0) {
            if (m->payload_len != 1) rc = FW_R_BAD_PAYLOAD; else c.resume_unknown = 1;
        } else {
            rc = apply_terminal(&c, m->payload[0], m->payload + 1, m->payload_len - 1u, peer);
        }
        break;
    case PST_T_RECEIPT_REFERENCE:
        if (m->payload_len != 32) { rc = FW_R_BAD_PAYLOAD; break; }
        if (c.has_receipt_reference && memcmp(c.receipt_reference, m->payload, 32) != 0) { rc = FW_R_RESULT_DIGEST; break; }
        memcpy(c.receipt_reference, m->payload, 32);
        c.has_receipt_reference = 1;
        break;
    default:
        rc = FW_R_UNSUPPORTED;
    }
    if (rc != FW_R_OK) return req_refuse(r, rc);
    c.next_ordinal = m->ordinal + 1u;
    *t = c;
    r->applied++;
    (void)now_us;
    return FW_R_OK;
}

int fw_req_session_lost(FwRequester *r) {
    if (!r) return 0;
    int n = 0;
    for (uint32_t i = 0; i < r->n; i++)
        if (!fw_state_terminal(r->t[i].state) && r->t[i].state != FW_ST_UNCERTAIN) { r->t[i].state = FW_ST_UNCERTAIN; n++; }
    return n;
}

int fw_req_tick(FwRequester *r, uint64_t now_us) {
    if (!r) return 0;
    int n = 0;
    for (uint32_t i = 0; i < r->n; i++) {
        FwTask *t = &r->t[i];
        if (!fw_state_terminal(t->state) && t->state != FW_ST_UNCERTAIN && now_us > t->deadline_us) {
            t->state = FW_ST_UNCERTAIN;
            n++;
        }
    }
    return n;
}

int fw_req_verify_result(const FwTask *t, const uint8_t *result, size_t len) {
    if (!t || (!result && len)) return FW_R_BAD_PAYLOAD;
    if (t->state != FW_ST_COMPLETED) return FW_R_STATE;
    uint8_t d[32];
    sha256_hash(result, len, d);
    return memcmp(d, t->result_digest, 32) == 0 ? FW_R_OK : FW_R_RESULT_DIGEST;
}

/* ================= executor ================= */
static const char *const exec_names[FW_X_N] = {
    "ok", "wrong side", "bad HTK1", "unknown kind", "bounds", "duplicate with different bytes", "journal store"
};
const char *fw_exec_code_name(int code) { return code >= 0 && code < FW_X_N ? exec_names[code] : "?"; }

int fw_exec_init(FwExecutor *x, const FwJournalStore *store) {
    if (!x || !store || !store->append) return FW_X_STORE;
    memset(x, 0, sizeof *x);
    x->store = store;
    return FW_X_OK;
}

const FwJournalEntry *fw_exec_lookup(const FwExecutor *x, const uint8_t task[16], const uint8_t idem[16]) {
    if (!x || !task || !idem) return NULL;
    for (uint32_t i = 0; i < x->count; i++) {
        const FwJournalEntry *e = &x->e[(x->head + FW_JOURNAL_ENTRIES - 1u - i) % FW_JOURNAL_ENTRIES];
        if (memcmp(e->task, task, 16) == 0 && memcmp(e->idem, idem, 16) == 0) return e;
    }
    return NULL;
}

static void ring_put(FwExecutor *x, const FwJournalEntry *e) {
    x->e[x->head] = *e;
    x->head = (x->head + 1u) % FW_JOURNAL_ENTRIES;
    if (x->count < FW_JOURNAL_ENTRIES) x->count++;
}

int fw_journal_encode(const FwJournalEntry *e, uint8_t out[FW_JOURNAL_ENTRY_BYTES]) {
    if (!e || !out) return -1;
    if (e->terminal_type != PST_T_COMPLETED && e->terminal_type != PST_T_FAILED && e->terminal_type != PST_T_CANCELLED) return -1;
    memset(out, 0, FW_JOURNAL_ENTRY_BYTES);
    memcpy(out, e->task, 16);
    memcpy(out + 16, e->idem, 16);
    memcpy(out + 32, e->htk_digest, 32);
    out[64] = e->terminal_type;
    p16(out + 65, e->fail_code);
    out[67] = e->has_receipt ? 1 : 0;
    memcpy(out + 68, e->result_digest, 32);
    if (e->has_receipt && htr_encode(&e->receipt, out + 100) != HTR_OK) return -1;
    return 0;
}

int fw_journal_decode(const uint8_t *b, size_t n, FwJournalEntry *e) {
    if (!b || !e || n != FW_JOURNAL_ENTRY_BYTES) return -1;
    memset(e, 0, sizeof *e);
    memcpy(e->task, b, 16);
    memcpy(e->idem, b + 16, 16);
    memcpy(e->htk_digest, b + 32, 32);
    e->terminal_type = b[64];
    if (e->terminal_type != PST_T_COMPLETED && e->terminal_type != PST_T_FAILED && e->terminal_type != PST_T_CANCELLED) return -1;
    e->fail_code = g16(b + 65);
    if (b[67] > 1) return -1;
    e->has_receipt = b[67];
    memcpy(e->result_digest, b + 68, 32);
    if (e->has_receipt && htr_decode(b + 100, HTR_BYTES, &e->receipt) != HTR_OK) return -1;
    return 0;
}

int fw_exec_load(FwExecutor *x, const uint8_t *rec, size_t len) {
    if (!x) return FW_X_STORE;
    FwJournalEntry e;
    if (fw_journal_decode(rec, len, &e) != 0) return FW_X_STORE;
    ring_put(x, &e);
    return FW_X_OK;
}

static int exec_refuse(FwExecutor *x, int why, uint16_t code, uint16_t *fail_code) {
    x->refused[why]++;
    if (fail_code) *fail_code = code;
    return FW_ADMIT_REFUSE;
}

int fw_exec_admit(FwExecutor *x, const PstMsg *m, const FwExecLimits *lim, HtkTask *task,
                  const FwJournalEntry **replay, uint16_t *fail_code) {
    if (!x || !m || !lim || !task || !replay || !fail_code) return FW_ADMIT_REFUSE;
    *replay = NULL;
    *fail_code = 0;
    if (m->type != PST_T_REQUEST) return exec_refuse(x, FW_X_WRONG_SIDE, 0, fail_code);
    /* Idempotence first: a repeat is answered from the journal whatever the limits are now. */
    const FwJournalEntry *j = fw_exec_lookup(x, m->task, m->idem);
    if (j) {
        if (memcmp(j->htk_digest, m->digest, 32) == 0) { *replay = j; x->replayed++; return FW_ADMIT_REPLAY; }
        return exec_refuse(x, FW_X_DUPLICATE_DIFFERENT, FW_FAIL_DUPLICATE_DIFFERENT, fail_code);
    }
    int rc = htk_decode(m->payload, m->payload_len, task);
    if (rc == HTK_BAD_KIND) return exec_refuse(x, FW_X_KIND, FW_FAIL_UNKNOWN_KIND, fail_code);
    if (rc == HTK_BAD_BOUNDS) return exec_refuse(x, FW_X_BOUNDS, FW_FAIL_BOUNDS, fail_code);
    if (rc != HTK_OK) return exec_refuse(x, FW_X_BAD_HTK, FW_FAIL_INTERNAL, fail_code);
    if (m->effect != PST_EFFECT_COMPUTE) return exec_refuse(x, FW_X_BAD_HTK, FW_FAIL_INTERNAL, fail_code);
    if (!(lim->kinds & (1u << task->kind))) return exec_refuse(x, FW_X_KIND, FW_FAIL_UNKNOWN_KIND, fail_code);
    uint32_t out_cap = lim->max_out_bytes_cap ? lim->max_out_bytes_cap : HTK_OUT_BYTES_MAX;
    uint32_t dl_cap = lim->deadline_ms_cap ? lim->deadline_ms_cap : HTK_DEADLINE_MS_MAX;
    if (task->threads > lim->cpus) return exec_refuse(x, FW_X_BOUNDS, FW_FAIL_BOUNDS, fail_code);
    if (lim->mem_bytes && task->max_mem_bytes > lim->mem_bytes) return exec_refuse(x, FW_X_BOUNDS, FW_FAIL_BOUNDS, fail_code);
    if (task->max_out_bytes > out_cap || task->deadline_ms > dl_cap) return exec_refuse(x, FW_X_BOUNDS, FW_FAIL_BOUNDS, fail_code);
    uint64_t need = htk_result_bytes(task);
    if (need && need > task->max_out_bytes) return exec_refuse(x, FW_X_BOUNDS, FW_FAIL_BOUNDS, fail_code);
    x->admitted++;
    return FW_ADMIT_RUN;
}

int fw_exec_record(FwExecutor *x, const FwJournalEntry *e) {
    if (!x || !e || !x->store) return FW_X_STORE;
    uint8_t rec[FW_JOURNAL_ENTRY_BYTES];
    if (fw_journal_encode(e, rec) != 0) return FW_X_STORE;
    if (x->store->append(x->store->ctx, rec, sizeof rec) != 0) { x->refused[FW_X_STORE]++; return FW_X_STORE; }
    ring_put(x, e);
    x->recorded++;
    return FW_X_OK;
}

int fw_terminal_payload(const FwJournalEntry *e, const char *reason, uint8_t *out, size_t cap, size_t *len) {
    if (!e || !out || !len) return -1;
    switch (e->terminal_type) {
    case PST_T_COMPLETED: return fw_completed_payload(e->result_digest, e->has_receipt ? &e->receipt : NULL, out, cap, len);
    case PST_T_FAILED: return fw_failed_payload(e->fail_code, reason, out, cap, len);
    case PST_T_CANCELLED: *len = 0; return 0;
    default: return -1;
    }
}

int fw_resume_snapshot_payload(const FwJournalEntry *e, uint8_t *out, size_t cap, size_t *len) {
    if (!out || !len || cap < 1) return -1;
    if (!e) { out[0] = 0; *len = 1; return 0; }
    out[0] = e->terminal_type;
    size_t inner = 0;
    if (fw_terminal_payload(e, NULL, out + 1, cap - 1, &inner) != 0) return -1;
    *len = 1 + inner;
    return 0;
}
