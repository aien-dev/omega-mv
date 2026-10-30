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

/* Exhaustive breadth-first exploration of the lifecycle model. */
#ifndef WORLD_EXPLORE_H
#define WORLD_EXPLORE_H
#include "world_model.h"

#define WX_MAX_TRACE 64
typedef struct {
    uint64_t states, transitions;
    uint32_t viol_seen;                     /* OR of every violation bit */
    int      trace_len[5];                  /* shortest counterexample per bit */
    WmOp     trace[5][WX_MAX_TRACE];
    int      truncated;                     /* state table full: not exhaustive */
} WxResult;

/* Bit index 0..4 for V_* bits. */
int  wx_bit_index(uint32_t bit);
int  wx_explore(const WmProfile *p, WxResult *res);
void wx_print_trace(const WmOp *t, int n);
#endif
