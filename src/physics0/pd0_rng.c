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

#include "physics0/pd0_rng.h"

uint64_t pd0_fnv1a64(const char *tag) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (; *tag; tag++) { h ^= (uint8_t)*tag; h *= 0x100000001b3ull; }
    return h;
}

uint64_t pd0_splitmix64(uint64_t *s) {
    uint64_t z = (*s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

void pd0_stream(pd0_rng *r, uint64_t seed, const char *tag) { r->s = seed ^ pd0_fnv1a64(tag); }
uint64_t pd0_next(pd0_rng *r) { return pd0_splitmix64(&r->s); }
int64_t pd0_uniform(pd0_rng *r) { return (int64_t)(pd0_next(r) % 1000001ull); }

int64_t pd0_const(pd0_rng *r, int64_t lo, int64_t hi) {
    int64_t u = pd0_uniform(r);
    return lo + ((hi - lo) * u) / PD0_MICRO;
}

int64_t pd0_noise(pd0_rng *r, int64_t sigma_micro) {
    int64_t u1 = pd0_uniform(r), u2 = pd0_uniform(r), u3 = pd0_uniform(r), u4 = pd0_uniform(r);
    pd0_i128 c = (pd0_i128)((u1 + u2 + u3 + u4) - 2000000LL) * sigma_micro;
    return (int64_t)(c / PD0_IH_SD);
}

int64_t pd0_mul(int64_t a, int64_t b) {
    pd0_i128 p = (pd0_i128)a * (pd0_i128)b;
    return (int64_t)(p / PD0_MICRO);
}

int64_t pd0_div(int64_t a, int64_t b) { return a / b; }
