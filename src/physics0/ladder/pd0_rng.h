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

/* SplitMix64 (Vigna's published constants) and FNV-1a 64, per spec section 3. Header-only. */
#ifndef PD0_RNG_H
#define PD0_RNG_H
#include <stdint.h>
#include <string.h>
typedef struct { uint64_t s; } pd0_rng;
static inline uint64_t pd0_rng_next(pd0_rng *r)
{
    uint64_t z = (r->s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
static inline uint64_t pd0_fnv1a64(const char *tag)
{
    uint64_t h = 0xcbf29ce484222325ull; size_t n = strlen(tag);
    for (size_t i = 0; i < n; i++) { h ^= (uint8_t)tag[i]; h *= 0x100000001b3ull; }
    return h;
}
static inline void pd0_rng_stream(pd0_rng *r, uint64_t seed, const char *tag) { r->s = seed ^ pd0_fnv1a64(tag); }
/* uniform micro in [0, 1_000_000] */
static inline int64_t pd0_rng_unit(pd0_rng *r) { return (int64_t)(pd0_rng_next(r) % 1000001ull); }
/* Irwin-Hall noise draw, sd of the four-uniform sum 577_350 micro */
static inline int64_t pd0_rng_noise(pd0_rng *r, int64_t sigma_micro)
{
    int64_t s = pd0_rng_unit(r) + pd0_rng_unit(r) + pd0_rng_unit(r) + pd0_rng_unit(r) - 2000000;
    return (s * sigma_micro) / 577350;
}
#endif
