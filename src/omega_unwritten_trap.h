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
 * C3 unwritten-output trap: host-only detector (no GPU, no libm).
 *
 * The caller pre-fills an output buffer with a poison word that the op under
 * test cannot emit, runs the work, and calls omega_unwritten_scan. A word that
 * still equals the poison was never stored (or its store was not visible).
 * Whole-block means every aligned group of `block` words that contains a hit is
 * entirely poison (the last group may be short), which is the signature of whole
 * missing 64-thread CTAs rather than scattered lost stores.
 */
#ifndef OMEGA_UNWRITTEN_TRAP_H
#define OMEGA_UNWRITTEN_TRAP_H

#include <stddef.h>
#include <stdint.h>

#define OMEGA_UNWRITTEN_BLOCK 64u

typedef struct {
    uint64_t count;          /* words equal to the poison                       */
    uint64_t first, last;    /* index of first and last hit (valid if count>0)  */
    uint64_t runs;           /* maximal contiguous runs of hits                 */
    uint64_t longest_run;    /* length of the longest run                       */
    uint64_t full_blocks;    /* aligned blocks that are entirely poison         */
    uint64_t partial_blocks; /* aligned blocks with some but not all poison     */
    int whole_blocks;        /* count>0 and partial_blocks==0                   */
    int tail;                /* count>0 and hits are exactly [first, n)         */
} OmegaUnwrittenReport;

/* Scans out[0..n). block must be > 0 (use OMEGA_UNWRITTEN_BLOCK for CTAs). */
void omega_unwritten_scan(const uint32_t *out, size_t n, uint32_t poison, uint32_t block,
                          OmegaUnwrittenReport *r);

#endif
