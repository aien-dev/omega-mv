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

/* make test-sha256-fs-parity: the freestanding build's migration-pending C SHA-256 (src/omega_sha256_fs.c,
 * compiled with its sha256_* renamed fs_sha256_*) against the Omega unit every hosted build uses
 * (om/sha256/sha256-om2.om through src/omega_om_units.c). One-shot digests of every length 0..1100 (all the
 * padding boundaries 55/56/63/64/65 and their block multiples) and of 4096, 65536 and 1048583 bytes; the
 * same inputs streamed in pseudo-random pieces through init/update/final; and the FIPS 180-2 "abc" and
 * empty-string digests on both sides. Exit 1 on any difference. */
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void fs_sha256_init(sha256_ctx *ctx);
void fs_sha256_update(sha256_ctx *ctx, const uint8_t *data, size_t len);
void fs_sha256_final(sha256_ctx *ctx, uint8_t digest[SHA256_DIGEST_SIZE]);
void fs_sha256_hash(const uint8_t *data, size_t len, uint8_t digest[SHA256_DIGEST_SIZE]);

static uint64_t rng = 0x9e3779b97f4a7c15ULL;
static uint64_t next(void) {
    rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
    return rng;
}

static int fails;
static long compared;

static void check(const char *what, size_t len, const uint8_t a[32], const uint8_t b[32]) {
    compared++;
    if (memcmp(a, b, 32) != 0) {
        if (fails < 10) printf("DIFFER %s len %zu\n", what, len);
        fails++;
    }
}

static void stream(const uint8_t *d, size_t len, int fs, uint8_t out[32]) {
    sha256_ctx c;
    if (fs) fs_sha256_init(&c); else sha256_init(&c);
    size_t off = 0;
    while (off < len) {
        size_t n = (size_t)(next() % 130);
        if (n > len - off) n = len - off;
        if (fs) fs_sha256_update(&c, d + off, n); else sha256_update(&c, d + off, n);
        off += n;
    }
    if (fs) fs_sha256_final(&c, out); else sha256_final(&c, out);
}

static void known(const char *msg, const char *hex) {
    uint8_t want[32], a[32], b[32];
    for (int i = 0; i < 32; i++) { unsigned v; sscanf(hex + 2 * i, "%2x", &v); want[i] = (uint8_t)v; }
    fs_sha256_hash((const uint8_t *)msg, strlen(msg), a);
    sha256_hash((const uint8_t *)msg, strlen(msg), b);
    check("known-answer C", strlen(msg), a, want);
    check("known-answer Omega", strlen(msg), b, want);
}

int main(void) {
    enum { BIG = 1048583 };
    uint8_t *d = malloc(BIG);
    if (!d) return 2;
    for (size_t i = 0; i < BIG; i++) d[i] = (uint8_t)next();
    uint8_t a[32], b[32];
    long lengths = 0;
    static const size_t extra[] = { 4096, 65536, BIG };
    for (size_t k = 0; k <= 1100 + 3; k++) {
        size_t len = k <= 1100 ? k : extra[k - 1101];
        fs_sha256_hash(d, len, a);
        sha256_hash(d, len, b);
        check("one-shot", len, a, b);
        uint64_t seed = rng;
        stream(d, len, 1, a);
        rng = seed;
        stream(d, len, 0, b);
        check("streamed", len, a, b);
        lengths++;
    }
    known("abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    known("", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    free(d);
    printf("%s: freestanding C SHA-256 vs the Omega unit: %ld lengths (0..1100, 4096, 65536, %d), %ld digest "
           "comparisons, %d differ\n", fails ? "FAIL" : "PASS", lengths, BIG, compared, fails);
    return fails ? 1 : 0;
}
