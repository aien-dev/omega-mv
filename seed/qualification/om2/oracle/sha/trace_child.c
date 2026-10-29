/* SPDX-License-Identifier: Apache-2.0
 * ORACLE (outside the delivered closure), L836 phaseB/sha-f5d14cb2, contract section 8 evidence (b).
 * Child of trace.py: makes pairs of calls into the bound OM-2 SHA unit that differ only in message
 * (and pending-buffer) bytes, never in lengths, call sequence or buffer addresses. It stops itself
 * with SIGSTOP immediately before and immediately after each traced call sequence, so the tracer
 * cuts the single-step trace into segments; message filling happens outside the traced segments.
 * Traced segment order: for each case, message A then message B.
 * Build: gcc -std=gnu11 -O2 -I<bind> trace_child.c -L<bind> -lomega_native -Wl,-rpath,<bind> */
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "omega_api.h"

static uint64_t s;
static uint8_t nx(void)
{
    uint64_t z = (s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return (uint8_t)(z ^ (z >> 31));
}

static uint8_t buf[4096];
static sha256_ctx ctx;
static uint8_t out[32];

static void fill(uint64_t seed, size_t n, int variant)
{
    s = seed;
    for (size_t i = 0; i < n; i++)
        buf[i] = variant == 0 ? 0x00 : variant == 1 ? 0xff : nx();
}

int main(void)
{
    static const size_t lens[] = {0, 1, 55, 56, 63, 64, 65, 119, 120, 200, 1000};
    uint64_t r;
    /* untraced warm-up: a thread's first call maps the binding's stack (NATIVE-ABI.md), which would
     * make only the first traced segment's binding path longer */
    if (omega2_sha256_hash(&r, buf, 0, out))
        return 4;
    /* priming stop: the tracer attaches here with PTRACE_CONT; every traced segment then starts at a
     * stop reached by single-stepping, so segment boundaries are uniform */
    raise(SIGSTOP);
    /* hash: per length, pairs (zeros, ones) and (random seed 1, random seed 2) */
    for (size_t i = 0; i < sizeof lens / sizeof lens[0]; i++) {
        for (int p = 0; p < 2; p++) {
            for (int ab = 0; ab < 2; ab++) {
                if (p == 0)
                    fill(0, lens[i], ab);
                else
                    fill(100 + 2 * i + (uint64_t)ab, lens[i], 2);
                raise(SIGSTOP);
                {
                    /* sensitivity controls (negative, must make pair 7 differ): -DCONTROL=1 gives
                     * message B one byte more; -DCONTROL=2 hashes message B from buf + 64 */
                    size_t n = lens[i], o = 0;
#if defined(CONTROL)
                    if (i == 3 && p == 1 && ab == 1) {
                        if (CONTROL == 1)
                            n++;
                        else
                            o = 64;
                    }
#endif
                    if (omega2_sha256_hash(&r, buf + o, n, out))
                        return 2;
                }
                raise(SIGSTOP);
            }
        }
    }
    /* stream: init, update(k), update(n-k), final with the same lengths; the second update absorbs
     * a pending partial block whose bytes differ between A and B */
    static const size_t ks[][2] = {{3, 60}, {10, 100}, {63, 1}, {64, 64}, {100, 200}, {1, 0}};
    for (size_t i = 0; i < sizeof ks / sizeof ks[0]; i++) {
        for (int ab = 0; ab < 2; ab++) {
            fill(500 + 2 * i + (uint64_t)ab, ks[i][0] + ks[i][1], 2);
            memset(&ctx, 0, sizeof ctx);
            raise(SIGSTOP);
            if (omega2_sha256_init(&r, (uint8_t *)&ctx) || omega2_sha256_update(&r, (uint8_t *)&ctx, buf, ks[i][0]) ||
                omega2_sha256_update(&r, (uint8_t *)&ctx, buf + ks[i][0], ks[i][1]) ||
                omega2_sha256_final(&r, (uint8_t *)&ctx, out))
                return 3;
            raise(SIGSTOP);
        }
    }
    raise(SIGSTOP);
    return 0;
}
