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

/* tg_sgd.h -- M22 "SGD first": plain float32 SGD (optional heavy-ball
 * momentum) applied to a tg_store SHADOW. The gradient comes from the caller
 * (no autodiff exists yet; M21). Adam/AdamW are deliberately deferred.
 *
 * Update, per element i, in this exact order (no FMA contraction; the
 * translation unit is built with -ffp-contract=off):
 *   momentum == 0:  p[i] = p[i] - lr * g[i]                (opt state untouched)
 *   momentum != 0:  v[i] = momentum * v[i] + g[i];  p[i] = p[i] - lr * v[i]
 *                   (v = optimizer state, n float32)
 * Each call appends one tier (a) dispatch record: op TG_OP_SGD, arg0 = lr and
 * momentum bit patterns, arg1 = caller's gradient reference id, input refs =
 * params, velocity (if used), external gradient. Nothing is hashed here. */
#ifndef OMEGA_TG_SGD_H
#define OMEGA_TG_SGD_H

#include "train/tg_store.h"

#define TG_OP_SGD 0x00444753u /* bytes "SGD\0" little-endian */

int tg_sgd_step(tg_store *st, tg_shadow *sh, const float *grad, size_t n,
                float lr, float momentum, uint64_t grad_ref);

#endif
