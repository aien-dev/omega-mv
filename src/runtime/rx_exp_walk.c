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
 * rx_exp_walk.c -- walk1d, a seeded, resettable, stochastic transition family
 * for verified experience (rx_experience.h has the state layout). Pure
 * functions of the packed state: the noise of step k is word k of the PD0
 * SplitMix64 stream (src/physics0/pd0_rng.c) keyed by the episode key, so a
 * replay needs nothing but the state and the action.
 */
#include "rx_experience.h"

#include "physics0/pd0_rng.h"
#include "sha256.h"

#include <string.h>

#define GOLDEN 0x9e3779b97f4a7c15ull

enum { OFF_KEY = 0, OFF_K = 24, OFF_X = 34, OFF_V = 46, OFF_TGT = 51, OFF_ACT = 57,
       OFF_NOISE = 60, OFF_LIVE = 62, OFF_OP = 63 };
enum { W_KEY = 24, W_K = 10, W_X = 12, W_V = 5, W_TGT = 6, W_ACT = 3, W_NOISE = 2 };
#define X_MAX 2000
#define V_MAX 8
#define T_MAX 20
#define ACT_MASK (((1ull << W_ACT) - 1) << OFF_ACT)
#define OP_BIT (1ull << OFF_OP)
#define LIVE_BIT (1ull << OFF_LIVE)
#define KEY_MASK ((1ull << W_KEY) - 1)

static uint64_t get(uint64_t s, unsigned off, unsigned w) { return (s >> off) & ((1ull << w) - 1); }
static int64_t sget(uint64_t s, unsigned off, unsigned w) {
    uint64_t v = get(s, off, w);
    return (v >> (w - 1)) ? (int64_t)v - (int64_t)(1ull << w) : (int64_t)v;
}
static uint64_t put(int64_t v, unsigned off, unsigned w) {
    return ((uint64_t)v & ((1ull << w) - 1)) << off;
}
static int64_t clamp(int64_t v, int64_t lo, int64_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* Word `i` of the stream keyed by `key` and `tag` (= the (i+1)-th pd0_next). */
static uint64_t stream_word(uint64_t key, const char *tag, uint64_t i) {
    pd0_rng r;
    pd0_stream(&r, key, tag);
    r.s += i * GOLDEN;
    return pd0_next(&r);
}

uint64_t rx_exp_walk_key(uint64_t seed) {
    uint64_t s = seed;
    uint64_t w = pd0_splitmix64(&s);
    uint64_t k = (w ^ (w >> 24) ^ (w >> 48)) & KEY_MASK;
    return k ? k : 1;
}

static int walk_step(uint64_t in, uint64_t *next, uint64_t *cost) {
    uint64_t key = get(in, OFF_KEY, W_KEY);
    if (in & OP_BIT) {                          /* reset: only the key may be set */
        if (key == 0 || (in & ~(OP_BIT | KEY_MASK))) return -1;
        int64_t tgt = (int64_t)(stream_word(key, "omega.exp.walk1d.target", 0) % (2 * T_MAX + 1)) - T_MAX;
        *next = key | put(tgt, OFF_TGT, W_TGT) | LIVE_BIT;
        *cost = 1;
        return 0;
    }
    uint64_t k = get(in, OFF_K, W_K), a = get(in, OFF_ACT, W_ACT);
    int64_t x = sget(in, OFF_X, W_X), v = sget(in, OFF_V, W_V), tgt = sget(in, OFF_TGT, W_TGT);
    if (!(in & LIVE_BIT) || key == 0 || a < 1 || a > 5 || k >= RX_EXP_WALK_K_MAX) return -1;
    if (x < -X_MAX || x > X_MAX || v < -V_MAX || v > V_MAX || tgt < -T_MAX || tgt > T_MAX) return -1;
    int64_t thrust = (int64_t)a - 3;
    int64_t noise = (int64_t)(stream_word(key, "omega.exp.walk1d.noise", k) % 3) - 1;
    int64_t v2 = clamp(v + thrust + noise, -V_MAX, V_MAX);
    int64_t x2 = clamp(x + v2, -X_MAX, X_MAX);
    *next = key | put((int64_t)(k + 1), OFF_K, W_K) | put(x2, OFF_X, W_X) | put(v2, OFF_V, W_V) |
            put(tgt, OFF_TGT, W_TGT) | put((int64_t)a, OFF_ACT, W_ACT) |
            put(noise + 2, OFF_NOISE, W_NOISE) | LIVE_BIT;
    *cost = (uint64_t)(thrust < 0 ? -thrust : thrust) + 1;
    return 0;
}

static uint64_t walk_encode(uint64_t prior, uint64_t action) {
    uint64_t a = action <= 7 ? action : 0;      /* out of range: code 0, refused by step */
    return (prior & ~(ACT_MASK | OP_BIT)) | (a << OFF_ACT);
}

static uint64_t walk_reset_input(uint64_t seed) { return OP_BIT | rx_exp_walk_key(seed); }

static uint32_t walk_unpack(uint64_t s, int64_t out[RX_EXP_MAX_FIELDS]) {
    out[RX_EXP_WALK_KEY] = (int64_t)get(s, OFF_KEY, W_KEY);
    out[RX_EXP_WALK_K] = (int64_t)get(s, OFF_K, W_K);
    out[RX_EXP_WALK_X] = sget(s, OFF_X, W_X);
    out[RX_EXP_WALK_V] = sget(s, OFF_V, W_V);
    out[RX_EXP_WALK_TARGET] = sget(s, OFF_TGT, W_TGT);
    out[RX_EXP_WALK_ACT] = (int64_t)get(s, OFF_ACT, W_ACT);
    out[RX_EXP_WALK_NOISE] = (int64_t)get(s, OFF_NOISE, W_NOISE);
    out[RX_EXP_WALK_LIVE] = (int64_t)get(s, OFF_LIVE, 1);
    return RX_EXP_WALK_FIELDS;
}

static void le64(uint8_t *b, uint64_t v) { for (int i = 0; i < 8; i++) b[i] = (uint8_t)(v >> (8 * i)); }

/* Objective: reach the episode's target. Bound to the key and the target. */
static void walk_objective(uint64_t s, uint8_t out[32]) {
    static const char tag[] = "omega.exp.walk1d.objective.v1";
    uint8_t b[sizeof tag - 1 + 16];
    memcpy(b, tag, sizeof tag - 1);
    le64(b + sizeof tag - 1, get(s, OFF_KEY, W_KEY));
    le64(b + sizeof tag - 1 + 8, (uint64_t)sget(s, OFF_TGT, W_TGT));
    sha256_hash(b, sizeof b, out);
}

static uint64_t walk_skill(const uint64_t *in, uint32_t n, uint32_t attempt, int *failed) {
    (void)attempt;
    uint64_t next = 0, cost = 0;
    *failed = n < 1 || walk_step(in[0], &next, &cost) != 0;
    return *failed ? 0 : next;
}

static int walk_contract(uint64_t input, uint64_t result) {
    uint64_t next = 0, cost = 0;
    return walk_step(input, &next, &cost) == 0 && next == result;
}

const RxExpFamily rx_exp_walk1d = {
    .family = RX_EXP_FAMILY_WALK1D,
    .version = 1,
    .op = 1,
    .name = "omega.exp.walk1d",
    .domain = ((uint64_t)V_MAX << 32) | X_MAX,
    .step = walk_step,
    .encode = walk_encode,
    .reset_input = walk_reset_input,
    .unpack = walk_unpack,
    .objective = walk_objective,
    .skill = walk_skill,
    .contract = walk_contract,
};
