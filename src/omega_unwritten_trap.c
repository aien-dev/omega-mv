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

#include "omega_unwritten_trap.h"
#include <string.h>

void omega_unwritten_scan(const uint32_t *out, size_t n, uint32_t poison, uint32_t block,
                          OmegaUnwrittenReport *r) {
    memset(r, 0, sizeof(*r));
    if (!out || block == 0) return;
    uint64_t run = 0;
    for (size_t b = 0; b < n; b += block) {
        size_t end = b + block < n ? b + block : n;
        uint64_t hits = 0;
        for (size_t i = b; i < end; i++) {
            if (out[i] == poison) {
                if (r->count == 0) r->first = i;
                r->last = i;
                r->count++;
                hits++;
                if (run == 0) r->runs++;
                run++;
                if (run > r->longest_run) r->longest_run = run;
            } else {
                run = 0;
            }
        }
        if (hits == end - b) r->full_blocks++;
        else if (hits) r->partial_blocks++;
    }
    r->whole_blocks = r->count > 0 && r->partial_blocks == 0;
    r->tail = r->count > 0 && r->last == n - 1 && r->count == (uint64_t)n - r->first;
}
