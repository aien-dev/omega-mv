/*
 * omega_bytes -- run instruction words written by hand, with the least machinery between.
 *
 * Input: a text file of instruction words in hex, one instruction per line; '#', ';' or
 * '//' start a comment. a64: one 32-bit word per line. gb10: one 128-bit sm_121
 * instruction per line, either as two 64-bit halves "lo hi" (the order cuobjdump and
 * nvdisasm print) or as one 128-bit number.
 *
 * Every word is named before anything runs; a word that cannot be named is refused:
 *   a64  -- src/aarch64_decoder.c must decode it, and the last word must be RET;
 *   gb10 -- its opcode (bits 0-11) must be one the in-repo sm_121 encoder emits for some
 *           opcode of the BlackwellIROpcode list in src/omega_blackwell_codegen.h
 *           (table built at start-up through omega_blackwell_encode_one).
 *
 * Then:
 *   a64  -- mapped executable by src/compiler/osc_native.c and called through
 *           osc_rt_call_native with up to 6 integer arguments;
 *   gb10 -- handed as program bytes to omega_gpu_execute with the Blackwell backend
 *           (src/omega_blackwell_engine.c), the door omega_blackwell_execute_vector uses.
 *           No CUDA library, no compiler, no cubin or ELF: the words go in as written.
 *
 * Usage:
 *   omega_bytes a64  FILE [ARG...]                 run, print result + receipt
 *   omega_bytes a64  FILE --gate mul3sub2|vadd64    16 argument sets against the C reference
 *   omega_bytes gb10 FILE [--n N] [--reps R] [--expect vadd|unchanged]
 *   omega_bytes check a64|gb10 FILE                 name every word, run nothing
 *   omega_bytes names                               print the gb10 opcode table
 *   omega_bytes emit-encoder-vecadd                 the existing encoder's vector add as a word file
 *   omega_bytes cpu-vadd [--n N] [--reps R]         gcc-compiled C loop on the CPU (reference point)
 *
 * The tool itself is C (Drake 2026-10-19 brief L098: allowed for the tool for now).
 */
#include "aarch64_decoder.h"
#include "compiler/osc_native.h"
#include "compiler/osc_rt.h"
#include "omega_blackwell_codegen.h"
#include "omega_blackwell_encoder.h"
#include "omega_blackwell_engine.h"
#include "omega_gpu_engine.h"
#include "sha256.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define OB_MAX_WORDS 4096u

/* ---- word file ---------------------------------------------------------------------- */

typedef struct {
    uint32_t w[OB_MAX_WORDS * 4]; /* 32-bit words in memory order */
    size_t nw;                    /* number of 32-bit words */
    unsigned line[OB_MAX_WORDS];  /* source line of each instruction */
} WordFile;

static int parse_hex(const char *s, size_t len, uint64_t *hi, uint64_t *lo, unsigned *digits) {
    *hi = 0;
    *lo = 0;
    *digits = 0;
    size_t i = 0;
    if (len >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) i = 2;
    for (; i < len; i++) {
        int v;
        char c = s[i];
        if (c == '_') continue;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        else return -1;
        if (++*digits > 32) return -1;
        *hi = (*hi << 4) | (*lo >> 60);
        *lo = (*lo << 4) | (uint64_t)v;
    }
    return *digits ? 0 : -1;
}

/* insn_words: 1 for a64, 4 for gb10. */
static int read_words(const char *path, unsigned insn_words, WordFile *wf) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "omega_bytes: cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }
    memset(wf, 0, sizeof *wf);
    char buf[1024];
    unsigned ln = 0;
    int rc = 0;
    while (fgets(buf, sizeof buf, f)) {
        ln++;
        char *cut = strpbrk(buf, "#;");
        if (cut) *cut = 0;
        cut = strstr(buf, "//");
        if (cut) *cut = 0;
        const char *tok[3];
        size_t tlen[3];
        unsigned nt = 0;
        for (char *p = buf; *p;) {
            while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',') p++;
            if (!*p) break;
            char *q = p;
            while (*q && *q != ' ' && *q != '\t' && *q != '\n' && *q != '\r' && *q != ',') q++;
            if (nt == 3) { nt = 4; break; }
            tok[nt] = p;
            tlen[nt] = (size_t)(q - p);
            nt++;
            p = q;
        }
        if (nt == 0) continue;
        if (wf->nw / insn_words >= OB_MAX_WORDS) {
            fprintf(stderr, "%s:%u: more than %u instructions\n", path, ln, OB_MAX_WORDS);
            rc = -1;
            break;
        }
        uint64_t hi, lo, hi2, lo2;
        unsigned d, d2;
        size_t at = wf->nw;
        if (insn_words == 1) {
            if (nt != 1 || parse_hex(tok[0], tlen[0], &hi, &lo, &d) || d > 8) {
                fprintf(stderr, "%s:%u: expected one 32-bit hex word\n", path, ln);
                rc = -1;
                break;
            }
            wf->w[at] = (uint32_t)lo;
        } else if (nt == 2) {
            if (parse_hex(tok[0], tlen[0], &hi, &lo, &d) || d > 16 || parse_hex(tok[1], tlen[1], &hi2, &lo2, &d2) ||
                d2 > 16) {
                fprintf(stderr, "%s:%u: expected two 64-bit hex halves (low first)\n", path, ln);
                rc = -1;
                break;
            }
            wf->w[at] = (uint32_t)lo;
            wf->w[at + 1] = (uint32_t)(lo >> 32);
            wf->w[at + 2] = (uint32_t)lo2;
            wf->w[at + 3] = (uint32_t)(lo2 >> 32);
        } else if (nt == 1) {
            if (parse_hex(tok[0], tlen[0], &hi, &lo, &d)) {
                fprintf(stderr, "%s:%u: expected one 128-bit hex word\n", path, ln);
                rc = -1;
                break;
            }
            wf->w[at] = (uint32_t)lo;
            wf->w[at + 1] = (uint32_t)(lo >> 32);
            wf->w[at + 2] = (uint32_t)hi;
            wf->w[at + 3] = (uint32_t)(hi >> 32);
        } else {
            fprintf(stderr, "%s:%u: too many numbers on one line\n", path, ln);
            rc = -1;
            break;
        }
        wf->line[at / insn_words] = ln;
        wf->nw += insn_words;
    }
    fclose(f);
    if (rc == 0 && wf->nw == 0) {
        fprintf(stderr, "%s: no instruction words\n", path);
        rc = -1;
    }
    return rc;
}

/* Little-endian bytes of the words, as they sit in memory. */
static void words_bytes(const WordFile *wf, uint8_t *out) {
    for (size_t i = 0; i < wf->nw; i++) {
        out[i * 4] = (uint8_t)wf->w[i];
        out[i * 4 + 1] = (uint8_t)(wf->w[i] >> 8);
        out[i * 4 + 2] = (uint8_t)(wf->w[i] >> 16);
        out[i * 4 + 3] = (uint8_t)(wf->w[i] >> 24);
    }
}

static void sha_hex(const uint8_t *p, size_t n, char out[65]) {
    static const char H[] = "0123456789abcdef";
    uint8_t d[32];
    sha256_ctx c;
    sha256_init(&c);
    sha256_update(&c, p, n);
    sha256_final(&c, d);
    for (int i = 0; i < 32; i++) {
        out[i * 2] = H[d[i] >> 4];
        out[i * 2 + 1] = H[d[i] & 15];
    }
    out[64] = 0;
}

/* ---- naming: a64 --------------------------------------------------------------------- */

static const char *a64_name(DecodedOp op) {
    static const char *const N[] = {"INVALID", "ADD", "SUB", "MUL", "AND", "ORR", "EOR", "MOVZ", "RET", "B",
                                    "B.cond", "CBZ", "CBNZ", "MOVK", "ADR", "LDR", "STR", "LDRB", "STRB", "SUBS"};
    return (unsigned)op < sizeof N / sizeof N[0] ? N[op] : "?";
}

static int check_a64(const char *path, const WordFile *wf, int verbose) {
    for (size_t i = 0; i < wf->nw; i++) {
        DecodedInsn d;
        if (aarch64_decode_instruction(wf->w[i], &d) != 0 || d.op == DECODED_INVALID) {
            fprintf(stderr, "REFUSED %s:%u: word 0x%08x is not named by src/aarch64_decoder.c\n", path, wf->line[i],
                    wf->w[i]);
            return -1;
        }
        if (verbose) printf("  %3zu  0x%08x  %s\n", i, wf->w[i], a64_name(d.op));
    }
    uint8_t bytes[OB_MAX_WORDS * 4];
    char err[160];
    words_bytes(wf, bytes);
    if (aarch64_validate_code_buffer(bytes, wf->nw * 4, err, sizeof err) != 0) {
        fprintf(stderr, "REFUSED %s: %s\n", path, err);
        return -1;
    }
    return 0;
}

/* ---- naming: gb10 -------------------------------------------------------------------- */

/* Opcode key -> names, built from the in-repo encoder. Up to 4 IR ops share a key (e.g.
 * LDG.E, LDG.E.64, LDG.E.128 differ only in width bits outside the key). */
typedef struct {
    uint32_t key;
    const char *names[6];
    unsigned nnames;
} Bw12;

static Bw12 g_tab[128];
static unsigned g_ntab;

static const struct {
    BlackwellIROpcode op;
    const char *name;
} BW_OPS[] = {
    {BW_IR_NOP, "NOP"}, {BW_IR_MOV_RZ, "MOV"}, {BW_IR_MOV_IMM, "MOV(imm)"}, {BW_IR_S2R, "S2R"},
    {BW_IR_LDC, "LDC"}, {BW_IR_LDC64, "LDC.64"}, {BW_IR_LDCU, "LDCU"}, {BW_IR_LDCU64, "LDCU.64"},
    {BW_IR_IMAD, "IMAD"}, {BW_IR_IMAD_WIDE, "IMAD.WIDE.U32"}, {BW_IR_ISETP_GE, "ISETP.GE"},
    {BW_IR_LDG_E, "LDG.E"}, {BW_IR_STG_E, "STG.E"}, {BW_IR_STG_EF, "STG.E.EF"}, {BW_IR_IADD3, "IADD3"},
    {BW_IR_HMMA_F16, "HMMA.F16"}, {BW_IR_HMMA_BF16, "HMMA.BF16"}, {BW_IR_SHF_R, "SHF.R"},
    {BW_IR_LOP3_AND, "LOP3(and)"}, {BW_IR_LDG_E_U16, "LDG.E.U16"}, {BW_IR_LDG_STRONG_SYS, "LDG.STRONG.SYS"},
    {BW_IR_LDG_MMIO, "LDG.MMIO"}, {BW_IR_STG_STRONG_SYS, "STG.STRONG.SYS"}, {BW_IR_MEMBAR_ALL_SYS, "MEMBAR.ALL.SYS"},
    {BW_IR_MEMBAR_SC_SYS, "MEMBAR.SC.SYS"}, {BW_IR_CCTL_IVALL, "CCTL.IVALL"},
    {BW_IR_ATOMG_ADD_STRONG_SYS, "ATOMG.ADD"}, {BW_IR_ATOMG_EXCH_STRONG_SYS, "ATOMG.EXCH"},
    {BW_IR_ISETP_GE_U32, "ISETP.GE.U32"}, {BW_IR_LOP3_XOR, "LOP3(xor)"}, {BW_IR_FADD, "FADD"}, {BW_IR_FSUB, "FSUB"},
    {BW_IR_FMUL, "FMUL"}, {BW_IR_FFMA, "FFMA"}, {BW_IR_FSETP, "FSETP"}, {BW_IR_FSEL, "FSEL"},
    {BW_IR_FMNMX_MIN, "FMNMX(min)"}, {BW_IR_FMNMX_MAX, "FMNMX(max)"}, {BW_IR_I2FP, "I2FP"}, {BW_IR_F2I, "F2I"},
    {BW_IR_MUFU_RCP, "MUFU.RCP"}, {BW_IR_MUFU_RSQ, "MUFU.RSQ"}, {BW_IR_SHFL_DOWN, "SHFL.DOWN"}, {BW_IR_LDS, "LDS.U8"},
    {BW_IR_STS, "STS.U8"}, {BW_IR_EXIT, "EXIT"}, {BW_IR_BRA, "BRA"}, {BW_IR_MUFU_EX2, "MUFU.EX2"},
    {BW_IR_BAR_SYNC, "BAR.SYNC"}, {BW_IR_LDS32, "LDS"}, {BW_IR_STS32, "STS"}, {BW_IR_LOP3_LUT, "LOP3.LUT"},
    {BW_IR_SHF_L_U32, "SHF.L.U32"}, {BW_IR_IMAD_HI_U32, "IMAD.HI.U32"}, {BW_IR_BSSY, "BSSY"}, {BW_IR_BSYNC, "BSYNC"},
    {BW_IR_LDG_E_64, "LDG.E.64"}, {BW_IR_LDG_E_128, "LDG.E.128"}, {BW_IR_LDS128, "LDS.128"},
};

static void tab_add(uint32_t key, const char *name) {
    for (unsigned i = 0; i < g_ntab; i++) {
        if (g_tab[i].key != key) continue;
        for (unsigned j = 0; j < g_tab[i].nnames; j++)
            if (strcmp(g_tab[i].names[j], name) == 0) return;
        if (g_tab[i].nnames < 6) g_tab[i].names[g_tab[i].nnames++] = name;
        return;
    }
    if (g_ntab < sizeof g_tab / sizeof g_tab[0]) {
        g_tab[g_ntab].key = key;
        g_tab[g_ntab].names[0] = name;
        g_tab[g_ntab].nnames = 1;
        g_ntab++;
    }
}

/* Encode every opcode in its register, immediate and uniform forms and record the 12-bit
 * opcode field of each word the encoder produces. */
static void build_bw_table(void) {
    if (g_ntab) return;
    for (size_t i = 0; i < sizeof BW_OPS / sizeof BW_OPS[0]; i++) {
        for (int form = 0; form < 4; form++) {
            BlackwellIRInsn in;
            uint32_t w[4];
            memset(&in, 0, sizeof in);
            in.op = BW_OPS[i].op;
            in.dst_vreg = 4;
            in.src1_vreg = 8;
            in.src2_vreg = (form == 0) ? 12 : -1;
            in.src3_vreg = (form == 0) ? 16 : -1;
            in.ureg = (form == 2) ? 4 : -1;
            in.is_uniform = (form == 2);
            in.predicate_p0 = (form == 3);
            in.imm = 1;
            if (omega_blackwell_encode_one(&in, w) == 0) tab_add(w[0] & 0xfffu, BW_OPS[i].name);
        }
    }
}

static const Bw12 *bw_lookup(uint32_t w0) {
    for (unsigned i = 0; i < g_ntab; i++)
        if (g_tab[i].key == (w0 & 0xfffu)) return &g_tab[i];
    return NULL;
}

static int check_gb10(const char *path, const WordFile *wf, int verbose) {
    build_bw_table();
    for (size_t i = 0; i < wf->nw / 4; i++) {
        const uint32_t *w = &wf->w[i * 4];
        const Bw12 *t = bw_lookup(w[0]);
        if (!t) {
            fprintf(stderr,
                    "REFUSED %s:%u: opcode 0x%03x (word 0x%08x%08x%08x%08x) is not emitted by the in-repo sm_121 "
                    "encoder for any BlackwellIROpcode\n",
                    path, wf->line[i], w[0] & 0xfffu, w[3], w[2], w[1], w[0]);
            return -1;
        }
        if (verbose) {
            uint32_t c = w[3];
            printf("  %3zu  %08x%08x %08x%08x  op=0x%03x %-14s stall=%u wb=%u rb=%u wait=0x%02x\n", i, w[1], w[0],
                   w[3], w[2], w[0] & 0xfffu, t->names[0], (c >> 9) & 15u, (c >> 14) & 7u, (c >> 17) & 7u,
                   (c >> 20) & 63u);
        }
    }
    return 0;
}

/* ---- a64 run ------------------------------------------------------------------------- */

static OscRt g_rt;

static int a64_call(const OscNative *nm, const uint64_t *args, unsigned nargs, uint64_t *ret) {
    void *entry = osc_native_at(nm, 0);
    if (!entry) return -1;
    osc_rt_init(&g_rt);
    return osc_rt_call_native(&g_rt, entry, args, nargs, ret);
}

static uint64_t g_rng = 0x9e3779b97f4a7c15ull;
static uint64_t rng(void) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return g_rng;
}

static int run_a64(const char *path, int argc, char **argv) {
    static WordFile wf;
    const char *gate = NULL;
    uint64_t args[6];
    unsigned nargs = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--gate") == 0 && i + 1 < argc) {
            gate = argv[++i];
            continue;
        }
        if (nargs == 6) {
            fprintf(stderr, "omega_bytes: at most 6 integer arguments\n");
            return 2;
        }
        args[nargs++] = strtoull(argv[i], NULL, 0);
    }
    if (read_words(path, 1, &wf) != 0) return 2;
    if (check_a64(path, &wf, 1) != 0) return 3;
    uint8_t bytes[OB_MAX_WORDS * 4];
    char sha[65];
    words_bytes(&wf, bytes);
    sha_hex(bytes, wf.nw * 4, sha);
    OscNative nm;
    int mrc = osc_native_map(&nm, bytes, wf.nw * 4);
    if (mrc != 0) {
        fprintf(stderr, "omega_bytes: osc_native_map failed (%d%s)\n", mrc, mrc == -2 ? ", not an AArch64 host" : "");
        return 4;
    }
    int rc = 0;
    if (!gate) {
        uint64_t r = 0;
        int trc = a64_call(&nm, args, nargs, &r);
        printf("{\"target\":\"a64\",\"file\":\"%s\",\"words\":%zu,\"sha256\":\"%s\",\"args\":[", path, wf.nw, sha);
        for (unsigned i = 0; i < nargs; i++) printf("%s%llu", i ? "," : "", (unsigned long long)args[i]);
        printf("],\"trap\":%d,\"result\":%llu,\"result_hex\":\"0x%016llx\"}\n", trc, (unsigned long long)r,
               (unsigned long long)r);
        rc = trc ? 5 : 0;
    } else if (strcmp(gate, "mul3sub2") == 0) {
        static const uint64_t fixed[] = {0, 1, 2, 3, 0xffffffffffffffffull, 0x5555555555555555ull,
                                         0x8000000000000000ull, 1000000007ull};
        unsigned pass = 0;
        for (unsigned k = 0; k < 16; k++) {
            uint64_t a = k < 8 ? fixed[k] : rng(), r = 0;
            uint64_t want = a * 3u - 2u;
            int trc = a64_call(&nm, &a, 1, &r);
            int ok = trc == 0 && r == want;
            pass += (unsigned)ok;
            printf("  set %2u a=0x%016llx hand=0x%016llx C=0x%016llx %s\n", k, (unsigned long long)a,
                   (unsigned long long)r, (unsigned long long)want, ok ? "ok" : "MISMATCH");
        }
        printf("{\"target\":\"a64\",\"gate\":\"mul3sub2\",\"file\":\"%s\",\"sha256\":\"%s\",\"sets\":16,\"pass\":%u,"
               "\"verdict\":\"%s\"}\n",
               path, sha, pass, pass == 16 ? "PASS" : "FAIL");
        rc = pass == 16 ? 0 : 1;
    } else if (strcmp(gate, "vadd64") == 0) {
        unsigned pass = 0;
        for (unsigned k = 0; k < 16; k++) {
            uint32_t a[64], b[64], out[66], want[64];
            for (unsigned i = 0; i < 64; i++) {
                a[i] = k == 0 ? 0xffffffffu : (uint32_t)rng(); /* set 0 checks the wrap at 2^32 */
                b[i] = k == 0 ? i + 1 : (uint32_t)rng();
                want[i] = a[i] + b[i];
                out[i] = ~want[i]; /* poison: never equal to the right answer */
            }
            out[64] = out[65] = 0xa5a5a5a5u; /* guard words: must stay untouched */
            uint64_t argv3[3] = {(uint64_t)(uintptr_t)a, (uint64_t)(uintptr_t)b, (uint64_t)(uintptr_t)out}, r = 0;
            int trc = a64_call(&nm, argv3, 3, &r);
            int ok = trc == 0 && memcmp(out, want, sizeof want) == 0 && out[64] == 0xa5a5a5a5u &&
                     out[65] == 0xa5a5a5a5u;
            pass += (unsigned)ok;
            printf("  set %2u out[0]=0x%08x out[63]=0x%08x guard=%s %s\n", k, out[0], out[63],
                   out[64] == 0xa5a5a5a5u ? "intact" : "OVERWRITTEN", ok ? "ok" : "MISMATCH");
        }
        printf("{\"target\":\"a64\",\"gate\":\"vadd64\",\"file\":\"%s\",\"sha256\":\"%s\",\"sets\":16,\"pass\":%u,"
               "\"verdict\":\"%s\"}\n",
               path, sha, pass, pass == 16 ? "PASS" : "FAIL");
        rc = pass == 16 ? 0 : 1;
    } else {
        fprintf(stderr, "omega_bytes: unknown gate %s\n", gate);
        rc = 2;
    }
    osc_native_unmap(&nm);
    return rc;
}

/* ---- gb10 run ------------------------------------------------------------------------ */

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

static int run_gb10(const char *path, int argc, char **argv) {
    static WordFile wf;
    uint32_t n = 1048576u;
    unsigned reps = 1;
    const char *expect = "vadd";
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--n") == 0 && i + 1 < argc) n = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (strcmp(argv[i], "--reps") == 0 && i + 1 < argc) reps = (unsigned)strtoul(argv[++i], NULL, 0);
        else if (strcmp(argv[i], "--expect") == 0 && i + 1 < argc) expect = argv[++i];
        else {
            fprintf(stderr, "omega_bytes gb10: unknown argument %s\n", argv[i]);
            return 2;
        }
    }
    int want_unchanged = strcmp(expect, "unchanged") == 0;
    if (!want_unchanged && strcmp(expect, "vadd") != 0) {
        fprintf(stderr, "omega_bytes gb10: --expect vadd|unchanged\n");
        return 2;
    }
    if (n == 0 || reps == 0 || reps > 1000) return 2;
    if (read_words(path, 4, &wf) != 0) return 2;
    if (check_gb10(path, &wf, 1) != 0) return 3;
    static uint8_t code[OB_MAX_WORDS * 16];
    char sha[65];
    words_bytes(&wf, code);
    sha_hex(code, wf.nw * 4, sha);

    uint32_t *a = malloc((size_t)n * 4), *b = malloc((size_t)n * 4), *out = malloc((size_t)n * 4),
             *want = malloc((size_t)n * 4), *poison = malloc((size_t)n * 4);
    uint64_t *ns = calloc(reps, sizeof *ns);
    if (!a || !b || !out || !want || !poison || !ns) return 4;
    for (uint32_t i = 0; i < n; i++) {
        a[i] = (uint32_t)rng();
        b[i] = (uint32_t)rng();
        want[i] = a[i] + b[i]; /* CPU reference, mod 2^32 */
        poison[i] = ~want[i];  /* never equal to the right answer */
    }
    omega_gpu_engine_set_backend(omega_blackwell_engine_backend());
    unsigned ok_reps = 0;
    size_t first_bad_mism = 0;
    int first_fail = 0;
    for (unsigned r = 0; r < reps; r++) {
        OmegaGpuJob job;
        OmegaGpuResult res;
        memset(&job, 0, sizeof job);
        memset(out, 0, (size_t)n * 4);
        job.program = code;
        job.program_len = wf.nw * 4;
        job.layout = OMEGA_GPU_LAYOUT_VECTOR_1D;
        job.element_count = n;
        job.input_a = a;
        job.input_a_len = (size_t)n * 4;
        job.input_b = b;
        job.input_b_len = (size_t)n * 4;
        job.output = out;
        job.output_len = (size_t)n * 4;
        job.poison = poison;
        job.poison_count = n;
        job.timeouts.marker_ms = 5000;
        job.timeouts.release_semaphore_ms = 5000;
        job.timeouts.marker2_ms = 5000;
        job.timeouts.visibility_ms = 5000;
        int erc = omega_gpu_execute(&job, &res);
        OmegaBlackwellEngineRunInfo info;
        omega_blackwell_engine_run_info(&info);
        ns[r] = (info.launch_ns && info.marker_done_ns > info.launch_ns) ? info.marker_done_ns - info.launch_ns : 0;
        size_t mism = 0;
        if (erc == OMEGA_GPU_ENGINE_OK)
            for (uint32_t i = 0; i < n; i++) mism += out[i] != want[i];
        int ok = want_unchanged ? (erc == OMEGA_GPU_ENGINE_OUTPUT_UNCHANGED && res.output_unchanged_words[0] == n)
                                : (erc == OMEGA_GPU_ENGINE_OK && mism == 0);
        fprintf(stderr, "  rep %u: engine=%s step=%s drv_rc=%d errno=%d drv_text=\"%s\" mismatches=%zu "
                "launch_to_done_ns=%llu %s\n", r,
                omega_gpu_engine_failure_name(res.failure), omega_gpu_engine_step_name(res.failed_step), res.drv_rc,
                res.err_no, res.drv_text, mism, (unsigned long long)ns[r], ok ? "ok" : "FAIL");
        if (ok) ok_reps++;
        else if (!first_fail) {
            first_fail = res.failure ? res.failure : -1;
            first_bad_mism = mism;
        }
        if (res.retained) break; /* uncertain completion: the engine blocks further runs */
    }
    uint64_t *sorted = calloc(reps, sizeof *sorted);
    if (!sorted) return 4;
    memcpy(sorted, ns, reps * sizeof *ns);
    qsort(sorted, reps, sizeof *sorted, cmp_u64);
    double bytes = 3.0 * 4.0 * (double)n;
    uint64_t med = sorted[reps / 2], min = sorted[0];
    printf("{\"target\":\"gb10\",\"file\":\"%s\",\"instructions\":%zu,\"sha256\":\"%s\",\"n\":%u,\"expect\":\"%s\","
           "\"reps\":%u,\"reps_ok\":%u,\"first_fail\":\"%s\",\"first_fail_mismatches\":%zu,"
           "\"bytes_moved\":%.0f,\"launch_to_done_ns\":{\"min\":%llu,\"median\":%llu,\"all\":[",
           path, wf.nw / 4, sha, n, expect, reps, ok_reps,
           first_fail > 0 ? omega_gpu_engine_failure_name(first_fail) : (first_fail ? "WRONG_RESULT" : "none"),
           first_bad_mism, bytes, (unsigned long long)min, (unsigned long long)med);
    for (unsigned r = 0; r < reps; r++) printf("%s%llu", r ? "," : "", (unsigned long long)ns[r]);
    printf("]},\"verdict\":\"%s\"}\n", ok_reps == reps ? "PASS" : "FAIL");
    free(a), free(b), free(out), free(want), free(poison), free(ns), free(sorted);
    return ok_reps == reps ? 0 : 1;
}

/* ---- CPU reference: the same op compiled from C by gcc --------------------------------- */

static uint64_t now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

__attribute__((noinline)) static void c_vadd(const uint32_t *restrict a, const uint32_t *restrict b,
                                             uint32_t *restrict c, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) c[i] = a[i] + b[i];
}

static int run_cpu_vadd(int argc, char **argv) {
    uint32_t n = 1048576u;
    unsigned reps = 21;
    for (int i = 0; i + 1 < argc; i += 2) {
        if (strcmp(argv[i], "--n") == 0) n = (uint32_t)strtoul(argv[i + 1], NULL, 0);
        else if (strcmp(argv[i], "--reps") == 0) reps = (unsigned)strtoul(argv[i + 1], NULL, 0);
    }
    if (n == 0 || reps == 0 || reps > 1000) return 2;
    /* Cold-memory pass: before each timed pass, stream 256 MiB through the caches so the
     * three arrays are not already cache-resident (the GPU runs see freshly copied inputs). */
    size_t evn = (256u << 20) / 8;
    uint64_t *ev = malloc(evn * 8);
    uint32_t *a = malloc((size_t)n * 4), *b = malloc((size_t)n * 4), *c = malloc((size_t)n * 4);
    uint64_t *ns = calloc(reps, sizeof *ns);
    if (!ev || !a || !b || !c || !ns) return 4;
    for (uint32_t i = 0; i < n; i++) a[i] = (uint32_t)rng(), b[i] = (uint32_t)rng(), c[i] = 0;
    uint64_t sink = 0;
    for (unsigned r = 0; r < reps; r++) {
        for (size_t i = 0; i < evn; i++) ev[i] = i + r;
        for (size_t i = 0; i < evn; i += 512) sink += ev[i];
        uint64_t t0 = now_ns();
        c_vadd(a, b, c, n);
        uint64_t t1 = now_ns();
        ns[r] = t1 - t0;
    }
    size_t bad = 0;
    for (uint32_t i = 0; i < n; i++) bad += c[i] != a[i] + b[i];
    uint64_t *s = calloc(reps, sizeof *s);
    if (!s) return 4;
    memcpy(s, ns, reps * sizeof *ns);
    qsort(s, reps, sizeof *s, cmp_u64);
    printf("{\"target\":\"cpu-c\",\"compiler\":\"gcc %s\",\"n\":%u,\"reps\":%u,\"mismatches\":%zu,\"bytes_moved\":%.0f,"
           "\"pass_ns\":{\"min\":%llu,\"median\":%llu,\"all\":[",
           __VERSION__, n, reps, bad, 12.0 * n, (unsigned long long)s[0], (unsigned long long)s[reps / 2]);
    for (unsigned r = 0; r < reps; r++) printf("%s%llu", r ? "," : "", (unsigned long long)ns[r]);
    printf("]},\"sink\":%llu,\"verdict\":\"%s\"}\n", (unsigned long long)(sink & 1), bad ? "FAIL" : "PASS");
    free(ev), free(a), free(b), free(c), free(ns), free(s);
    return bad ? 1 : 0;
}

/* ---- the existing encoder's words, as a word file ------------------------------------ */

static int emit_encoder_vecadd(void) {
    uint8_t code[OMEGA_BW_VECADD_CODE_SIZE];
    size_t len = 0;
    if (omega_blackwell_encode_vecadd(code, sizeof code, &len) != 0 || len % 16) return 1;
    printf("# gb10 vector add as emitted by the existing in-repo encoder\n"
           "# (omega_blackwell_encode_vecadd, src/omega_blackwell_encoder.c). Generated by\n"
           "# `omega_bytes emit-encoder-vecadd`; do not edit by hand.\n");
    for (size_t i = 0; i < len; i += 16) {
        uint64_t lo = 0, hi = 0;
        for (int k = 7; k >= 0; k--) {
            lo = (lo << 8) | code[i + (size_t)k];
            hi = (hi << 8) | code[i + 8 + (size_t)k];
        }
        printf("0x%016llx 0x%016llx\n", (unsigned long long)lo, (unsigned long long)hi);
    }
    return 0;
}

static void usage(void) {
    fprintf(stderr,
            "usage: omega_bytes a64 FILE [ARG...] | a64 FILE --gate mul3sub2|vadd64\n"
            "       omega_bytes gb10 FILE [--n N] [--reps R] [--expect vadd|unchanged]\n"
            "       omega_bytes check a64|gb10 FILE | names | emit-encoder-vecadd | cpu-vadd [--n N] [--reps R]\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return 2;
    }
    if (strcmp(argv[1], "a64") == 0 && argc >= 3) return run_a64(argv[2], argc - 3, argv + 3);
    if (strcmp(argv[1], "gb10") == 0 && argc >= 3) return run_gb10(argv[2], argc - 3, argv + 3);
    if (strcmp(argv[1], "check") == 0 && argc == 4) {
        static WordFile wf;
        int g = strcmp(argv[2], "gb10") == 0;
        if (!g && strcmp(argv[2], "a64") != 0) {
            usage();
            return 2;
        }
        if (read_words(argv[3], g ? 4 : 1, &wf) != 0) return 2;
        int rc = g ? check_gb10(argv[3], &wf, 1) : check_a64(argv[3], &wf, 1);
        printf("%s %s: %s\n", argv[2], argv[3], rc ? "REFUSED" : "every word named");
        return rc ? 3 : 0;
    }
    if (strcmp(argv[1], "names") == 0) {
        build_bw_table();
        for (unsigned i = 0; i < g_ntab; i++) {
            printf("0x%03x", g_tab[i].key);
            for (unsigned j = 0; j < g_tab[i].nnames; j++) printf(" %s", g_tab[i].names[j]);
            printf("\n");
        }
        return 0;
    }
    if (strcmp(argv[1], "emit-encoder-vecadd") == 0) return emit_encoder_vecadd();
    if (strcmp(argv[1], "cpu-vadd") == 0) return run_cpu_vadd(argc - 2, argv + 2);
    usage();
    return 2;
}
