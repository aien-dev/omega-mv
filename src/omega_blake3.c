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

/* BLAKE3 is implemented in Omega: om/blake3/b3unit.om, compiled by the seeded compiler and bound
 * through the omega-bridge native call ABI v1 as omega_omb3 (generated into src/omega_om_units.inc,
 * which src/sha256.c compiles; regenerate with om/units.sh). This file only cuts the input into
 * 1024-byte chunks, because ABI v1 arrays have a fixed length. No hashing logic lives here. */
#include "omega_blake3.h"

#include <stdlib.h>
#include <string.h>

#define OMB3_CHUNK 1024u
#define OMB3_STATE 448u

uint32_t omega_omb3(uint64_t *result, void *st, uint64_t st_len, const void *b, uint64_t b_len, uint64_t n,
                    uint64_t fin, void *o, uint64_t o_len);

/* A trap or refused argument can only mean a broken invariant (chunk sizes, stack depth), never a
 * recoverable condition, so it aborts like a failed assert. */
static void omb3_call(uint64_t *st, const uint8_t *chunk, size_t n, unsigned fin, uint8_t out[32])
{
    uint64_t unused;
    if (omega_omb3(&unused, st, OMB3_STATE, chunk, OMB3_CHUNK, n, fin, out, 32) != 0)
        abort();
}

void omega_blake3_hash(const uint8_t *data, size_t len, uint8_t out[32])
{
    uint64_t st[OMB3_STATE];
    memset(st, 0, sizeof st);
    while (len > OMB3_CHUNK) {
        omb3_call(st, data, OMB3_CHUNK, 0, out);
        data += OMB3_CHUNK;
        len -= OMB3_CHUNK;
    }
    uint8_t last[OMB3_CHUNK]; /* only last[0..len) is read */
    if (len > 0)
        memcpy(last, data, len);
    omb3_call(st, last, len, 1, out);
}
