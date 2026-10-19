/*
 * lifetime_test: host checks for omega_lifetime (FORTRAN idea 1, EQUIVALENCE; omega#569).
 *   hand cases with known answers (sharing, an op that reads one buffer while it writes another, LIVE_IN / LIVE_OUT /
 *   PINNED, a TAIL glued to its head), a random property sweep (every plan passes both rules, never needs fewer bytes than
 *   the busiest step, never more than the unshared layout) and the negative controls: two buffers live at the same time
 *   forced into one slot, a planner that ignores where a lifetime ends, a read before any write. Each must go red.
 */
#include "../src/omega_lifetime.h"
#include "../src/omega_gpu_q38_graph.h"
#include <stdio.h>
#include <string.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static OmegaLtAccess acc[4096];
static const char *names[1024];

static void chain(void) {
    /* A written at 0 read at 1; B written at 1 read at 2; C written at 2 read at 3: A and C may share, B may not join either */
    OmegaLt t; omega_lt_init(&t, acc, 4096, names, 1024);
    int A = omega_lt_buf(&t, "A", 1000, 256, 0), B = omega_lt_buf(&t, "B", 1000, 256, 0), C = omega_lt_buf(&t, "C", 1000, 256, 0);
    omega_lt_op(&t, "wA"); omega_lt_use(&t, A, 1);
    omega_lt_op(&t, "A->B"); omega_lt_use(&t, A, 0); omega_lt_use(&t, B, 1);
    omega_lt_op(&t, "B->C"); omega_lt_use(&t, B, 0); omega_lt_use(&t, C, 1);
    omega_lt_op(&t, "rC"); omega_lt_use(&t, C, 0);
    CHECK(omega_lt_live(&t) == 0, "live");
    CHECK(t.buf[A].first == 0 && t.buf[A].last == 1 && t.buf[C].first == 2 && t.buf[C].last == 3, "intervals");
    uint64_t off[OMEGA_LT_MAX_BUFS], arena = 0, flat[OMEGA_LT_MAX_BUFS], farena = 0;
    char why[256];
    CHECK(omega_lt_plan(&t, off, &arena) == 0 && omega_lt_check(&t, off, why, sizeof why) == 0, "chain plan safe: %s", why);
    CHECK(off[A] == off[C] && off[B] != off[A], "A and C share, B apart (A %llu B %llu C %llu)", (unsigned long long)off[A],
          (unsigned long long)off[B], (unsigned long long)off[C]);
    CHECK(arena == 2048, "arena %llu (2 slots of 1024)", (unsigned long long)arena);
    CHECK(omega_lt_peak_live(&t) == 2000, "peak live %llu", (unsigned long long)omega_lt_peak_live(&t));
    CHECK(omega_lt_plan_flat(&t, flat, &farena) == 0 && farena == 3072 && omega_lt_check(&t, flat, why, sizeof why) == 0, "flat %llu", (unsigned long long)farena);
    CHECK(omega_lt_valid_at_end(&t, off, C) == 1 && omega_lt_valid_at_end(&t, off, A) == 0, "C intact at the end, A overwritten by C");
    /* negative control 1: B forced onto A's bytes (live together at step 1) */
    uint64_t bad[OMEGA_LT_MAX_BUFS]; memcpy(bad, off, sizeof bad); bad[B] = off[A];
    CHECK(omega_lt_check(&t, bad, why, sizeof why) != 0 && strstr(why, "share bytes"), "forced overlap must be red: '%s'", why);
    printf("negative control (forced share A,B): RED as required: %s\n", why);
    char map[2048];
    CHECK(omega_lt_map(&t, off, map, sizeof map) > 0 && strstr(map, "A->B x1") && strstr(map, "slot 1"), "map");
}

static void outputs_and_tail(void) {
    OmegaLt t; omega_lt_init(&t, acc, 4096, names, 1024);
    int X = omega_lt_buf(&t, "x", 512, 256, OMEGA_LT_LIVE_IN | OMEGA_LT_LIVE_OUT);
    int T = omega_lt_buf(&t, "tmp", 4096, 256, 0);
    int L = omega_lt_buf(&t, "logits", 3840, 256, OMEGA_LT_LIVE_OUT);
    int P = omega_lt_buf(&t, "logits_pad", 256, 4, OMEGA_LT_TAIL | OMEGA_LT_PINNED);
    int K = omega_lt_buf(&t, "keep", 256, 256, OMEGA_LT_PINNED);
    omega_lt_op(&t, "mlp"); omega_lt_use(&t, X, 0); omega_lt_use(&t, T, 1);
    omega_lt_op(&t, "res"); omega_lt_use(&t, T, 0); omega_lt_use(&t, X, 0); omega_lt_use(&t, X, 1);
    omega_lt_op(&t, "head"); omega_lt_use(&t, X, 0); omega_lt_use(&t, L, 1);
    omega_lt_op(&t, "argmax"); omega_lt_use(&t, L, 0); omega_lt_use(&t, P, 0); omega_lt_use(&t, K, 0);
    CHECK(omega_lt_live(&t) == 0, "live");
    CHECK(t.buf[X].first == 0 && t.buf[X].last == 4 && t.buf[L].last == 4 && t.buf[P].first == 0 && t.buf[K].last == 4, "flag intervals");
    uint64_t off[OMEGA_LT_MAX_BUFS], arena = 0, flat[OMEGA_LT_MAX_BUFS], farena = 0;
    char why[256];
    CHECK(omega_lt_plan(&t, off, &arena) == 0 && omega_lt_check(&t, off, why, sizeof why) == 0, "safe: %s", why);
    CHECK(off[P] == off[L] + 3840, "tail glued (logits %llu pad %llu)", (unsigned long long)off[L], (unsigned long long)off[P]);
    CHECK(off[L] < off[T] + 4096 && off[T] < off[L] + 3840, "logits shares tmp's bytes (dead after res)");
    CHECK(omega_lt_plan_flat(&t, flat, &farena) == 0 && arena < farena, "shared %llu < flat %llu", (unsigned long long)arena, (unsigned long long)farena);
    /* negative control 3: the pad (pinned) forced under tmp: replay finds it overwritten */
    uint64_t bad[OMEGA_LT_MAX_BUFS]; memcpy(bad, off, sizeof bad);
    bad[L] = off[T]; bad[P] = off[T] + 3840;
    CHECK(omega_lt_check(&t, bad, why, sizeof why) != 0, "pinned pad under tmp must be red: '%s'", why);
}

static void same_op_read_write(void) {
    OmegaLt t; omega_lt_init(&t, acc, 4096, names, 1024);
    int A = omega_lt_buf(&t, "in", 64, 64, 0), B = omega_lt_buf(&t, "out", 64, 64, 0);
    omega_lt_op(&t, "w"); omega_lt_use(&t, A, 1);
    omega_lt_op(&t, "k"); omega_lt_use(&t, A, 0); omega_lt_use(&t, B, 1);
    omega_lt_op(&t, "r"); omega_lt_use(&t, B, 0);
    omega_lt_live(&t);
    uint64_t off[OMEGA_LT_MAX_BUFS], arena = 0; char why[256];
    CHECK(omega_lt_plan(&t, off, &arena) == 0 && off[A] != off[B] && omega_lt_check(&t, off, why, sizeof why) == 0, "one op reading in, writing out: apart");
    /* negative control 4: read before any write (a host write not declared LIVE_IN) is red even with a safe layout */
    OmegaLt u; omega_lt_init(&u, acc, 4096, names, 1024);
    int C = omega_lt_buf(&u, "x", 64, 64, 0);
    omega_lt_op(&u, "r"); omega_lt_use(&u, C, 0);
    omega_lt_live(&u);
    CHECK(omega_lt_plan(&u, off, &arena) == 0 && omega_lt_check(&u, off, why, sizeof why) != 0 && strstr(why, "before any write"), "read before write red: '%s'", why);
}

/* independent oracle: every byte of the arena remembers which buffer last wrote it. Unsafe when a read finds a byte owned by
 * another buffer, an op writes bytes another buffer read in the same op, two kept-from-before buffers start on the same
 * bytes, or a kept-after buffer has lost a byte at the end. Shares no code with omega_lifetime's check. */
static uint8_t own[1 << 20];
static int ever_written(const OmegaLt *t, uint32_t i) {
    for (uint32_t k = 0; k < t->n_acc; k++) if (t->acc[k].buf == i && t->acc[k].write) return 1;
    return 0;
}
static int oracle_unsafe(const OmegaLt *t, const uint64_t *off, uint64_t arena) {
    if (arena > sizeof own) return -1;
    memset(own, 0xff, arena);
    for (uint32_t i = 0; i < t->n_buf; i++)
        if ((t->buf[i].flags & (OMEGA_LT_LIVE_IN | OMEGA_LT_PINNED)) || ((t->buf[i].flags & OMEGA_LT_LIVE_OUT) && !ever_written(t, i)))
            for (uint64_t b = off[i]; b < off[i] + t->buf[i].bytes; b++) { if (own[b] != 0xff) return 1; own[b] = (uint8_t)i; }
    for (uint32_t k = 0; k < t->n_acc;) {
        uint32_t e = k;
        while (e < t->n_acc && t->acc[e].step == t->acc[k].step) e++;
        for (uint32_t j = k; j < e; j++) if (!t->acc[j].write) {
            const uint32_t i = t->acc[j].buf;
            for (uint64_t b = off[i]; b < off[i] + t->buf[i].bytes; b++) if (own[b] != i) return 1;
        }
        for (uint32_t j = k; j < e; j++) if (t->acc[j].write)
            for (uint32_t r = k; r < e; r++) if (!t->acc[r].write && t->acc[r].buf != t->acc[j].buf) {
                const uint32_t a = t->acc[j].buf, c = t->acc[r].buf;
                if (off[a] < off[c] + t->buf[c].bytes && off[c] < off[a] + t->buf[a].bytes) return 1;
            }
        for (uint32_t j = k; j < e; j++) if (t->acc[j].write) {
            const uint32_t i = t->acc[j].buf;
            for (uint64_t b = off[i]; b < off[i] + t->buf[i].bytes; b++) own[b] = (uint8_t)i;
        }
        k = e;
    }
    for (uint32_t i = 0; i < t->n_buf; i++)
        if (t->buf[i].flags & (OMEGA_LT_LIVE_OUT | OMEGA_LT_PINNED))
            for (uint64_t b = off[i]; b < off[i] + t->buf[i].bytes; b++) if (own[b] != i) return 1;
    return 0;
}

/* a planner that forgets where lifetimes end (treats each buffer as live at its first step only): must be caught */
static void mutant_planner(OmegaLt *t, uint64_t *off, uint64_t *arena) {
    OmegaLt m = *t;
    for (uint32_t i = 0; i < m.n_buf; i++) if (m.buf[i].first != OMEGA_LT_NONE) m.buf[i].last = m.buf[i].first;
    omega_lt_plan(&m, off, arena);
}

static uint64_t rng = 0x9e3779b97f4a7c15ull;
static uint32_t rnd(uint32_t n) { rng = rng * 6364136223846793005ull + 1442695040888963407ull; return (uint32_t)(rng >> 33) % n; }

static void random_sweep(void) {
    uint32_t mutant_caught = 0, mutant_cases = 0, conservative = 0;
    for (int it = 0; it < 3000; it++) {
        OmegaLt t; omega_lt_init(&t, acc, 4096, names, 1024);
        const uint32_t nb = 2 + rnd(20), nops = 1 + rnd(40);
        int id[OMEGA_LT_MAX_BUFS]; uint8_t w[OMEGA_LT_MAX_BUFS];
        for (uint32_t b = 0; b < nb; b++) {
            uint32_t fl = 0, r = rnd(10);
            if (r == 0) fl = OMEGA_LT_LIVE_IN; else if (r == 1) fl = OMEGA_LT_LIVE_OUT; else if (r == 2) fl = OMEGA_LT_PINNED;
            const uint32_t al = 1u << rnd(6);
            uint64_t bytes = (uint64_t)(1 + rnd(64)) * al;
            if (b > 0 && rnd(8) == 0 && t.buf[b - 1].bytes % 16 == 0) { fl |= OMEGA_LT_TAIL; bytes = 16u * (1 + rnd(64)); id[b] = omega_lt_buf(&t, "tail", bytes, 16, fl); }
            else id[b] = omega_lt_buf(&t, "b", bytes, al, fl);
            w[b] = (fl & (OMEGA_LT_LIVE_IN | OMEGA_LT_PINNED)) ? 1 : 0;
            if (id[b] < 0) { printf("FAIL buf %u\n", b); fails++; return; }
        }
        for (uint32_t s = 0; s < nops; s++) {
            omega_lt_op(&t, (s & 1) ? "odd" : "even");
            const uint32_t nr = rnd(3), nw = 1 + rnd(2);
            for (uint32_t k = 0; k < nr; k++) { uint32_t b = rnd(nb); if (w[b]) omega_lt_use(&t, id[b], 0); }
            for (uint32_t k = 0; k < nw; k++) { uint32_t b = rnd(nb); omega_lt_use(&t, id[b], 1); w[b] = 1; }
        }
        omega_lt_live(&t);
        uint64_t off[OMEGA_LT_MAX_BUFS], arena = 0, flat[OMEGA_LT_MAX_BUFS], farena = 0; char why[256];
        if (omega_lt_plan(&t, off, &arena) != 0 || omega_lt_check(&t, off, why, sizeof why) != 0) { fails++; printf("FAIL sweep %d: %s\n", it, why); return; }
        omega_lt_plan_flat(&t, flat, &farena);
        uint64_t peak = omega_lt_peak_live(&t);
        if (arena < peak || arena > farena) { fails++; printf("FAIL sweep %d: arena %llu peak %llu flat %llu\n", it, (unsigned long long)arena,
                                                             (unsigned long long)peak, (unsigned long long)farena); return; }
        uint64_t moff[OMEGA_LT_MAX_BUFS], marena = 0;
        mutant_planner(&t, moff, &marena);
        if (oracle_unsafe(&t, off, arena) != 0 || oracle_unsafe(&t, flat, farena) != 0) { fails++; printf("FAIL sweep %d: oracle finds the plan unsafe\n", it); return; }
        int ored = oracle_unsafe(&t, moff, marena), cred = omega_lt_check(&t, moff, why, sizeof why) != 0;
        if (ored < 0) { fails++; printf("FAIL sweep %d: arena too big for the oracle\n", it); return; }
        if (ored && !cred) { fails++; printf("FAIL sweep %d: the oracle finds the mutant plan unsafe, the check passed it\n", it); return; }
        if (cred && !ored) { if (strncmp(why, "check:", 6) != 0) { fails++; printf("FAIL sweep %d: replay red on a plan the oracle finds safe (%s)\n", it, why); return; } conservative++; }
        mutant_cases++; mutant_caught += (uint32_t)ored;
    }
    checks++;
    printf("random sweep: 3000 traces safe, arena between peak live and flat; every mutant plan the byte oracle finds unsafe is red: %u of %u traces (plus %u refused by the conservative live-together rule)\n",
           mutant_caught, mutant_cases, conservative);
    CHECK(mutant_caught >= 100, "the mutant planner was unsafe in only %u of %u traces: the sweep does not exercise the check", mutant_caught, mutant_cases);
}

static void q38_plan(uint32_t layers, uint64_t mask, int print_map) {
    static char map[16384];
    OmegaGpuQ38Plan f, s;
    CHECK(omega_gpu_q38_graph_plan(layers, mask, 248320u, 0, &f, NULL, 0) == 0, "q38 flat plan");
    CHECK(omega_gpu_q38_graph_plan(layers, mask, 248320u, OMEGA_GPU_Q38_SHARE, &s, map, sizeof map) == 0, "q38 shared plan");
    CHECK(s.shared_bytes < s.flat_bytes && s.peak_live <= s.shared_bytes && f.flat_bytes == s.flat_bytes, "q38 plan sizes");
    printf("q38 decode graph, %u layers (mask 0x%llx): %u ops, %u accesses; buffer flat %llu B, shared %llu B, saved %llu B; "
           "peak live %llu B; pinned %llu B\n", layers, (unsigned long long)mask, s.ops, s.accesses, (unsigned long long)s.flat_bytes,
           (unsigned long long)s.shared_bytes, (unsigned long long)(s.flat_bytes - s.shared_bytes), (unsigned long long)s.peak_live,
           (unsigned long long)s.pinned_bytes);
    if (print_map) printf("%s", map);
    CHECK(omega_gpu_q38_graph_plan(0, 0, 248320u, 0, &f, NULL, 0) != 0 && omega_gpu_q38_graph_plan(2, 0x4, 248320u, 0, &f, NULL, 0) != 0, "plan refusals");
}

int main(void) {
    chain();
    outputs_and_tail();
    same_op_read_write();
    random_sweep();
    q38_plan(2, 0x2, 0);
    q38_plan(4, 0x8, 0);
    q38_plan(64, 0x8888888888888888ull, 1);
    printf("lifetime_test: %d of %d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
