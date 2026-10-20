/*
 * test_omegese_lib.c -- the library (omegese_lib.h) against the command, prefix by prefix.
 *
 *   test_omegese_lib <omegese-binary> <scratch-dir> <file>...
 *
 * For every file, every prefix (each byte boundary) is asked twice in one process: in order, then in a
 * fixed shuffled order across all files, so state a query left behind would change a later answer. Each
 * answer (stdout bytes and exit code) must equal `<omegese> next --machine` on the same prefix, and on
 * every 8th prefix also `<omegese> complete`. Prints one summary line; exit 0 only when every answer
 * matched.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include "omegese_lib.h"

static char *slurp_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    if (fseek(f, 0, SEEK_END) || (*len = (size_t)ftell(f), fseek(f, 0, SEEK_SET))) { perror(path); exit(2); }
    char *b = malloc(*len + 1);
    if (!b || fread(b, 1, *len, f) != *len) { perror(path); exit(2); }
    b[*len] = 0;
    fclose(f);
    return b;
}

/* the command on src[0..len): its stdout and exit code */
static int command(const char *bin, const char *dir, int mode, const char *src, size_t len, char **out, size_t *n) {
    char path[4096], cmd[8192];
    snprintf(path, sizeof path, "%s/prefix.omegese", dir);
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(src, 1, len, f) != len || fclose(f)) { perror(path); exit(2); }
    snprintf(cmd, sizeof cmd, "'%s' %s '%s' 2>/dev/null", bin, mode == OMEGESE_COMPLETE ? "complete" : "next --machine", path);
    FILE *p = popen(cmd, "r");
    if (!p) { perror("popen"); exit(2); }
    size_t cap = 1 << 12;
    *out = malloc(cap);
    *n = 0;
    size_t k;
    while ((k = fread(*out + *n, 1, cap - *n, p)) > 0) {
        *n += k;
        if (*n == cap) *out = realloc(*out, cap *= 2);
    }
    int st = pclose(p);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

typedef struct { int file; size_t len; } Q;

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: test_omegese_lib <omegese> <scratch-dir> <file>...\n"); return 2; }
    int nf = argc - 3;
    char **src = malloc(sizeof *src * (size_t)nf);
    size_t *len = malloc(sizeof *len * (size_t)nf), nq = 0;
    for (int i = 0; i < nf; i++) { src[i] = slurp_file(argv[3 + i], &len[i]); nq += len[i] + 1; }
    Q *q = malloc(sizeof *q * nq * 2);
    size_t m = 0;
    for (int i = 0; i < nf; i++) for (size_t l = 0; l <= len[i]; l++) q[m++] = (Q){i, l};
    /* the second pass: the same queries, shuffled with a fixed generator */
    memcpy(q + m, q, sizeof *q * m);
    unsigned long long s = 0x9E3779B97F4A7C15ull;
    for (size_t i = m - 1; i > 0; i--) {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        size_t j = (size_t)(s % (i + 1));
        Q t = q[m + i]; q[m + i] = q[m + j]; q[m + j] = t;
    }
    size_t asked = 0, completes = 0, bad = 0;
    for (size_t i = 0; i < 2 * m; i++) {
        for (int mode = 0; mode < 2; mode++) {
            if (mode == OMEGESE_COMPLETE && i % 8) continue;
            const char *p = src[q[i].file];
            char *a, *b;
            size_t an, bn;
            int ra = omegese_lib_query(mode, p, q[i].len, &a, &an);
            int rb = command(argv[1], argv[2], mode, p, q[i].len, &b, &bn);
            asked++;
            completes += mode == OMEGESE_COMPLETE;
            if (ra != rb || an != bn || memcmp(a, b, an)) {
                if (bad++ < 5)
                    printf("MISMATCH %s %s prefix %zu bytes: library exit %d (%zu bytes), command exit %d (%zu bytes)\n--- library\n%.*s--- command\n%.*s---\n",
                           argv[3 + q[i].file], mode ? "complete" : "next", q[i].len, ra, an, rb, bn, (int)an, a, (int)bn, b);
            }
            omegese_lib_free(a);
            free(b);
        }
    }
    printf("%s omegese_lib: %zu queries (%zu complete) over %d files, 2 passes (in order, shuffled), %zu mismatches\n",
           bad ? "FAIL" : "PASS", asked, completes, nf, bad);
    for (int i = 0; i < nf; i++) free(src[i]);
    free(src);
    free(len);
    free(q);
    return bad ? 1 : 0;
}
