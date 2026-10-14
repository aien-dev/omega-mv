/* fp8_scan_check: W-FL1 cross-check of the fast e4m3 NaN scan against the byte loop on real checkpoint bytes.
 * Usage: fp8_scan_check <file>...   Every file is read in 384 MiB chunks (the gate_up layer buffer size); each chunk
 * is scanned by omega_fp8_pack_scan_nan_ref (the old loop), omega_fp8_pack_scan_nan (NEON/SWAR) and
 * omega_fp8_pack_scan_nan_swar, aligned and at a 1-byte offset. Count and first index must agree everywhere.
 * Whole files include the bf16 tensors and the JSON header, so the "a block holds a NaN code" path runs too.
 * Prints per-scan seconds over scanned bytes (file reads excluded). Host only: no chip call. Exit 0 = all agree. */
#include "omega_gpu_fp8_api.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec + 1e-9 * (double)t.tv_nsec; }

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: fp8_scan_check <file>...\n"); return 2; }
    const size_t CH = (size_t)384 << 20;
    uint8_t *buf = malloc(CH + 1);
    if (!buf) return 2;
    double t_ref = 0, t_fast = 0, t_swar = 0; size_t bytes = 0, chunks = 0, hits = 0, bad = 0;
    for (int a = 1; a < argc; a++) {
        int fd = open(argv[a], O_RDONLY);
        if (fd < 0) { perror(argv[a]); return 2; }
        for (off_t off = 0;; off += (off_t)CH) {
            ssize_t got = 0;
            while ((size_t)got < CH) { ssize_t r = pread(fd, buf + 1 + got, CH - (size_t)got, off + got); if (r <= 0) break; got += r; }
            if (got <= 0) break;
            size_t len = (size_t)got;
            for (int shift = 0; shift < 2; shift++) {
                /* shift 0: the chunk from buf+1 (odd address); shift 1: copied down to buf (aligned malloc base) */
                const uint8_t *w = shift ? buf : buf + 1;
                if (shift) memmove(buf, buf + 1, len);
                size_t fr = 9, ff = 9, fs = 9; double t0 = now_s();
                size_t nr = omega_fp8_pack_scan_nan_ref(w, len, &fr); double t1 = now_s();
                size_t nf = omega_fp8_pack_scan_nan(w, len, &ff); double t2 = now_s();
                size_t ns = omega_fp8_pack_scan_nan_swar(w, len, &fs); double t3 = now_s();
                if (nr != nf || fr != ff || nr != ns || fr != fs) {
                    bad++; printf("MISMATCH %s @%lld shift %d: ref %zu@%zu fast %zu@%zu swar %zu@%zu\n", argv[a], (long long)off, shift, nr, fr, nf, ff, ns, fs);
                }
                if (shift == 0) { t_ref += t1 - t0; t_fast += t2 - t1; t_swar += t3 - t2; bytes += len; hits += nr; }
            }
            chunks++;
            if (len < CH) break;
        }
        close(fd);
    }
    printf("fp8_scan_check: %zu chunks, %.3f GB, %zu NaN-code bytes (bf16 data and headers included)\n", chunks, (double)bytes / 1e9, hits);
    printf("  byte loop %.2f s (%.1f GB/s) | fast %.2f s (%.1f GB/s) | swar %.2f s (%.1f GB/s)\n", t_ref, (double)bytes / 1e9 / t_ref,
           t_fast, (double)bytes / 1e9 / t_fast, t_swar, (double)bytes / 1e9 / t_swar);
    printf("fp8_scan_check: %s (%zu mismatches)\n", bad ? "FAIL" : "PASS", bad);
    free(buf);
    return bad ? 1 : 0;
}
