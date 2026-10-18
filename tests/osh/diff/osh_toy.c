/*
 * osh_toy.c -- the toy stage operations of the cross-adapter differential corpus (omega#568, tests/osh/diff/README.md).
 * One multicall program: the operation is the last path element of argv[0], so the runner links it into a private PATH
 * directory under each name. The AIENOS native harness implements the same names as Omega-native ops with the same
 * observable bytes (README, "Toy operations"); the semantics below are the contract, this file is only the Linux build.
 *   emit ARGS...    each argument followed by a newline, on stdout
 *   args ARGS...    "N\n" then "LEN:BYTES\n" per argument (argument boundaries, empty and non-ASCII bytes)
 *   pass            copy stdin to stdout in reads of up to 4096 bytes
 *   trickle         copy stdin to stdout one byte per read and one byte per write (short reads and writes)
 *   flood N         write N bytes, byte i = 'a' + i % 26, in writes of up to 4093 bytes (partial writes looped)
 *   bytes           write the 256 byte values 0..255 once
 *   take N          read until N bytes or EOF, write them, exit 0 without reading more (consumer early exit)
 *   sink            read stdin to EOF, print "n=COUNT sum=FNV1A32\n" (8 lower-case hex digits)
 *   status N        exit with status N, no output
 *   oe              write "O" to stdout, then "E" to stderr
 *   stall           wait for a signal forever (cancellation fixtures)
 * Not part of osh. No shell is involved anywhere.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int put(int fd, const void *p, size_t n)
{
    const unsigned char *b = p;
    while (n > 0) {
        ssize_t w = write(fd, b, n);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return -1;
        b += w;
        n -= (size_t)w;
    }
    return 0;
}

static long num(const char *s)
{
    char *e;
    long v = strtol(s, &e, 10);
    return (*s && !*e && v >= 0) ? v : -1;
}

int main(int argc, char **argv)
{
    const char *op = strrchr(argv[0], '/');
    op = op ? op + 1 : argv[0];
    if (strcmp(op, "emit") == 0) {
        for (int i = 1; i < argc; i++)
            if (put(1, argv[i], strlen(argv[i])) || put(1, "\n", 1)) return 1;
        return 0;
    }
    if (strcmp(op, "args") == 0) {
        char h[32];
        int k = snprintf(h, sizeof h, "%d\n", argc - 1);
        if (put(1, h, (size_t)k)) return 1;
        for (int i = 1; i < argc; i++) {
            k = snprintf(h, sizeof h, "%zu:", strlen(argv[i]));
            if (put(1, h, (size_t)k) || put(1, argv[i], strlen(argv[i])) || put(1, "\n", 1)) return 1;
        }
        return 0;
    }
    if (strcmp(op, "pass") == 0 || strcmp(op, "trickle") == 0) {
        size_t chunk = op[0] == 'p' ? 4096 : 1;
        char b[4096];
        for (;;) {
            ssize_t r = read(0, b, chunk);
            if (r < 0 && errno == EINTR) continue;
            if (r < 0) return 1;
            if (r == 0) return 0;
            for (ssize_t i = 0; i < r; i += (ssize_t)chunk)
                if (put(1, b + i, (size_t)(r - i) < chunk ? (size_t)(r - i) : chunk)) return 1;
        }
    }
    if (strcmp(op, "flood") == 0) {
        long n = argc == 2 ? num(argv[1]) : -1;
        if (n < 0) return 2;
        char b[4093];
        long done = 0;
        while (done < n) {
            size_t k = (size_t)(n - done) < sizeof b ? (size_t)(n - done) : sizeof b;
            for (size_t i = 0; i < k; i++) b[i] = (char)('a' + (done + (long)i) % 26);
            if (put(1, b, k)) return 1;
            done += (long)k;
        }
        return 0;
    }
    if (strcmp(op, "bytes") == 0) {
        unsigned char b[256];
        for (int i = 0; i < 256; i++) b[i] = (unsigned char)i;
        return put(1, b, sizeof b) ? 1 : 0;
    }
    if (strcmp(op, "take") == 0) {
        long n = argc == 2 ? num(argv[1]) : -1;
        if (n < 0 || n > 65536) return 2;
        static char b[65536];
        long got = 0;
        while (got < n) {
            ssize_t r = read(0, b + got, (size_t)(n - got));
            if (r < 0 && errno == EINTR) continue;
            if (r <= 0) break;
            got += r;
        }
        return put(1, b, (size_t)got) ? 1 : 0;
    }
    if (strcmp(op, "sink") == 0) {
        uint32_t h = 2166136261u;
        unsigned long long n = 0;
        unsigned char b[4096];
        for (;;) {
            ssize_t r = read(0, b, sizeof b);
            if (r < 0 && errno == EINTR) continue;
            if (r < 0) return 1;
            if (r == 0) break;
            for (ssize_t i = 0; i < r; i++) h = (h ^ b[i]) * 16777619u;
            n += (unsigned long long)r;
        }
        char o[64];
        int k = snprintf(o, sizeof o, "n=%llu sum=%08x\n", n, h);
        return put(1, o, (size_t)k) ? 1 : 0;
    }
    if (strcmp(op, "status") == 0) {
        long n = argc == 2 ? num(argv[1]) : -1;
        return n < 0 || n > 255 ? 2 : (int)n;
    }
    if (strcmp(op, "oe") == 0) return put(1, "O", 1) || put(2, "E", 1) ? 1 : 0;
    if (strcmp(op, "stall") == 0) {
        for (;;) pause();
    }
    fprintf(stderr, "osh_toy: unknown operation %s\n", op);
    return 2;
}
