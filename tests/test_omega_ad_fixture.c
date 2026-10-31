/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Drake Stapleton (AIEN / aien-dev)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * AD fixture oracle for the native .om tape (native/autodiff664, omega #664).
 *
 *   test_omega_ad_fixture --host [--mutant-sign] FIXTURE
 *   test_omega_ad_fixture --chip [--mutant-sign] FIXTURE   (-DOMEGA_AD_FIXTURE_CHIP)
 *
 * FIXTURE is written by the native trainer (aien-train tests/adtrain, mode D):
 * the inputs of one MLP step and the forward values / gradients the native
 * tape computed. This program rebuilds the same graph on the C M21 tape over
 * an M20 realization (CPU table or GB10 table), runs backward, and compares
 * every value: Y[8,2], loss, dW1, db1, dW2, db2 (75 values).
 *
 * Graph: H = max(X.W1 + broadcast(b1), 0); Y = H.W2 + broadcast(b2);
 *        loss = sum_axis0(sum_axis1((Y - T) * (Y - T))).
 *
 * Format: little-endian u32 words. "ADFX", version 1, M K H N = 8 4 8 2, then
 * X[32] W1[32] b1[8] W2[16] b2[2] T[16], then Y[16] loss dW1[32] db1[8]
 * dW2[16] db2[2]: 187 words, 748 bytes. Any other size is refused.
 *
 * Comparison: equal values (==, so +0 and -0 agree) are exact; otherwise the
 * ULP distance of two finite same-sign values. PASS needs max_ulp 0; PASS_TOL
 * allows max_ulp <= 2 (the declared contingency); anything else is FAIL.
 * --mutant-sign negates the expected dW1 first: a negative control that must
 * FAIL. Exit 0 PASS/PASS_TOL, 1 FAIL, 2 refused or setup error.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "omega_autodiff.h"
#include "omega_numeric.h"
#include "omega_tensor.h"

enum { FM = 8, FK = 4, FH = 8, FN = 2 };
enum {
    OFF_X = 6,
    OFF_W1 = OFF_X + FM * FK,
    OFF_B1 = OFF_W1 + FK * FH,
    OFF_W2 = OFF_B1 + FH,
    OFF_B2 = OFF_W2 + FH * FN,
    OFF_T = OFF_B2 + FN,
    OFF_Y = OFF_T + FM * FN,
    OFF_L = OFF_Y + FM * FN,
    OFF_DW1 = OFF_L + 1,
    OFF_DB1 = OFF_DW1 + FK * FH,
    OFF_DW2 = OFF_DB1 + FH,
    OFF_DB2 = OFF_DW2 + FH * FN,
    FIX_WORDS = OFF_DB2 + FN
};
enum { NCMP = FIX_WORDS - OFF_Y };

static uint32_t W[FIX_WORDS];
static float wf(size_t i) {
    float f;
    memcpy(&f, &W[i], sizeof f);
    return f;
}

static uint32_t fbits(float f) {
    uint32_t u;
    memcpy(&u, &f, sizeof u);
    return u;
}

static int load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { printf("AD_FIXTURE: cannot open %s\n", path); return 2; }
    unsigned char buf[4 * FIX_WORDS + 1];
    size_t got = fread(buf, 1, sizeof buf, f);
    fclose(f);
    if (got != 4 * FIX_WORDS) { printf("AD_FIXTURE: size %zu, want %d\n", got, 4 * FIX_WORDS); return 2; }
    if (memcmp(buf, "ADFX", 4) != 0) { printf("AD_FIXTURE: bad magic\n"); return 2; }
    for (size_t i = 0; i < FIX_WORDS; i++)
        W[i] = (uint32_t)buf[4 * i] | (uint32_t)buf[4 * i + 1] << 8 | (uint32_t)buf[4 * i + 2] << 16 |
               (uint32_t)buf[4 * i + 3] << 24;
    if (W[1] != 1 || W[2] != FM || W[3] != FK || W[4] != FH || W[5] != FN) {
        printf("AD_FIXTURE: bad version or dimensions\n");
        return 2;
    }
    return 0;
}

/* ULP distance of two finite same-sign floats; UINT32_MAX when not comparable. */
static uint32_t ulps(float a, float b) {
    if (a == b) return 0;
    uint32_t ua, ub;
    memcpy(&ua, &a, 4);
    memcpy(&ub, &b, 4);
    if ((ua & 0x7f800000U) == 0x7f800000U || (ub & 0x7f800000U) == 0x7f800000U) return UINT32_MAX;
    if ((ua ^ ub) & 0x80000000U) return UINT32_MAX;
    return ua > ub ? ua - ub : ub - ua;
}

static int leaf(OmegaAdTape *t, OmegaTensorCtx *ctx, size_t off, uint64_t r, uint64_t c, bool rg,
                OmegaTensor *keep, uint32_t *id) {
    float v[64];
    for (size_t i = 0; i < r * c; i++) v[i] = off ? wf(off + i) : 0.0f;
    uint64_t sh[2] = { r, c };
    int rc = omega_tensor_from_f32(ctx, 2, sh, v, keep);
    if (!rc) rc = omega_ad_leaf(t, *keep, rg, id);
    return rc;
}

int main(int argc, char **argv) {
    bool chip = false, mutant = false;
    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--host")) chip = false;
        else if (!strcmp(argv[i], "--chip")) chip = true;
        else if (!strcmp(argv[i], "--mutant-sign")) mutant = true;
        else if (!path) path = argv[i];
        else path = NULL, i = argc;
    }
    if (!path) { printf("usage: %s --host|--chip [--mutant-sign] FIXTURE\n", argv[0]); return 2; }
    const char *arm = chip ? "CHIP" : "HOST";
    const OmegaTensorRealization *real = omega_tensor_cpu_realization();
    if (chip) {
#ifdef OMEGA_AD_FIXTURE_CHIP
        const char *e = getenv("GB10_CHIP_RUN");
        if (!e || strcmp(e, "1") != 0) { printf("--chip needs GB10_CHIP_RUN=1 (tests/run_ad_chip.sh)\n"); return 2; }
        real = omega_tensor_gb10_realization();
#else
        printf("--chip refused: built without OMEGA_AD_FIXTURE_CHIP\n");
        return 2;
#endif
    }
    if (!omega_numeric_fpenv_ok()) { printf("FPCR not RNE/no-FTZ: refusing to run\n"); return 2; }
    int rc = load(path);
    if (rc) return rc;
    if (mutant)
        for (size_t i = 0; i < FK * FH; i++) W[OFF_DW1 + i] ^= 0x80000000U;

    OmegaTensorCtx *ctx = NULL;
    if (omega_tensor_ctx_create(256, real, &ctx)) { printf("AD_FIXTURE: ctx\n"); return 2; }
    OmegaAdNode nodes[32];
    float scratch[3 * 64];
    OmegaAdTape t;
    OmegaTensor lv[7];
    uint32_t x, w1, b1, w2, b2, tt, z, mm1, bc1, a1, h, mm2, bc2, y, d, sq, s1, loss;
    rc = omega_ad_tape_init(&t, ctx, nodes, 32, scratch, sizeof scratch / sizeof scratch[0]);
    uint64_t sh88[2] = { FM, FH }, sh82[2] = { FM, FN };
    if (!rc) rc = leaf(&t, ctx, OFF_X, FM, FK, false, &lv[0], &x);
    if (!rc) rc = leaf(&t, ctx, OFF_W1, FK, FH, true, &lv[1], &w1);
    if (!rc) rc = leaf(&t, ctx, OFF_B1, 1, FH, true, &lv[2], &b1);
    if (!rc) rc = leaf(&t, ctx, OFF_W2, FH, FN, true, &lv[3], &w2);
    if (!rc) rc = leaf(&t, ctx, OFF_B2, 1, FN, true, &lv[4], &b2);
    if (!rc) rc = leaf(&t, ctx, OFF_T, FM, FN, false, &lv[5], &tt);
    if (!rc) rc = leaf(&t, ctx, 0, FM, FH, false, &lv[6], &z);
    if (!rc) rc = omega_ad_matmul(&t, x, w1, &mm1);
    if (!rc) rc = omega_ad_broadcast(&t, b1, 2, sh88, &bc1);
    if (!rc) rc = omega_ad_binary(&t, OMEGA_AD_ADD, mm1, bc1, &a1);
    if (!rc) rc = omega_ad_binary(&t, OMEGA_AD_MAX, a1, z, &h);
    if (!rc) rc = omega_ad_matmul(&t, h, w2, &mm2);
    if (!rc) rc = omega_ad_broadcast(&t, b2, 2, sh82, &bc2);
    if (!rc) rc = omega_ad_binary(&t, OMEGA_AD_ADD, mm2, bc2, &y);
    if (!rc) rc = omega_ad_binary(&t, OMEGA_AD_SUB, y, tt, &d);
    if (!rc) rc = omega_ad_binary(&t, OMEGA_AD_MUL, d, d, &sq);
    if (!rc) rc = omega_ad_reduce(&t, OMEGA_AD_SUM, sq, 1, true, &s1);
    if (!rc) rc = omega_ad_reduce(&t, OMEGA_AD_SUM, s1, 0, true, &loss);
    if (!rc) rc = omega_ad_backward(&t, loss);

    float got[NCMP];
    OmegaTensor v;
    if (!rc) rc = omega_ad_value(&t, y, &v);
    if (!rc) rc = omega_tensor_read_f32(ctx, v, got, FM * FN);
    if (!rc) rc = omega_ad_value(&t, loss, &v);
    if (!rc) rc = omega_tensor_read_f32(ctx, v, got + (OFF_L - OFF_Y), 1);
    if (!rc) rc = omega_ad_grad_read_f32(&t, w1, got + (OFF_DW1 - OFF_Y), FK * FH);
    if (!rc) rc = omega_ad_grad_read_f32(&t, b1, got + (OFF_DB1 - OFF_Y), FH);
    if (!rc) rc = omega_ad_grad_read_f32(&t, w2, got + (OFF_DW2 - OFF_Y), FH * FN);
    if (!rc) rc = omega_ad_grad_read_f32(&t, b2, got + (OFF_DB2 - OFF_Y), FN);
    if (rc) {
        printf("AD_FIXTURE_%s: C tape error %d (numeric %d)\n", arm, rc, omega_tensor_last_numeric_error(ctx));
        omega_ad_tape_release(&t);
        omega_tensor_ctx_destroy(ctx);
        return 2;
    }

    unsigned exact = 0;
    uint32_t max_ulp = 0;
    bool comparable = true;
    for (size_t i = 0; i < NCMP; i++) {
        uint32_t u = ulps(got[i], wf(OFF_Y + i));
        if (u == 0) exact++;
        if (u == UINT32_MAX) comparable = false;
        else if (u > max_ulp) max_ulp = u;
        if (u) printf("AD_FIXTURE_%s: word %zu native %08x C %08x ulp %u\n", arm, (size_t)OFF_Y + i,
                      (unsigned)W[OFF_Y + i], (unsigned)fbits(got[i]), (unsigned)u);
    }
    const char *verdict = !comparable || max_ulp > 2 ? "FAIL" : max_ulp ? "PASS_TOL" : "PASS";
    char ulpbuf[16];
    snprintf(ulpbuf, sizeof ulpbuf, "%u", (unsigned)max_ulp);
    printf("AD_FIXTURE_%s: exact=%u/%d max_ulp=%s verdict=%s%s\n", arm, exact, NCMP,
           comparable ? ulpbuf : "inf", verdict, mutant ? " (mutant-sign)" : "");
    omega_ad_tape_release(&t);
    for (int i = 0; i < 7; i++) omega_tensor_release(ctx, lv[i]);
    omega_tensor_ctx_destroy(ctx);
    return strcmp(verdict, "FAIL") ? 0 : 1;
}
