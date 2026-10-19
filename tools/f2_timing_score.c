/*
 * f2_timing_score: Turing scoring of the FREQUENCY timing model for the attn256 decode kernel (omega-mv#570).
 * Profile: docs/turing/F2_FREQUENCY_TIMING_PROFILE_V0.md (frozen before the held-out set was collected).
 *
 * Input: TSV traces from tests/gpu_attn256_freq_test --chip-traces, one launch per line:
 *   tag class seed pos splits mode rep elapsed_ns call_ns steps rescales out_fnv
 * Only mode 0 lines (the default program) are modelled; mode 1 lines are used by `bits` and `modes`.
 *
 *   f2_timing_score fit TRAIN.tsv MODEL.txt
 *   f2_timing_score score MODEL.txt HELD.tsv [--classes a,b] [--shuffle SEED]
 *   f2_timing_score bits FILE.tsv          every (tag, class, seed, pos) group: all launches, both modes, same output bits
 *   f2_timing_score modes FILE.tsv         per (class, pos): median elapsed of mode 0 and mode 1
 *
 * Models (integers only after the fit; lengths through src/turing/ty_math.h):
 *   B (instruction count): t = mu_B[pos]. At fixed branch odds the instruction count is a function of pos alone, so a
 *     free value per pos is the most a pure instruction-count model can say.
 *   M (frequency): t = mu_M[pos] + g[pos] * R / S, R / S = the share of V steps that took the rescale path, from the
 *     host replica of the branch (a pure function of the inputs; the chip counters matched it, chip-check.log).
 *   Data code: r = round((t - pred) / 1000 ns), zigzag, Rice code with parameter k (fitted on TRAIN, 5 bits).
 *   L(B) = 5 + 8 + npos * (32 + 32) bits, L(M) = L(B) + npos * 32 bits.
 *   T(M; B, D) = [L(B) + L(D|B)] - [L(M) + L(D|M)] in bits (ty_gain, micro-bit exact).
 */
#include "turing/ty_math.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W_NS 1000
#define MAXP 16
typedef struct { char tag[32], cls[16]; uint32_t seed, pos, splits, mode, rep; int64_t el; uint64_t S, R; char fnv[17]; } Line;
typedef struct { uint32_t npos, pos[MAXP]; int64_t muB[MAXP], muM[MAXP], g[MAXP]; uint32_t kB, kM; } Model;

static Line *load(const char *path, size_t *n) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "cannot read %s\n", path); exit(2); }
    size_t cap = 1024; Line *v = malloc(cap * sizeof *v); *n = 0;
    char buf[512];
    while (fgets(buf, sizeof buf, f)) {
        Line L; memset(&L, 0, sizeof L); uint64_t call;
        if (sscanf(buf, "%31s %15s %u %u %u %u %u %" SCNd64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %16s", L.tag, L.cls, &L.seed, &L.pos, &L.splits, &L.mode, &L.rep, &L.el, &call, &L.S, &L.R, L.fnv) != 12 || L.S == 0) {
            fprintf(stderr, "malformed line in %s: %s", path, buf); exit(2);
        }
        if (*n == cap) { cap *= 2; v = realloc(v, cap * sizeof *v); }
        v[(*n)++] = L;
    }
    fclose(f);
    return v;
}
static int in_list(const char *s, const char *list) {
    if (!list) return 1;
    size_t n = strlen(s);
    for (const char *p = list; *p;) { const char *e = strchr(p, ','); size_t m = e ? (size_t)(e - p) : strlen(p); if (m == n && strncmp(p, s, n) == 0) return 1; if (!e) break; p = e + 1; }
    return 0;
}
static int pidx(const Model *m, uint32_t pos) { for (uint32_t i = 0; i < m->npos; i++) if (m->pos[i] == pos) return (int)i; return -1; }
static int64_t div_round(__int128 a, __int128 b) { /* round half away from zero, b > 0 */ return (int64_t)(a >= 0 ? (2 * a + b) / (2 * b) : -((-2 * a + b) / (2 * b))); }
static int64_t predict(const Model *m, int M, int i, uint64_t S, uint64_t R) {
    return M ? m->muM[i] + div_round((__int128)m->g[i] * (__int128)R, (__int128)S) : m->muB[i];
}
static uint64_t zz(int64_t r) { return r >= 0 ? (uint64_t)r * 2u : (uint64_t)(-r) * 2u - 1u; }
static uint64_t rice_bits(uint64_t z, uint32_t k) { return (z >> k) + 1u + k; }
static uint64_t model_bits(const Model *m, int M) { return 5u + 8u + (uint64_t)m->npos * 64u + (M ? (uint64_t)m->npos * 32u : 0u); }

static int fit(const char *train, const char *out) {
    size_t n; Line *v = load(train, &n);
    Model m; memset(&m, 0, sizeof m);
    for (size_t j = 0; j < n; j++) if (v[j].mode == 0 && pidx(&m, v[j].pos) < 0) { if (m.npos == MAXP) return 2; m.pos[m.npos++] = v[j].pos; }
    for (uint32_t i = 0; i < m.npos; i++) {
        double c = 0, st = 0, sf = 0, sff = 0, sft = 0;
        for (size_t j = 0; j < n; j++) if (v[j].mode == 0 && v[j].pos == m.pos[i]) { double f = (double)v[j].R / (double)v[j].S, t = (double)v[j].el; c++; st += t; sf += f; sff += f * f; sft += f * t; }
        double mt = st / c, mf = sf / c, var = sff / c - mf * mf, cov = sft / c - mf * mt;
        double g = var > 1e-12 ? cov / var : 0.0;
        m.muB[i] = llround(mt); m.g[i] = llround(g); m.muM[i] = llround(mt - g * mf);
    }
    for (int M = 0; M < 2; M++) {
        uint64_t best = UINT64_MAX; uint32_t bk = 0;
        for (uint32_t k = 0; k < 32; k++) {
            uint64_t tot = 0;
            for (size_t j = 0; j < n; j++) if (v[j].mode == 0) { int i = pidx(&m, v[j].pos); tot += rice_bits(zz(div_round((__int128)v[j].el - predict(&m, M, i, v[j].S, v[j].R), W_NS)), k); }
            if (tot < best) { best = tot; bk = k; }
        }
        if (M) m.kM = bk; else m.kB = bk;
    }
    FILE *f = fopen(out, "w");
    if (!f) return 2;
    fprintf(f, "F2_TIMING_MODEL_V0 w_ns %d kB %u kM %u npos %u\n", W_NS, m.kB, m.kM, m.npos);
    for (uint32_t i = 0; i < m.npos; i++) fprintf(f, "pos %u muB %" PRId64 " muM %" PRId64 " g %" PRId64 "\n", m.pos[i], m.muB[i], m.muM[i], m.g[i]);
    fclose(f);
    printf("fit: %zu lines, %u positions, kB %u kM %u -> %s\n", n, m.npos, m.kB, m.kM, out);
    for (uint32_t i = 0; i < m.npos; i++) printf("  pos %6u  B: %" PRId64 " ns   M: %" PRId64 " + %" PRId64 " * share ns\n", m.pos[i], m.muB[i], m.muM[i], m.g[i]);
    free(v);
    return 0;
}
static void read_model(const char *path, Model *m) {
    FILE *f = fopen(path, "r"); int w = 0;
    memset(m, 0, sizeof *m);
    if (!f || fscanf(f, "F2_TIMING_MODEL_V0 w_ns %d kB %u kM %u npos %u", &w, &m->kB, &m->kM, &m->npos) != 4 || w != W_NS || m->npos > MAXP) { fprintf(stderr, "bad model %s\n", path); exit(2); }
    for (uint32_t i = 0; i < m->npos; i++)
        if (fscanf(f, " pos %u muB %" SCNd64 " muM %" SCNd64 " g %" SCNd64, &m->pos[i], &m->muB[i], &m->muM[i], &m->g[i]) != 4) { fprintf(stderr, "bad model row\n"); exit(2); }
    fclose(f);
}
static uint32_t lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s; }
static int score(const char *mpath, const char *held, const char *classes, int shuffle, uint32_t seed) {
    Model m; read_model(mpath, &m);
    size_t n; Line *v = load(held, &n);
    size_t *idx = malloc(n * sizeof *idx), k = 0;
    for (size_t j = 0; j < n; j++) if (v[j].mode == 0 && in_list(v[j].cls, classes)) {
        if (pidx(&m, v[j].pos) < 0) { fprintf(stderr, "position %u not in the model: refused\n", v[j].pos); return 2; }
        idx[k++] = j;
    }
    if (shuffle) { /* negative control: permute (S, R) among the selected launches of the same position */
        for (uint32_t i = 0; i < m.npos; i++) {
            size_t c = 0, *g = malloc(k * sizeof *g);
            for (size_t a = 0; a < k; a++) if (v[idx[a]].pos == m.pos[i]) g[c++] = idx[a];
            for (size_t a = c; a > 1; a--) { size_t b = lcg(&seed) % a; uint64_t S = v[g[a - 1]].S, R = v[g[a - 1]].R; v[g[a - 1]].S = v[g[b]].S; v[g[a - 1]].R = v[g[b]].R; v[g[b]].S = S; v[g[b]].R = R; }
            free(g);
        }
    }
    int64_t ldb = 0, ldm = 0, dlb, dlm, T; uint64_t bitsB = 0, bitsM = 0; double sab = 0, sam = 0;
    for (size_t a = 0; a < k; a++) {
        const Line *L = &v[idx[a]]; int i = pidx(&m, L->pos);
        int64_t rb = div_round((__int128)L->el - predict(&m, 0, i, L->S, L->R), W_NS), rm = div_round((__int128)L->el - predict(&m, 1, i, L->S, L->R), W_NS);
        bitsB += rice_bits(zz(rb), m.kB); bitsM += rice_bits(zz(rm), m.kM);
        sab += rb < 0 ? -rb : rb; sam += rm < 0 ? -rm : rm;
    }
    int64_t ub;
    if (ty_bits_to_ub(bitsB, &ub) != TY_OK) return 3;
    ldb = ub;
    if (ty_bits_to_ub(bitsM, &ub) != TY_OK) return 3;
    ldm = ub;
    if (ty_dl(model_bits(&m, 0), ldb, &dlb) != TY_OK || ty_dl(model_bits(&m, 1), ldm, &dlm) != TY_OK || ty_gain(dlb, dlm, &T) != TY_OK) return 3;
    printf("score %s classes=%s shuffle=%s: launches %zu\n", held, classes ? classes : "all", shuffle ? "yes" : "no", k);
    printf("  L(B) %" PRIu64 " bits  L(D|B) %" PRIu64 " bits  (mean |r| %.2f us)\n", model_bits(&m, 0), bitsB, k ? sab / (double)k : 0.0);
    printf("  L(M) %" PRIu64 " bits  L(D|M) %" PRIu64 " bits  (mean |r| %.2f us)\n", model_bits(&m, 1), bitsM, k ? sam / (double)k : 0.0);
    printf("  T(M; B, D) = %" PRId64 " ub = %.6f bits  (%.4f bits per launch)\n", T, (double)T / 1e6, k ? (double)T / 1e6 / (double)k : 0.0);
    free(v); free(idx);
    return 0;
}
static int bits(const char *path) {
    size_t n; Line *v = load(path, &n); size_t groups = 0, bad = 0;
    for (size_t a = 0; a < n; a++) {
        int first = 1;
        for (size_t b = 0; b < a; b++) if (!strcmp(v[a].tag, v[b].tag) && !strcmp(v[a].cls, v[b].cls) && v[a].seed == v[b].seed && v[a].pos == v[b].pos) { first = 0; if (strcmp(v[a].fnv, v[b].fnv)) bad++; break; }
        groups += first;
    }
    printf("bits %s: %zu launches in %zu groups, %zu differ from their group's first launch\n", path, n, groups, bad);
    free(v);
    return bad ? 1 : 0;
}
static int cmp_i64(const void *a, const void *b) { int64_t x = *(const int64_t *)a, y = *(const int64_t *)b; return x < y ? -1 : x > y; }
static int modes(const char *path) {
    size_t n; Line *v = load(path, &n);
    int64_t *t = malloc(n * sizeof *t);
    for (size_t a = 0; a < n; a++) {
        int seen = 0;
        for (size_t b = 0; b < a && !seen; b++) seen = !strcmp(v[a].cls, v[b].cls) && v[a].pos == v[b].pos;
        if (seen) continue;
        double med[2] = { 0, 0 }; size_t cnt[2] = { 0, 0 }; double share = (double)v[a].R / (double)v[a].S;
        for (int md = 0; md < 2; md++) {
            size_t c = 0;
            for (size_t b = 0; b < n; b++) if (!strcmp(v[a].cls, v[b].cls) && v[a].pos == v[b].pos && v[b].mode == (uint32_t)md) t[c++] = v[b].el;
            qsort(t, c, sizeof *t, cmp_i64);
            cnt[md] = c; med[md] = c ? (c % 2 ? (double)t[c / 2] : 0.5 * (double)(t[c / 2 - 1] + t[c / 2])) : 0;
        }
        printf("%-8s pos %6u share %.4f  mode0 median %.1f us (n %zu)  mode1 median %.1f us (n %zu)  mode1-mode0 %+.1f us\n", v[a].cls, v[a].pos, share,
               med[0] / 1e3, cnt[0], med[1] / 1e3, cnt[1], (med[1] - med[0]) / 1e3);
    }
    free(t); free(v);
    return 0;
}
int main(int argc, char **argv) {
    if (argc >= 4 && !strcmp(argv[1], "fit")) return fit(argv[2], argv[3]);
    if (argc >= 4 && !strcmp(argv[1], "score")) {
        const char *classes = NULL; int sh = 0; uint32_t seed = 0;
        for (int i = 4; i < argc; i++) {
            if (!strcmp(argv[i], "--classes") && i + 1 < argc) classes = argv[++i];
            else if (!strcmp(argv[i], "--shuffle") && i + 1 < argc) { sh = 1; seed = (uint32_t)strtoul(argv[++i], NULL, 10); }
            else { fprintf(stderr, "unknown argument %s\n", argv[i]); return 2; }
        }
        return score(argv[2], argv[3], classes, sh, seed);
    }
    if (argc >= 3 && !strcmp(argv[1], "bits")) return bits(argv[2]);
    if (argc >= 3 && !strcmp(argv[1], "modes")) return modes(argv[2]);
    fprintf(stderr, "usage: f2_timing_score fit TRAIN MODEL | score MODEL HELD [--classes a,b] [--shuffle SEED] | bits FILE | modes FILE\n");
    return 2;
}
