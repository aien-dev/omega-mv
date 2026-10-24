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

#ifndef OMEGA_BLAKE3_H
#define OMEGA_BLAKE3_H

/* omega_blake3: BLAKE3 default (unkeyed) hash, 32-byte output, one shot. VC1 stage 4.
 *
 * Why it exists: an aien-proof EvidenceReceiptV1 id is BLAKE3 over the receipt's canonical
 * bytes (aien-protocols specs/evidence-receipt/SUBSET.md section 1), and the resolver must
 * recompute that id itself (SPEC 5.1 rule 1). Portable C, no SIMD, no allocation, no state.
 * Verified against the official BLAKE3 test vectors (tests/resolve/blake3_vectors.txt, from
 * BLAKE3-team/BLAKE3 test_vectors/test_vectors.json, input = 0,1,..,250 repeating). */

#include <stddef.h>
#include <stdint.h>

void omega_blake3_hash(const uint8_t *data, size_t len, uint8_t out[32]);

#endif /* OMEGA_BLAKE3_H */
