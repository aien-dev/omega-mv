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

/* DUAL-1b pre-registered scenario traces (evidence/DUAL/1b-replay/receipt.md,
 * section "Pre-registered: scenarios"). Every generator is a pure function of
 * its declared parameters and seed; the trace digest is the scenario identity. */
#ifndef DUAL_TRACES_H
#define DUAL_TRACES_H
#include "rx_dual_replay.h"

#define DUAL_TRACE_N 200u
#define DUAL_SD 100.0
#define DUAL_BUDGET 5000.0
#define DUAL_SCALE 1000.0

/* Scenario 1..6, 8, 9, 11, 12 by number; 7 by part ('A' or 'B'); 10 by resource index r (0..31). */
RxDualStatus dual_trace_constant(RxDualTraceKind kind, uint64_t seed, uint32_t n, uint64_t gen0, double est, double sd, RxDualTrace *out);
RxDualStatus dual_trace_scenario(int scenario, char part, uint32_t r, RxDualTrace *out);
#endif
