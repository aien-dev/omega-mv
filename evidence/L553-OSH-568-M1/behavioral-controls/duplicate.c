/*
 * osh_diff.c -- cross-adapter pipeline differential runner (omega#568, parent aien-architecture-mv#158).
 * Contract: aien-protocols-mv specs/osh-platform/OSH_PLATFORM_ABI.md v1 DRAFT at fb5fca98e6676bb104888c73268e0f9a81e9c63e
 * (sections 5, 7.1, 7.3, 8, 10, 12); the corpus and the result-file format are tests/osh/diff/README.md.
 *
 * For every fixture in fixtures.txt the runner drives the ACTUAL compiled Omega shell core (the three .osc units embedded
 * in build/osh/osh_units.c, through osh_shell_run_string, the same driver loop as the osh program) on the Linux host
 * adapter (src/osh/host), twice: native AArch64 units and the OSC interpreter. Each run is a forked child in its own
 * process group, in a private directory, with a PATH that holds only the toy operations (osh_toy.c), and with the
 * fixture's injected faults and resource limits. A read-only TEST-ONLY observer (OshDiffHooks, -DOSH_DIFF_TEST_HOOKS)
 * hands out every request record the core produced, the host result for it, and every core refusal.
 *
 * Columns: host-native, host-interp, aienos. A host column is PASS when the run matches every expectation of the fixture
 * and the cleanup checks (no descriptor left open, no process left in the pipeline group); the two host columns must also
 * give the same normalized result line. The aienos column compares a supplied native result file (--native FILE; a raw
 * QEMU serial log is fine, only OSHDIFF lines are read) against the host-native line, field by field. No file, or no
 * line for a fixture, is NOT_RUN, never PASS. A file that is supplied but invalid (bad header, missing end line, wrong
 * pins, adapter not aienos-native) fails every portable fixture. scope linux fixtures are NOT_RUN on aienos by design.
 *
 * Usage: osh_diff [--pin KEY=VALUE]... [--native FILE] [--require-native] [--emit FILE] [--host-results FILE] [--strict-host] TOYBIN FIXTURES
 *   --emit FILE          write the host-native results in the result-file format (adapter=linux-host)
 *   --host-results FILE  compare only: take host results from FILE instead of running; final line is then
 *                        OSH_DIFF_COMPARE_ONLY_PASS/FAIL, never OSH_DIFF_PASS (used by the comparator mutants)
 *   --require-native     an aienos NOT_RUN on a portable fixture is a failure (acceptance mode)
 *   --strict-host        a host NOT_RUN is a failure (the plain build; the ASan build allows the RLIMIT_AS fixture to be NOT_RUN)
 * Final line OSH_DIFF_PASS or OSH_DIFF_FAIL.
 *
 * UNVERIFIED (the ABI draft does not pin these; also listed in README.md):
 *  - wire form (7.3): OUT is serialized as out_used single bytes (one per OUT cell), after the cells.
 *  - the request line grammar extension " direct" after conn= when flags bit 0 is set (vectors/README has no direct form).
 *  - stage categories (README "Stage categories") map host outcomes to portable names; the mapping of SIGPIPE to
 *    broken_pipe, SIGINT to interrupted and SIGXFSZ to limit is this harness's choice.
 *  - a failed input redirection is exit:1 for that stage on every adapter (bash convention, osh_exec.c DECISIONS).
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "osh_host.h"
#include "osh_layout.h"
#include "osh_shell.h"
#include "sha256.h"

#ifndef OSH_DIFF_TEST_HOOKS
#error "osh_diff needs the TEST-ONLY observer: build with -DOSH_DIFF_TEST_HOOKS"
#endif
#if defined(__SANITIZE_ADDRESS__)
#define OSH_DIFF_ASAN 1
#else
#define OSH_DIFF_ASAN 0
#endif

#define MAXFX 128
#define MAXP 16    /* pipelines per fixture (CAP_PIPELINES) */
#define LINE 16384
#define RUN_TIMEOUT_S 60

static const char *const TOYS[] = {"emit", "args", "pass", "trickle", "flood", "bytes", "take", "sink", "status", "oe", "stall"};

/* ---------------- byte strings ---------------- */

typedef struct {
    unsigned char *p;
    size_t n;
} Bytes;

static int hexv(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

/* "..." with \" \\ \xHH (vectors grammar) and the fixture conveniences \n \t. Advances *sp past the closing quote. */
static int q_decode(const char **sp, Bytes *b)
{
    const char *s = *sp;
    if (*s != '"') return -1;
    s++;
    b->p = malloc(strlen(s) + 1);
    b->n = 0;
    for (; *s && *s != '"'; s++) {
        if (*s != '\\') { b->p[b->n++] = (unsigned char)*s; continue; }
        s++;
        if (*s == '"' || *s == '\\') b->p[b->n++] = (unsigned char)*s;
        else if (*s == 'n') b->p[b->n++] = '\n';
        else if (*s == 't') b->p[b->n++] = '\t';
        else if (*s == 'x' && hexv(s[1]) >= 0 && hexv(s[2]) >= 0) { b->p[b->n++] = (unsigned char)(hexv(s[1]) * 16 + hexv(s[2])); s += 2; }
        else { free(b->p); return -1; }
    }
    if (*s != '"') { free(b->p); return -1; }
    *sp = s + 1;
    return 0;
}

/* vectors grammar encoder (same rule as tests/osh/test_osh_expand.c fq, without the repetition shorthand) */
static void q_encode(char *out, size_t *o, size_t cap, const unsigned char *s, size_t n)
{
    if (*o + 4 * n + 3 >= cap) { out[*o] = 0; return; }
    out[(*o)++] = '"';
    for (size_t i = 0; i < n; i++) {
        unsigned char c = s[i];
        if (c == '"' || c == '\\') { out[(*o)++] = '\\'; out[(*o)++] = (char)c; }
        else if (c < 0x20 || c > 0x7e) *o += (size_t)sprintf(out + *o, "\\x%02x", c);
        else out[(*o)++] = (char)c;
    }
    out[(*o)++] = '"';
    out[*o] = 0;
}

static void hex32(const uint8_t d[32], char out[65])
{
    for (int i = 0; i < 32; i++) sprintf(out + 2 * i, "%02x", d[i]);
    out[64] = 0;
}

static void sha_hex(const void *p, size_t n, char out[65])
{
    uint8_t d[32];
    sha256_hash(p, n, d);
    hex32(d, out);
}

/* ---------------- fixtures ---------------- */

/* F_MUTANT_*: break cleanup on purpose so mutants.sh can prove the ABI section 12 checks go red; never in fixtures.txt */
enum { F_NONE = 0, F_REVOKE_SPAWN, F_INTERRUPT, F_LAUNCH_FAIL, F_STOP_FIRST, F_MUTANT_LEAK_FD, F_MUTANT_LEAK_PROCESS };
enum { L_NONE = 0, L_NOFILE_TIGHT, L_NPROC, L_FSIZE, L_AS };

typedef struct {
    char name[64];
    Bytes content;
} FxFile;

typedef struct {
    char id[64];
    int linux_only;
    char covers[256];
    int direct;            /* 0 text, 1 direct argv */
    Bytes text;            /* text mode */
    Bytes dargv[OSH_MAX_CMDS][OSH_MAX_ARGV];
    int dncmds, dnargv[OSH_MAX_CMDS];
    Bytes in;              /* stdin, may be empty */
    int has_in;
    FxFile setup[4];
    int nsetup;
    int fault, fault_arg;
    char fault_name[64];
    int limit;
    unsigned long long limit_arg;
    /* expectations */
    char req[LINE];
    int has_req;
    int refuse_code, refuse_off, has_refuse;
    char refuse_name[32];
    Bytes out;
    int has_out;
    Bytes errhas;
    int has_errhas;
    FxFile outfile[4];
    int noutfile;
    char outlen_name[64];
    long long outlen;
    int status, has_status;
    char stages[LINE], errs[1024];
    int lineno;
} Fixture;

static Fixture FX[MAXFX];
static int NFX;
static char ABI_PIN[256], FIXTURES_SHA[65], CORE_SHA[65];
/* identity the build system passes with --pin KEY=VALUE (mk/osh.mk computes them); "unknown" when not given */
static const char *PIN_OMEGA = "unknown", *PIN_CC = "unknown", *PIN_HARNESS = "unknown", *PIN_AIENOS_LOCK = "unknown";

static int fx_fail(const char *path, int ln, const char *why)
{
    fprintf(stderr, "%s:%d: %s\n", path, ln, why);
    return -1;
}

static char *rest(char *line, const char *key)
{
    size_t k = strlen(key);
    if (strncmp(line, key, k) != 0 || line[k] != ' ') return NULL;
    return line + k + 1;
}

static int load_fixtures(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s: %s\n", path, strerror(errno)); return -1; }
    {
        sha256_ctx c;
        sha256_init(&c);
        unsigned char b[4096];
        size_t r;
        while ((r = fread(b, 1, sizeof b, f)) > 0) sha256_update(&c, b, r);
        uint8_t d[32];
        sha256_final(&c, d);
        hex32(d, FIXTURES_SHA);
        rewind(f);
    }
    static char line[LINE];
    int ln = 0;
    Fixture *x = NULL;
    while (fgets(line, sizeof line, f)) {
        ln++;
        size_t n = strlen(line);
        if (n && line[n - 1] == '\n') line[--n] = 0;
        if (n == 0 || line[0] == '#') continue;
        char *v;
        if ((v = rest(line, "abi"))) { snprintf(ABI_PIN, sizeof ABI_PIN, "%.*s", (int)strcspn(v, " "), v); continue; }
        if ((v = rest(line, "fixture"))) {
            if (x) return fx_fail(path, ln, "fixture without end");
            if (NFX >= MAXFX) return fx_fail(path, ln, "too many fixtures");
            x = &FX[NFX];
            memset(x, 0, sizeof *x);
            x->lineno = ln;
            if (strlen(v) == 0 || strlen(v) >= sizeof x->id || strchr(v, 0x20)) return fx_fail(path, ln, "fixture id must be one word under 64 bytes");
            memcpy(x->id, v, strlen(v) + 1);
            for (int i = 0; i < NFX; i++)
                if (strcmp(FX[i].id, x->id) == 0) return fx_fail(path, ln, "duplicate fixture id");
            continue;
        }
        if (!x) return fx_fail(path, ln, "line outside a fixture");
        if (strcmp(line, "end") == 0) {
            if (!x->direct && !x->text.p) return fx_fail(path, ln, "fixture has no text or direct line");
            if (!x->has_status || !x->has_out) return fx_fail(path, ln, "fixture lacks status or stdout expectation");
            if (!x->has_req && !x->has_refuse) return fx_fail(path, ln, "fixture lacks req or refuse expectation");
            NFX++;
            x = NULL;
            continue;
        }
        const char *s;
        if ((v = rest(line, "scope"))) {
            if (strcmp(v, "portable") == 0) x->linux_only = 0;
            else if (strcmp(v, "linux") == 0) x->linux_only = 1;
            else return fx_fail(path, ln, "scope must be portable or linux");
        } else if ((v = rest(line, "covers"))) snprintf(x->covers, sizeof x->covers, "%s", v);
        else if ((v = rest(line, "text"))) {
            s = v;
            if (q_decode(&s, &x->text) || *s) return fx_fail(path, ln, "bad text string");
        } else if ((v = rest(line, "direct"))) {
            x->direct = 1;
            s = v;
            for (;;) {
                if (x->dncmds >= OSH_MAX_CMDS) return fx_fail(path, ln, "too many direct commands");
                int c = x->dncmds;
                for (;;) {
                    if (x->dnargv[c] >= OSH_MAX_ARGV || q_decode(&s, &x->dargv[c][x->dnargv[c]])) return fx_fail(path, ln, "bad direct argv");
                    if (memchr(x->dargv[c][x->dnargv[c]].p, 0, x->dargv[c][x->dnargv[c]].n)) return fx_fail(path, ln, "NUL in a direct argument (not expressible to the builder)");
                    x->dnargv[c]++;
                    if (*s != ',') break;
                    s++;
                }
                x->dncmds++;
                if (*s == 0) break;
                if (strncmp(s, " | ", 3) != 0) return fx_fail(path, ln, "direct commands are separated by \" | \"");
                s += 3;
            }
        } else if ((v = rest(line, "stdin"))) {
            s = v;
            if (q_decode(&s, &x->in) || *s) return fx_fail(path, ln, "bad stdin string");
            x->has_in = 1;
        } else if ((v = rest(line, "file"))) {
            if (x->nsetup >= 4) return fx_fail(path, ln, "too many files");
            FxFile *ff = &x->setup[x->nsetup++];
            size_t k = strcspn(v, " ");
            if (k == 0 || k >= sizeof ff->name || v[k] != ' ' || memchr(v, '/', k)) return fx_fail(path, ln, "bad file name");
            snprintf(ff->name, sizeof ff->name, "%.*s", (int)k, v);
            s = v + k + 1;
            if (q_decode(&s, &ff->content) || *s) return fx_fail(path, ln, "bad file content");
        } else if ((v = rest(line, "fault"))) {
            if (x->fault) return fx_fail(path, ln, "one fault per fixture");
            if (strncmp(v, "revoke_spawn=", 13) == 0) { x->fault = F_REVOKE_SPAWN; snprintf(x->fault_name, sizeof x->fault_name, "%s", v + 13); }
            else if (strcmp(v, "interrupt_after_launch") == 0) x->fault = F_INTERRUPT;
            else if (strcmp(v, "stop_first_after_launch") == 0) x->fault = F_STOP_FIRST;
            else if (strcmp(v, "mutant_leak_fd") == 0) x->fault = F_MUTANT_LEAK_FD;
            else if (strcmp(v, "mutant_leak_process") == 0) x->fault = F_MUTANT_LEAK_PROCESS;
            else if (strncmp(v, "launch_fail_at=", 15) == 0) {
                x->fault = F_LAUNCH_FAIL;
                x->fault_arg = atoi(v + 15);
                if (x->fault_arg < 1 || x->fault_arg > OSH_MAX_CMDS) return fx_fail(path, ln, "launch_fail_at must be 1..8");
            } else return fx_fail(path, ln, "unknown fault");
        } else if ((v = rest(line, "limit"))) {
            if (x->limit) return fx_fail(path, ln, "one limit per fixture");
            if (strcmp(v, "nofile=tight") == 0) x->limit = L_NOFILE_TIGHT;
            else if (strncmp(v, "nproc=", 6) == 0) { x->limit = L_NPROC; x->limit_arg = strtoull(v + 6, NULL, 10); }
            else if (strncmp(v, "fsize=", 6) == 0) { x->limit = L_FSIZE; x->limit_arg = strtoull(v + 6, NULL, 10); }
            else if (strncmp(v, "as=", 3) == 0) { x->limit = L_AS; x->limit_arg = strtoull(v + 3, NULL, 10); }
            else return fx_fail(path, ln, "unknown limit");
        } else if ((v = rest(line, "req"))) { snprintf(x->req, sizeof x->req, "%s", v); x->has_req = 1; }
        else if ((v = rest(line, "refuse"))) {
            if (sscanf(v, "%d %31s off=%d", &x->refuse_code, x->refuse_name, &x->refuse_off) != 3) return fx_fail(path, ln, "refuse CODE NAME off=N");
            x->has_refuse = 1;
        } else if ((v = rest(line, "stdout"))) {
            s = v;
            if (q_decode(&s, &x->out) || *s) return fx_fail(path, ln, "bad stdout string");
            x->has_out = 1;
        } else if ((v = rest(line, "stderr_has"))) {
            s = v;
            if (q_decode(&s, &x->errhas) || *s || x->errhas.n == 0) return fx_fail(path, ln, "bad stderr_has string");
            x->has_errhas = 1;
        } else if ((v = rest(line, "outfile"))) {
            if (x->noutfile >= 4) return fx_fail(path, ln, "too many outfiles");
            FxFile *ff = &x->outfile[x->noutfile++];
            size_t k = strcspn(v, " ");
            if (k == 0 || k >= sizeof ff->name || v[k] != ' ' || memchr(v, '/', k)) return fx_fail(path, ln, "bad outfile name");
            snprintf(ff->name, sizeof ff->name, "%.*s", (int)k, v);
            s = v + k + 1;
            if (q_decode(&s, &ff->content) || *s) return fx_fail(path, ln, "bad outfile content");
        } else if ((v = rest(line, "outlen"))) {
            if (sscanf(v, "%63s %lld", x->outlen_name, &x->outlen) != 2 || strchr(x->outlen_name, '/')) return fx_fail(path, ln, "outlen NAME N");
        } else if ((v = rest(line, "status"))) { x->status = atoi(v); x->has_status = 1; }
        else if ((v = rest(line, "stages"))) snprintf(x->stages, sizeof x->stages, "%s", v);
        else if ((v = rest(line, "err"))) snprintf(x->errs, sizeof x->errs, "%s", v);
        else return fx_fail(path, ln, "unknown key");
    }
    fclose(f);
    if (x) return fx_fail(path, ln, "last fixture has no end");
    if (!ABI_PIN[0]) return fx_fail(path, ln, "no abi pin line");
    if (NFX == 0) return fx_fail(path, ln, "no fixtures");
    return 0;
}

/* ---------------- observation shared between the run child and the runner ---------------- */

typedef struct {
    char line[2048];     /* request rendered in the vectors grammar, without the "P<i> " prefix */
    char wire[65];       /* sha256 of the 7.3 wire form */
    char stages[512];    /* stage categories, comma separated */
    char err[32];        /* pipeline platform error name */
} ObsPipe;

typedef struct {
    int npipes, overflow, bad_out_cell;
    ObsPipe p[MAXP];
    int refused;
    uint64_t refuse_code, refuse_off;
    int fds_before, fds_after;
    unsigned long calls_native, calls_interp; /* core entry calls by mode: proves which backend ran */
    int setup_failed;
    char setup_why[256];
    int not_run;         /* the fixture cannot run in this build/environment (reason in setup_why) */
} Obs;

static Obs *OBS;

static const char *e_name(int e)
{
    static const char *const n[] = {"OK", "DENIED", "REVOKED", "STALE", "NOT_FOUND", "UNAVAILABLE", "INVALID_ARG", "LIMIT",
                                    "INTERRUPTED", "BROKEN_PIPE", "IO", "OUTCOME_UNKNOWN", "CAP_DOMAIN_MISMATCH",
                                    "CAP_GEN_NARROW", "NOT_SUPPORTED", "WOULD_BLOCK", "PARTIAL_LAUNCH"};
    return e >= 0 && e <= 16 ? n[e] : "IO"; /* unknown codes are IO, never OK (ABI 8.6) */
}

/* README "Stage categories": the portable name of one command's end. */
static void stage_cat(const OshCmdResult *c, char *o, size_t cap)
{
    switch (c->outcome) {
    case OSH_OUT_COMPLETED:
        if (c->termsig == SIGPIPE) snprintf(o, cap, "broken_pipe");
        else if (c->termsig == SIGINT) snprintf(o, cap, "interrupted");
        else if (c->termsig == SIGXFSZ) snprintf(o, cap, "limit");
        else if (c->termsig) snprintf(o, cap, "signal:%d", c->termsig); /* Linux-only */
        else if (c->status == 0) snprintf(o, cap, "ok");
        else snprintf(o, cap, "exit:%d", c->status);
        break;
    case OSH_OUT_CANCELLED: snprintf(o, cap, "cancelled"); break;
    case OSH_OUT_NOT_STARTED: snprintf(o, cap, "not_started"); break;
    case OSH_OUT_FAILED_NO_EFFECT:
        if (c->err == OSH_E_NOT_FOUND) snprintf(o, cap, "not_found");
        else if (c->err == OSH_E_DENIED || c->err == OSH_E_REVOKED || c->err == OSH_E_STALE || c->err == OSH_E_CAP_DOMAIN_MISMATCH ||
                 c->err == OSH_E_CAP_GEN_NARROW)
            snprintf(o, cap, "refused:%s", e_name(c->err));
        else snprintf(o, cap, "failed:%s", e_name(c->err));
        break;
    default: snprintf(o, cap, "unknown"); break;
    }
}

static void put_le(unsigned char *p, uint64_t v, int n)
{
    for (int i = 0; i < n; i++) p[i] = (unsigned char)(v >> (8 * i));
}

static void on_pipeline(void *ctx, const uint64_t *rec, size_t nrec, const uint64_t *out, size_t nout, const OshResult *res)
{
    (void)ctx;
    if (OBS->npipes >= MAXP) { OBS->overflow = 1; return; }
    ObsPipe *p = &OBS->p[OBS->npipes++];
    unsigned char ob[OSH_OUT_CAP];
    size_t no = nout <= OSH_OUT_CAP ? nout : OSH_OUT_CAP;
    for (size_t i = 0; i < no; i++) {
        if (out[i] > 255) OBS->bad_out_cell = 1;
        ob[i] = (unsigned char)out[i];
    }
    /* wire form, ABI 7.3: {magic u32 "OSHQ", abi_version u16, kind u16 = 1, total_len u32, reserved u32}, cells as u64 LE,
     * then out_used bytes of OUT (UNVERIFIED: one byte per OUT cell). */
    size_t total = 16 + 8 * nrec + no;
    unsigned char *w = malloc(total);
    put_le(w, 0x4F534851u, 4);
    put_le(w + 4, 1, 2);
    put_le(w + 6, 1, 2);
    put_le(w + 8, (uint64_t)total, 4);
    put_le(w + 12, 0, 4);
    for (size_t i = 0; i < nrec; i++) put_le(w + 16 + 8 * i, rec[i], 8);
    memcpy(w + 16 + 8 * nrec, ob, no);
    sha_hex(w, total, p->wire);
    free(w);
    /* render (vectors/README grammar; " direct" when flags bit 0 is set, UNVERIFIED extension) */
    char *L = p->line;
    size_t o = 0, cap = sizeof p->line;
    o += (size_t)snprintf(L, cap, "conn=%llu%s", (unsigned long long)rec[2], (rec[3] & OSH_REQ_FLAG_DIRECT) ? " direct" : "");
    uint64_t nc = rec[1];
    for (uint64_t c = 0; c < nc && OSH_REQ_HDR_CELLS + (c + 1) * OSH_CMD_CELLS <= nrec && o + 64 < cap; c++) {
        const uint64_t *b = rec + OSH_REQ_HDR_CELLS + c * OSH_CMD_CELLS;
        o += (size_t)snprintf(L + o, cap - o, " | C%llu b=%llu argv=", (unsigned long long)c, (unsigned long long)b[3]);
        for (uint64_t k = 0; k < b[0] && k < OSH_MAX_ARGV; k++) {
            if (k && o + 2 < cap) L[o++] = ',';
            uint64_t off = b[4 + 2 * k], len = b[5 + 2 * k];
            if (off + len <= no) q_encode(L, &o, cap, ob + off, (size_t)len);
        }
        o += (size_t)snprintf(L + o, cap - o, " as=");
        for (uint64_t k = 0; k < b[1] && k < OSH_MAX_ASSIGN; k++) {
            const uint64_t *e = b + 68 + 4 * k;
            if (k && o + 2 < cap) L[o++] = ',';
            if (e[0] + e[1] <= no && o + e[1] + 2 < cap) { memcpy(L + o, ob + e[0], (size_t)e[1]); o += (size_t)e[1]; }
            if (o + 2 < cap) L[o++] = '=';
            if (e[2] + e[3] <= no) q_encode(L, &o, cap, ob + e[2], (size_t)e[3]);
        }
        o += (size_t)snprintf(L + o, cap - o, " rd=");
        for (uint64_t k = 0; k < b[2] && k < OSH_MAX_REDIR; k++) {
            const uint64_t *e = b + 132 + 4 * k;
            static const char *const kn[] = {"?", "IN", "OUT", "APPEND", "DUP"};
            if (k && o + 2 < cap) L[o++] = ',';
            o += (size_t)snprintf(L + o, cap - o, "%s%llu:", kn[e[0] < 5 ? e[0] : 0], (unsigned long long)e[1]);
            if (e[0] == OSH_R_DUP) o += (size_t)snprintf(L + o, cap - o, "%llu", (unsigned long long)e[2]);
            else if (e[2] + e[3] <= no) q_encode(L, &o, cap, ob + e[2], (size_t)e[3]);
        }
    }
    if (o >= cap) o = cap - 1;
    L[o] = 0;
    size_t so = 0;
    for (int i = 0; i < res->ncmds && i < OSH_MAX_CMDS; i++) {
        char c[48];
        stage_cat(&res->cmd[i], c, sizeof c);
        so += (size_t)snprintf(p->stages + so, sizeof p->stages - so, "%s%s", i ? "," : "", c);
    }
    snprintf(p->err, sizeof p->err, "%s", e_name(res->err));
}

static void on_refusal(void *ctx, uint64_t code, uint64_t off)
{
    (void)ctx;
    OBS->refused = 1;
    OBS->refuse_code = code;
    OBS->refuse_off = off;
}

/* ---------------- fault injection (run child only) ---------------- */

static const char *g_revoke;
static int effect_revoke(void *ctx, const OshBinding *b, int op, const char *path)
{
    (void)ctx;
    (void)b;
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    return op == OSH_OP_SPAWN && g_revoke && strcmp(base, g_revoke) == 0 ? OSH_E_REVOKED : OSH_E_OK;
}

static void after_launch_interrupt(void *ctx)
{
    (void)ctx;
    kill(0, SIGINT); /* the run child leads its own group, so this is a terminal ^C to the shell and its pipeline only */
}

static void after_launch_stop_first(void *ctx)
{
    (void)ctx;
    char p[96], b[512];
    snprintf(p, sizeof p, "/proc/self/task/%ld/children", (long)syscall(SYS_gettid));
    int fd = open(p, O_RDONLY | O_CLOEXEC);
    ssize_t n = fd >= 0 ? read(fd, b, sizeof b - 1) : -1;
    if (fd >= 0) close(fd);
    if (n <= 0) { OBS->not_run = 1; snprintf(OBS->setup_why, sizeof OBS->setup_why, "no /proc children list"); kill(0, SIGINT); return; }
    b[n] = 0;
    pid_t first = (pid_t)atol(b);
    if (first > 0) kill(first, SIGSTOP);
}

static void after_launch_leak_fd(void *ctx)
{
    (void)ctx;
    (void)open("/dev/null", O_RDONLY); /* deliberately never closed */
}

static void after_launch_leak_process(void *ctx)
{
    (void)ctx;
    if (fork() == 0) { /* stays in the pipeline group, never reaped by the shell */
        sleep(30);
        _exit(0);
    }
}

static int count_fds(void)
{
    DIR *d = opendir("/proc/self/fd");
    if (!d) return -1;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d))) if (e->d_name[0] != '.') n++;
    closedir(d);
    return n - 1; /* the directory's own descriptor */
}

static int lowest_free_fd(void)
{
    for (int fd = 0; fd < 4096; fd++)
        if (fcntl(fd, F_GETFD) < 0 && errno == EBADF) return fd;
    return -1;
}

static int write_file(const char *dir, const char *name, const Bytes *b)
{
    char p[1024];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return -1;
    ssize_t w = b->n ? write(fd, b->p, b->n) : 0;
    close(fd);
    return w == (ssize_t)b->n ? 0 : -1;
}

static void setup_fail(const char *why)
{
    OBS->setup_failed = 1;
    snprintf(OBS->setup_why, sizeof OBS->setup_why, "%s: %s", why, strerror(errno));
    _exit(99);
}

/* The run child: fresh group, private directory, toy PATH, faults, limits, then the real shell core. Never returns. */
static void run_child(const Fixture *x, int native, const char *dir, const char *bindir)
{
    setpgid(0, 0);
    /* The run child stands in for a shell started from a terminal: SIGINT and SIGQUIT at their defaults.
     * A harness started under nohup or `cmd &` (the Spark judge, a CI runner) inherits them as ignored;
     * osh then rightly keeps them ignored in its children (POSIX), the stalled stages of f25 never see the
     * ^C and the fixture times out. Reset them so the fixture's precondition holds wherever it runs. */
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    if (chdir(dir) != 0) setup_fail("chdir");
    for (int i = 0; i < x->nsetup; i++)
        if (write_file(dir, x->setup[i].name, &x->setup[i].content)) setup_fail("fixture file");
    Bytes none = {NULL, 0};
    if (write_file(dir, ".stdin", x->has_in ? &x->in : &none)) setup_fail("stdin file");
    int fi = open(".stdin", O_RDONLY), fo = open(".stdout", O_WRONLY | O_CREAT | O_TRUNC, 0644),
        fe = open(".stderr", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fi < 0 || fo < 0 || fe < 0) setup_fail("stdio files");
    if (dup2(fi, 0) < 0 || dup2(fo, 1) < 0 || dup2(fe, 2) < 0) setup_fail("dup2");
    close(fi); close(fo); close(fe);
    static char pathv[1100];
    snprintf(pathv, sizeof pathv, "PATH=%s", bindir);
    char *envp[] = {pathv, "LC_ALL=C", NULL};
    OshShell *sh = calloc(1, sizeof *sh);
    if (!sh || osh_shell_init(sh, envp, native, 0, "osh", NULL, 0) != 0) setup_fail("osh_shell_init");
    sh->diff.on_pipeline = on_pipeline;
    sh->diff.on_refusal = on_refusal;
    if (x->fault == F_REVOKE_SPAWN) { g_revoke = x->fault_name; sh->s.effect_hook = effect_revoke; }
    if (x->fault == F_INTERRUPT) sh->s.after_launch_hook = after_launch_interrupt;
    if (x->fault == F_STOP_FIRST) sh->s.after_launch_hook = after_launch_stop_first;
    if (x->fault == F_LAUNCH_FAIL) sh->s.fail_fork_at = x->fault_arg;
    if (x->fault == F_MUTANT_LEAK_FD) sh->s.after_launch_hook = after_launch_leak_fd;
    if (x->fault == F_MUTANT_LEAK_PROCESS) sh->s.after_launch_hook = after_launch_leak_process;

    OshBuilder *b = NULL;
    if (x->direct) { /* ABI section 11, the sequence osh_submit_argv uses, for ncmds 1 to 8 */
        b = malloc(sizeof *b);
        if (!b) setup_fail("builder");
        osh_rb_init(b, OSH_REQ_FLAG_DIRECT, OSH_CONN_NONE);
        for (int c = 0; c < x->dncmds; c++) {
            osh_rb_cmd(b, OSH_B_NONE);
            for (int k = 0; k < x->dnargv[c]; k++) {
                char *a = strndup((const char *)x->dargv[c][k].p, x->dargv[c][k].n);
                osh_rb_arg(b, a);
                free(a);
            }
        }
        osh_rb_seal(b);
        if (b->err) setup_fail("direct record overflow");
    }
    OBS->fds_before = count_fds();
    struct rlimit rl;
    if (x->limit == L_NOFILE_TIGHT) {
        int lf = lowest_free_fd(); /* exactly one free descriptor below the limit: pipe2 needs two */
        rl.rlim_cur = rl.rlim_max = (rlim_t)lf + 1;
        if (lf < 0 || setrlimit(RLIMIT_NOFILE, &rl) != 0) setup_fail("RLIMIT_NOFILE");
    } else if (x->limit == L_NPROC) {
        if (geteuid() == 0) { OBS->not_run = 1; snprintf(OBS->setup_why, sizeof OBS->setup_why, "RLIMIT_NPROC does not bind root"); _exit(98); }
        rl.rlim_cur = rl.rlim_max = (rlim_t)x->limit_arg;
        if (setrlimit(RLIMIT_NPROC, &rl) != 0) setup_fail("RLIMIT_NPROC");
    } else if (x->limit == L_FSIZE) {
        rl.rlim_cur = rl.rlim_max = (rlim_t)x->limit_arg;
        if (setrlimit(RLIMIT_FSIZE, &rl) != 0) setup_fail("RLIMIT_FSIZE");
    } else if (x->limit == L_AS) {
        if (OSH_DIFF_ASAN) { OBS->not_run = 1; snprintf(OBS->setup_why, sizeof OBS->setup_why, "RLIMIT_AS cannot apply under ASan"); _exit(98); }
        rl.rlim_cur = rl.rlim_max = (rlim_t)x->limit_arg;
        if (setrlimit(RLIMIT_AS, &rl) != 0) setup_fail("RLIMIT_AS");
    }
    int st;
    if (x->direct) {
        OshResult res;
        st = osh_exec_record(&sh->s, b->rec, osh_rb_cells(b), b->out, b->out_used, &res);
        on_pipeline(NULL, b->rec, osh_rb_cells(b), b->out, b->out_used, &res);
        free(b);
    } else {
        st = osh_shell_run_string(sh, (const char *)x->text.p, x->text.n);
    }
    if (x->limit == L_NOFILE_TIGHT) { /* lift it again so the count below can open /proc/self/fd */
        rl.rlim_cur = rl.rlim_max = 1024;
        setrlimit(RLIMIT_NOFILE, &rl);
    }
    OBS->fds_after = count_fds();
    OBS->calls_native = sh->core.calls_native;
    OBS->calls_interp = sh->core.calls_interp;
    osh_shell_free(sh);
    free(sh);
    fflush(NULL);
    exit(st & 255);
}

/* ---------------- one host run ---------------- */

typedef struct {
    int not_run, fail;
    char why[512];
    char norm[LINE]; /* normalized result line fields after "OSHDIFF vec <id> " */
} Verdict;

static void vfail(Verdict *v, const char *fmt, ...)
{
    if (v->fail) return; /* first failure is the reason shown */
    v->fail = 1;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(v->why, sizeof v->why, fmt, ap);
    va_end(ap);
}

static Bytes slurp(const char *dir, const char *name, int *ok)
{
    char p[1024];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    Bytes b = {NULL, 0};
    *ok = 0;
    FILE *f = fopen(p, "rb");
    if (!f) return b;
    size_t cap = 1 << 16;
    b.p = malloc(cap);
    size_t r;
    while ((r = fread(b.p + b.n, 1, cap - b.n, f)) > 0) {
        b.n += r;
        if (b.n == cap) { cap *= 2; b.p = realloc(b.p, cap); }
    }
    fclose(f);
    *ok = 1;
    return b;
}

static void rm_tree(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    char p[1024];
    while ((e = readdir(d))) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        unlink(p);
    }
    closedir(d);
    rmdir(dir);
}

static void host_run(const Fixture *x, int native, const char *root, const char *bindir, Verdict *v)
{
    memset(v, 0, sizeof *v);
    char dir[1024];
    snprintf(dir, sizeof dir, "%s/%s.%s", root, x->id, native ? "native" : "interp");
    if (mkdir(dir, 0700) != 0) { vfail(v, "mkdir %s: %s", dir, strerror(errno)); return; }
    memset(OBS, 0, sizeof *OBS);
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) { vfail(v, "fork: %s", strerror(errno)); return; }
    if (pid == 0) run_child(x, native, dir, bindir);
    setpgid(pid, pid);
    int st = 0, done = 0;
    struct timespec t0, now, nap = {0, 2000000};
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;) {
        pid_t r = waitpid(pid, &st, WNOHANG);
        if (r == pid) { done = 1; break; }
        if (r < 0 && errno != EINTR) break;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec - t0.tv_sec > RUN_TIMEOUT_S) break;
        nanosleep(&nap, NULL);
    }
    if (!done) {
        kill(-pid, SIGKILL);
        waitpid(pid, &st, 0);
        vfail(v, "timeout after %d s (pipeline group killed)", RUN_TIMEOUT_S);
        rm_tree(dir);
        return;
    }
    /* cleanup (ABI 12): nothing of the pipeline may outlive the shell's return */
    int leftover = kill(-pid, 0) == 0;
    if (leftover) { kill(-pid, SIGKILL); usleep(20000); }
    if (OBS->not_run) { v->not_run = 1; snprintf(v->why, sizeof v->why, "%s", OBS->setup_why); rm_tree(dir); return; }
    if (OBS->setup_failed) { vfail(v, "setup: %s", OBS->setup_why); rm_tree(dir); return; }
    if (!WIFEXITED(st)) { vfail(v, "run child died of signal %d", WTERMSIG(st)); rm_tree(dir); return; }
    int status = WEXITSTATUS(st);
    int ok1, ok2;
    Bytes out = slurp(dir, ".stdout", &ok1), err = slurp(dir, ".stderr", &ok2);
    if (!ok1 || !ok2) vfail(v, "stdout/stderr capture missing");
    if (leftover) vfail(v, "a process of the pipeline outlived the shell (cleanup, ABI 12)");
    if (OBS->fds_before < 0 || OBS->fds_after != OBS->fds_before)
        vfail(v, "descriptor leak: %d open before, %d after (cleanup, ABI 12)", OBS->fds_before, OBS->fds_after);
    /* text runs must call the core only in the requested backend; direct runs never call it (ABI 11) */
    if (x->direct ? (OBS->calls_native || OBS->calls_interp)
                  : native ? (!OBS->calls_native || OBS->calls_interp) : (!OBS->calls_interp || OBS->calls_native))
        vfail(v, "core backend: %lu native and %lu interpreter calls in a %s %s run", OBS->calls_native, OBS->calls_interp,
              x->direct ? "direct" : "text", native ? "native" : "interpreter");
    if (OBS->overflow) vfail(v, "more than %d pipelines", MAXP);
    if (OBS->bad_out_cell) vfail(v, "an OUT cell above 255");

    /* expectations */
    char got[LINE];
    size_t o = 0;
    got[0] = 0;
    for (int i = 0; i < OBS->npipes && o + 64 < sizeof got; i++)
        o += (size_t)snprintf(got + o, sizeof got - o, "%sP%d %s", i ? " | " : "", i, OBS->p[i].line);
    if (x->has_req && strcmp(got, x->req) != 0) vfail(v, "request records differ:\n      want %s\n      got  %s", x->req, got);
    if (!x->has_req && OBS->npipes) vfail(v, "a request was produced but the fixture expects none: %s", got);
    if (x->has_refuse != OBS->refused || (x->has_refuse && ((int)OBS->refuse_code != x->refuse_code || (int)OBS->refuse_off != x->refuse_off)))
        vfail(v, "refusal: want %s %d off=%d, got %s %llu off=%llu", x->has_refuse ? "refuse" : "none", x->refuse_code, x->refuse_off,
              OBS->refused ? "refuse" : "none", (unsigned long long)OBS->refuse_code, (unsigned long long)OBS->refuse_off);
    if (x->has_refuse && x->refuse_name[0]) {
        const char *nm = osh_code_name((unsigned)x->refuse_code);
        if (!nm || strcmp(nm, x->refuse_name) != 0) vfail(v, "refusal name %s is not the core's name for %d", x->refuse_name, x->refuse_code);
    }
    if (out.n != x->out.n || (out.n && memcmp(out.p, x->out.p, out.n) != 0)) {
        char a[512], b[512];
        size_t oa = 0, ob = 0;
        q_encode(a, &oa, sizeof a, x->out.p, x->out.n < 100 ? x->out.n : 100);
        q_encode(b, &ob, sizeof b, out.p, out.n < 100 ? out.n : 100);
        vfail(v, "stdout: want %zu bytes %s, got %zu bytes %s", x->out.n, a, out.n, b);
    }
    if (x->has_errhas && !memmem(err.p, err.n, x->errhas.p, x->errhas.n)) vfail(v, "stderr lacks the expected diagnostic");
    if (status != x->status) vfail(v, "status: want %d, got %d", x->status, status);
    char gst[LINE], ger[1024];
    size_t a = 0, b = 0;
    gst[0] = ger[0] = 0;
    for (int i = 0; i < OBS->npipes; i++) {
        a += (size_t)snprintf(gst + a, sizeof gst - a, "%sP%d=%s", i ? " " : "", i, OBS->p[i].stages);
        b += (size_t)snprintf(ger + b, sizeof ger - b, "%sP%d=%s", i ? " " : "", i, OBS->p[i].err);
    }
    if (strcmp(gst, x->stages) != 0) vfail(v, "stages: want '%s', got '%s'", x->stages, gst);
    if (strcmp(ger, x->errs) != 0) vfail(v, "platform errors: want '%s', got '%s'", x->errs, ger);
    char files[1024];
    size_t fo = 0;
    files[0] = 0;
    for (int i = 0; i < x->noutfile; i++) {
        int okf;
        Bytes fb = slurp(dir, x->outfile[i].name, &okf);
        if (!okf) vfail(v, "output file %s missing", x->outfile[i].name);
        else if (fb.n != x->outfile[i].content.n || (fb.n && memcmp(fb.p, x->outfile[i].content.p, fb.n) != 0))
            vfail(v, "output file %s: content differs (%zu bytes)", x->outfile[i].name, fb.n);
        char h[65];
        sha_hex(fb.p ? fb.p : (unsigned char *)"", fb.n, h);
        fo += (size_t)snprintf(files + fo, sizeof files - fo, "%s%s:%zu:%s", fo ? "," : "", x->outfile[i].name, fb.n, h);
        free(fb.p);
    }
    if (x->outlen_name[0]) {
        int okf;
        Bytes fb = slurp(dir, x->outlen_name, &okf);
        if (!okf || (long long)fb.n != x->outlen) vfail(v, "output file %s: want %lld bytes, got %zu", x->outlen_name, x->outlen, fb.n);
        char h[65];
        sha_hex(fb.p ? fb.p : (unsigned char *)"", fb.n, h);
        fo += (size_t)snprintf(files + fo, sizeof files - fo, "%s%s:%zu:%s", fo ? "," : "", x->outlen_name, fb.n, h);
        free(fb.p);
    }

    /* normalized line (README "Result file"): the fields every adapter reports */
    char outh[65];
    sha_hex(out.p ? out.p : (unsigned char *)"", out.n, outh);
    size_t n = 0;
    n += (size_t)snprintf(v->norm + n, sizeof v->norm - n, "req=");
    for (int i = 0; i < OBS->npipes; i++) n += (size_t)snprintf(v->norm + n, sizeof v->norm - n, "%s%s", i ? "," : "", OBS->p[i].wire);
    if (!OBS->npipes) n += (size_t)snprintf(v->norm + n, sizeof v->norm - n, "none");
    if (OBS->refused) n += (size_t)snprintf(v->norm + n, sizeof v->norm - n, " refuse=%llu:%llu", (unsigned long long)OBS->refuse_code, (unsigned long long)OBS->refuse_off);
    else n += (size_t)snprintf(v->norm + n, sizeof v->norm - n, " refuse=none");
    n += (size_t)snprintf(v->norm + n, sizeof v->norm - n, " status=%d stages=", status);
    for (int i = 0; i < OBS->npipes; i++) n += (size_t)snprintf(v->norm + n, sizeof v->norm - n, "%sP%d=%s", i ? ";" : "", i, OBS->p[i].stages);
    if (!OBS->npipes) n += (size_t)snprintf(v->norm + n, sizeof v->norm - n, "none");
    n += (size_t)snprintf(v->norm + n, sizeof v->norm - n, " err=");
    for (int i = 0; i < OBS->npipes; i++) n += (size_t)snprintf(v->norm + n, sizeof v->norm - n, "%sP%d=%s", i ? ";" : "", i, OBS->p[i].err);
    if (!OBS->npipes) n += (size_t)snprintf(v->norm + n, sizeof v->norm - n, "none");
    snprintf(v->norm + n, sizeof v->norm - n, " out=%zu:%s files=%s", out.n, outh, fo ? files : "none");
    free(out.p);
    free(err.p);
    rm_tree(dir);
}

/* ---------------- result files (README "Result file") ---------------- */

static const char *const PIN_KEYS[] = {"adapter", "aienos", "omega", "core", "abi", "cc", "harness", "fixtures"};
#define NPIN 8

typedef struct {
    int present, valid;
    char why[256];
    char pin[NPIN][300];
    char *vec[MAXFX]; /* normalized fields per fixture index, NULL = no line */
} ResultFile;

static void free_results(ResultFile *r)
{
    for (int i = 0; i < MAXFX; i++) { free(r->vec[i]); r->vec[i] = NULL; }
}

static int pin_index(const char *k, size_t n)
{
    for (int i = 0; i < NPIN; i++)
        if (strlen(PIN_KEYS[i]) == n && strncmp(PIN_KEYS[i], k, n) == 0) return i;
    return -1;
}

static int fx_index(const char *id)
{
    for (int i = 0; i < NFX; i++)
        if (strcmp(FX[i].id, id) == 0) return i;
    return -1;
}

static void rf_invalid(ResultFile *r, const char *fmt, const char *arg)
{
    if (!r->valid) return;
    r->valid = 0;
    snprintf(r->why, sizeof r->why, fmt, arg);
}

/* Only lines starting with "OSHDIFF" count (after an optional CR at the end), so a raw QEMU serial log is accepted. */
static void load_results(const char *path, ResultFile *r)
{
    memset(r, 0, sizeof *r);
    r->present = 1;
    r->valid = 1;
    FILE *f = fopen(path, "rb");
    if (!f) { rf_invalid(r, "cannot open %s", path); return; }
    static char line[LINE];
    int header = 0, pins = 0, ended = 0, nvec = 0;
    while (fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        if (strncmp(line, "OSHDIFF", 7) != 0) continue;
        if (ended) { rf_invalid(r, "%s", "an OSHDIFF line after the end line"); continue; }
        if (strcmp(line, "OSHDIFF-RESULT 1") == 0) {
            if (header) rf_invalid(r, "%s", "two header lines");
            header = 1;
            continue;
        }
        if (!header) { rf_invalid(r, "%s", "first OSHDIFF line is not \"OSHDIFF-RESULT 1\""); continue; }
        if (strncmp(line, "OSHDIFF pin ", 12) == 0) {
            if (pins) rf_invalid(r, "%s", "two pin lines");
            pins = 1;
            for (char *s = line + 12; *s;) {
                /* Bound the token before looking for a value. In particular,
                 * a missing '=' must not read past the terminating NUL. */
                size_t token_len = strcspn(s, " ");
                char *eq = memchr(s, '=', token_len);
                if (!eq) { rf_invalid(r, "%s", "pin token without ="); break; }
                size_t k = (size_t)(eq - s);
                char *val = eq + 1;
                size_t vl = token_len - k - 1;
                int pi = pin_index(s, k);
                if (pi < 0) { rf_invalid(r, "unknown pin key in: %s", s); break; }

                if (vl == 0 || vl >= sizeof r->pin[pi]) { rf_invalid(r, "empty or long pin value for %s", PIN_KEYS[pi]); break; }
                snprintf(r->pin[pi], sizeof r->pin[pi], "%.*s", (int)vl, val);
                s = val + vl;
                while (*s == ' ') s++;
            }
            continue;
        }
        if (strncmp(line, "OSHDIFF vec ", 12) == 0) {
            char *id = line + 12;
            size_t k = strcspn(id, " ");
            if (id[k] != ' ') { rf_invalid(r, "%s", "vec line without fields"); continue; }
            id[k] = 0;
            int i = fx_index(id);
            if (i < 0) { rf_invalid(r, "unknown fixture %s", id); continue; }
            if (r->vec[i]) { rf_invalid(r, "duplicate line for %s", id); continue; }
            r->vec[i] = strdup(id + k + 1);
            nvec++;
            continue;
        }
        if (strncmp(line, "OSHDIFF end n=", 14) == 0) {
            char *e;
            long want = strtol(line + 14, &e, 10);
            if (*e || want != nvec) rf_invalid(r, "%s", "end line count does not match the vec lines");
            ended = 1;
            continue;
        }
        rf_invalid(r, "unknown OSHDIFF line: %s", line);
    }
    fclose(f);
    if (!header) rf_invalid(r, "%s", "no \"OSHDIFF-RESULT 1\" header");
    if (!pins) rf_invalid(r, "%s", "no pin line");
    if (!ended) rf_invalid(r, "%s", "no end line (truncated log?)");
    for (int i = 0; i < NPIN && r->valid; i++)
        if (!r->pin[i][0]) rf_invalid(r, "pin %s missing", PIN_KEYS[i]);
}

/* ---------------- main ---------------- */

static void core_digest(void)
{
    sha256_ctx c;
    sha256_init(&c);
    const unsigned char *u[3] = {osh_unit_lex, osh_unit_parse, osh_unit_expand};
    size_t n[3] = {osh_unit_lex_len, osh_unit_parse_len, osh_unit_expand_len};
    for (int i = 0; i < 3; i++) {
        unsigned char l[8];
        put_le(l, n[i], 8);
        sha256_update(&c, l, 8);
        sha256_update(&c, u[i], n[i]);
    }
    uint8_t d[32];
    sha256_final(&c, d);
    hex32(d, CORE_SHA);
}

static int make_bindir(const char *toy, const char *bindir)
{
    if (mkdir(bindir, 0700) != 0) return -1;
    for (size_t i = 0; i < sizeof TOYS / sizeof TOYS[0]; i++) {
        char p[2400];
        snprintf(p, sizeof p, "%s/%s", bindir, TOYS[i]);
        if (symlink(toy, p) != 0) return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *native_path = NULL, *emit_path = NULL, *host_path = NULL;
    int require_native = 0, strict_host = 0;
    int ai = 1;
    for (; ai < argc && strncmp(argv[ai], "--", 2) == 0; ai++) {
        if (strcmp(argv[ai], "--native") == 0 && ai + 1 < argc) native_path = argv[++ai];
        else if (strcmp(argv[ai], "--emit") == 0 && ai + 1 < argc) emit_path = argv[++ai];
        else if (strcmp(argv[ai], "--host-results") == 0 && ai + 1 < argc) host_path = argv[++ai];
        else if (strcmp(argv[ai], "--require-native") == 0) require_native = 1;
        else if (strcmp(argv[ai], "--strict-host") == 0) strict_host = 1;
        else if (strcmp(argv[ai], "--pin") == 0 && ai + 1 < argc) {
            const char *p = argv[++ai];
            if (strncmp(p, "omega=", 6) == 0) PIN_OMEGA = p + 6;
            else if (strncmp(p, "cc=", 3) == 0) PIN_CC = p + 3;
            else if (strncmp(p, "harness=", 8) == 0) PIN_HARNESS = p + 8;
            else if (strncmp(p, "aienos=", 7) == 0) PIN_AIENOS_LOCK = p + 7;
            else { fprintf(stderr, "unknown pin %s\n", p); return 2; }
            if (!*strchr(p, 0x3d) || strchr(p, 0x20)) { fprintf(stderr, "pin values are one word: %s\n", p); return 2; }
        }
        else { fprintf(stderr, "unknown option %s\n", argv[ai]); return 2; }
    }
    if (argc - ai != 2) {
        fprintf(stderr, "usage: osh_diff [--native FILE] [--require-native] [--emit FILE] [--host-results FILE] [--strict-host] TOYBIN FIXTURES\n");
        return 2;
    }
    const char *toy = argv[ai], *fixtures = argv[ai + 1];
    if (toy[0] != '/') { fprintf(stderr, "TOYBIN must be an absolute path\n"); return 2; }
    if (load_fixtures(fixtures) != 0) { printf("OSH_DIFF_FAIL\n"); return 1; }
    core_digest();
    OBS = mmap(NULL, sizeof *OBS, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (OBS == MAP_FAILED) { perror("mmap"); return 1; }

    char root[] = "/tmp/osh_diff.XXXXXX";
    if (!mkdtemp(root)) { perror("mkdtemp"); return 1; }
    char bindir[1100];
    snprintf(bindir, sizeof bindir, "%s/bin", root);
    if (make_bindir(toy, bindir) != 0) { perror("toy bin directory"); return 1; }

    static Verdict hn[MAXFX], hi[MAXFX];
    ResultFile hostf = {0}, nat = {0};
    if (host_path) {
        load_results(host_path, &hostf);
        if (!hostf.valid) {
            printf("host results %s invalid: %s\nOSH_DIFF_COMPARE_ONLY_FAIL\n", host_path, hostf.why);
            free_results(&hostf);
            rm_tree(bindir);
            rmdir(root);
            munmap(OBS, sizeof *OBS);
            return 1;
        }
        for (int i = 0; i < NFX; i++) {
            memset(&hn[i], 0, sizeof hn[i]);
            if (hostf.vec[i]) snprintf(hn[i].norm, sizeof hn[i].norm, "%s", hostf.vec[i]);
            else { hn[i].not_run = 1; snprintf(hn[i].why, sizeof hn[i].why, "no line in host results"); }
            hi[i] = hn[i];
        }
    } else {
        for (int i = 0; i < NFX; i++) {
            host_run(&FX[i], 1, root, bindir, &hn[i]);
            host_run(&FX[i], 0, root, bindir, &hi[i]);
            if (!hn[i].fail && !hi[i].fail && !hn[i].not_run && !hi[i].not_run && strcmp(hn[i].norm, hi[i].norm) != 0)
                vfail(&hi[i], "native and interpreter results differ:\n      native %s\n      interp %s", hn[i].norm, hi[i].norm);
        }
    }
    rm_tree(bindir);
    rmdir(root);
    if (native_path) load_results(native_path, &nat);

    /* host pins (README "Pins") */
    char hpin[NPIN][300];
    snprintf(hpin[0], sizeof hpin[0], "linux-host");
    snprintf(hpin[1], sizeof hpin[1], "lock:%s", PIN_AIENOS_LOCK);
    snprintf(hpin[2], sizeof hpin[2], "%s", PIN_OMEGA);
    snprintf(hpin[3], sizeof hpin[3], "%s", CORE_SHA);
    snprintf(hpin[4], sizeof hpin[4], "%s", ABI_PIN);
    snprintf(hpin[5], sizeof hpin[5], "%s", PIN_CC);
    snprintf(hpin[6], sizeof hpin[6], "%s", PIN_HARNESS);
    snprintf(hpin[7], sizeof hpin[7], "%s", FIXTURES_SHA);
    if (host_path) for (int i = 0; i < NPIN; i++) snprintf(hpin[i], sizeof hpin[i], "%s", hostf.pin[i]);

    if (nat.present && nat.valid) {
        if (strcmp(nat.pin[0], "aienos-native") != 0) rf_invalid(&nat, "adapter is %s, not aienos-native", nat.pin[0]);
        else if (strcmp(nat.pin[4], hpin[4]) != 0) rf_invalid(&nat, "abi pin %s differs from the host's", nat.pin[4]);
        else if (strcmp(nat.pin[7], hpin[7]) != 0) rf_invalid(&nat, "fixtures digest %s differs from the corpus run here", nat.pin[7]);
        else if (strcmp(nat.pin[3], hpin[3]) != 0) rf_invalid(&nat, "shell core digest %s differs: not the same compiled core", nat.pin[3]);
    }

    printf("OSH cross-adapter pipeline differential (omega#568)%s\n", host_path ? " -- COMPARE ONLY, host results read from a file, not run" : "");
    printf("pins host:   adapter=%s aienos=%s omega=%s\n             core=%s\n             abi=%s\n             cc=%s harness=%s\n             fixtures=%s\n",
           hpin[0], hpin[1], hpin[2], hpin[3], hpin[4], hpin[5], hpin[6], hpin[7]);
    if (!nat.present) printf("pins aienos: none supplied (no native result file)\n");
    else if (!nat.valid) printf("pins aienos: INVALID result file %s: %s\n", native_path, nat.why);
    else printf("pins aienos: adapter=%s aienos=%s omega=%s cc=%s harness=%s\n", nat.pin[0], nat.pin[1], nat.pin[2], nat.pin[5], nat.pin[6]);
    printf("%-28s %-8s %-11s %-11s %-8s %s\n", "fixture", "scope", "host-native", "host-interp", "aienos", "reason");

    int hp = 0, hf = 0, hnr = 0, np = 0, nf = 0, nnr = 0;
    for (int i = 0; i < NFX; i++) {
        const Fixture *x = &FX[i];
        const char *c1 = hn[i].fail ? "FAIL" : hn[i].not_run ? "NOT_RUN" : "PASS";
        const char *c2 = hi[i].fail ? "FAIL" : hi[i].not_run ? "NOT_RUN" : "PASS";
        if (host_path) c1 = c2 = hn[i].not_run ? "NOT_RUN" : "(file)";
        const char *c3, *why3 = "";
        char nwhy[LINE];
        nwhy[0] = 0;
        if (x->linux_only) { c3 = "NOT_RUN"; why3 = "platform-specific (linux)"; }
        else if (!nat.present) { c3 = "NOT_RUN"; why3 = "no native result supplied"; }
        else if (!nat.valid) { c3 = "FAIL"; why3 = "native result file invalid"; }
        else if (!nat.vec[i]) { c3 = "NOT_RUN"; why3 = "no native line for this fixture"; }
        else if (hn[i].fail || hn[i].not_run) { c3 = "FAIL"; why3 = "host reference did not pass; nothing to compare"; }
        else if (strcmp(nat.vec[i], hn[i].norm) != 0) {
            c3 = "FAIL";
            snprintf(nwhy, sizeof nwhy, "differs from host:\n      host   %s\n      aienos %s", hn[i].norm, nat.vec[i]);
            why3 = nwhy;
        } else c3 = "PASS";
        const char *why = hn[i].fail || hn[i].not_run ? hn[i].why : hi[i].fail || hi[i].not_run ? hi[i].why : why3;
        printf("%-28s %-8s %-11s %-11s %-8s %s\n", x->id, x->linux_only ? "linux" : "portable", c1, c2, c3, why);
        if (!host_path) {
            for (int k = 0; k < 2; k++) {
                const Verdict *h = k ? &hi[i] : &hn[i];
                if (h->fail) hf++;
                else if (h->not_run) hnr++;
                else hp++;
            }
        } else if (hn[i].not_run) hnr++;
        if (!strcmp(c3, "PASS")) np++;
        else if (!strcmp(c3, "FAIL")) nf++;
        else nnr++;
    }
    int portable_nr = 0;
    for (int i = 0; i < NFX; i++)
        if (!FX[i].linux_only && (!nat.present || (nat.valid && !nat.vec[i]))) portable_nr++;
    printf("summary: host PASS %d FAIL %d NOT_RUN %d (two runs per fixture) | aienos PASS %d FAIL %d NOT_RUN %d (portable NOT_RUN %d)\n",
           hp, hf, hnr, np, nf, nnr, portable_nr);

    if (emit_path && !host_path) {
        FILE *e = fopen(emit_path, "w");
        if (!e) { perror(emit_path); free_results(&hostf); free_results(&nat); munmap(OBS, sizeof *OBS); return 1; }
        fprintf(e, "OSHDIFF-RESULT 1\nOSHDIFF pin");
        for (int k = 0; k < NPIN; k++) fprintf(e, " %s=%s", PIN_KEYS[k], hpin[k]);
        fprintf(e, "\n");
        int ne = 0;
        for (int i = 0; i < NFX; i++)
            if (!hn[i].fail && !hn[i].not_run) { fprintf(e, "OSHDIFF vec %s %s\n", FX[i].id, hn[i].norm); ne++; }
        fprintf(e, "OSHDIFF end n=%d\n", ne);
        fclose(e);
    }
    int bad = hf || nf || (strict_host && hnr) || (require_native && portable_nr);
    if (require_native && portable_nr) printf("acceptance: %d portable fixtures have no native AIENOS result (NOT_RUN is never green)\n", portable_nr);
    if (host_path) printf("%s\n", bad ? "OSH_DIFF_COMPARE_ONLY_FAIL" : "OSH_DIFF_COMPARE_ONLY_PASS");
    else printf("%s\n", bad ? "OSH_DIFF_FAIL" : "OSH_DIFF_PASS");
    free_results(&hostf);
    free_results(&nat);
    munmap(OBS, sizeof *OBS);
    return bad ? 1 : 0;
}
