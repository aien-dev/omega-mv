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

/* Omega mixed algebra: Z3 (integers mod 3, values {0,1,2}).
 * Map to balanced trits: 0 <-> 0, 1 <-> +1, 2 <-> -1. This map is a ring
 * isomorphism: Z3 add = balanced-add sum trit (carry dropped), Z3 mul = H2. */
#ifndef OMA_Z3_H
#define OMA_Z3_H

#include "algebra/oma_trit.h"

typedef uint8_t oma_z3; /* one of 0, 1, 2 */

/* Scalar inputs are int: out-of-range values are rejected, never wrapped. */
int oma_z3_make(int v, oma_z3 *out);
int oma_z3_add(int a, int b, oma_z3 *out);
int oma_z3_mul(int a, int b, oma_z3 *out);
int oma_z3_neg(int a, oma_z3 *out);
int oma_z3_to_trit(int a, oma_trit *out);
int oma_trit_to_z3(int t, oma_z3 *out);

/* Z3 block on bitplanes: {one, two}, lane i = bit i. Under the map above
 * it is bit-identical to an oma_block ({pos, neg}); (1,1) is invalid. */
typedef struct {
    uint64_t one;
    uint64_t two;
} oma_z3_block;

int oma_z3_block_validate(const oma_z3_block *b);
int oma_z3_block_encode(const uint8_t in[OMA_BLOCK_TRITS], oma_z3_block *out);
int oma_z3_block_decode(const oma_z3_block *b, uint8_t out[OMA_BLOCK_TRITS]);
int oma_z3_block_from_trits(const oma_block *t, oma_z3_block *out);
int oma_z3_block_to_trits(const oma_z3_block *z, oma_block *out);
/* Carry-free lane-wise ops. */
int oma_z3_block_add(const oma_z3_block *a, const oma_z3_block *b, oma_z3_block *out);
int oma_z3_block_mul(const oma_z3_block *a, const oma_z3_block *b, oma_z3_block *out);
int oma_z3_block_neg(const oma_z3_block *a, oma_z3_block *out);

#endif /* OMA_Z3_H */
