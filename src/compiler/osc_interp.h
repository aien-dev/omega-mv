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
 * osc_interp.h -- reference interpreter for the OSC-1 typed IR.
 * Semantics: docs/osc/OSC-1-DESIGN.md section 5, exactly. Allocation and
 * release go through osc_rt_alloc / osc_rt_release so the pool event log is
 * identical to natively compiled code.
 * OSC-1 slice; not a general Omega compiler; no self-hosting.
 */
#ifndef OSC_INTERP_H
#define OSC_INTERP_H

#include <stdint.h>
#include "osc_ir.h"
#include "osc_rt.h"

/* Defensive bound on executed IR instructions per run (all IR loops already
 * carry a static bound; exceeding this reports OSC_TRAP_RUNTIME). */
#define OSC_INTERP_MAX_STEPS (1ULL << 32)

/* Run function `func` of `u` with `nargs` arguments.
 * Returns -1 if refused (unit fails osc_ir_validate, bad func index, nargs !=
 * nparams, a scalar argument not canonical for its type, or a REF argument
 * that is not a live pool slot of matching length in rt); otherwise the trap
 * code (0 = normal return with *ret set; void functions return 0).
 * Does not reset rt: the caller resets the pool between runs. */
int osc_interp_run(const OscUnit *u, int func, const uint64_t *args, unsigned nargs, OscRt *rt, uint64_t *ret);

/* Same, but skips osc_ir_validate: only for a unit the caller has already
 * validated and not modified since (used by fuzz loops). */
int osc_interp_run_prevalidated(const OscUnit *u, int func, const uint64_t *args, unsigned nargs, OscRt *rt,
                                uint64_t *ret);

#endif /* OSC_INTERP_H */
