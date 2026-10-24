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
 * osc_lower.h -- OSC-1 lowering: checked AST (osc_check annotations) ->
 * OscUnit IR (docs/osc/OSC-1-DESIGN.md section 4). Blocks, short-circuit
 * && / ||, explicit while counters with OSC_I_TRAP 4, RELEASE at every scope
 * exit (reverse declaration order, early returns included), bounds-checked
 * LOAD/STORE (checked by the IR semantics), CALL.
 * OSC-1 slice; not a general Omega compiler; no self-hosting.
 */
#ifndef OSC_LOWER_H
#define OSC_LOWER_H

#include "osc_alloc.h"
#include "osc_diag.h"
#include "osc_ir.h"
#include "osc_parse.h"

/* 0 ok; -1 with *d filled (CAPACITY when a function exceeds the IR limits, or `a` refuses the
 * lowering state). `a` is not NULL here. */
int osc_lower(const OscAlloc *a, const OscAst *ast, OscUnit *out, OscDiag *d);

#endif /* OSC_LOWER_H */
