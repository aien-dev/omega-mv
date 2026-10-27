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

#ifndef OMEGA_TENSOR_REDUCE_SEAM_H
#define OMEGA_TENSOR_REDUCE_SEAM_H

/*
 * The one place where tensor reductions meet the E1 reduction contract.
 *
 * The order is OMEGA_TENSOR_REDUCE_DECLARED_ORDER (omega_tensor.h), the same
 * string as OMEGA_REDUCE_DECLARED_ORDER in src/omega_numeric_reduce.h of the
 * E1 WP-D reductions (omega PR #134, docs/numeric/E1_REDUCTION_CONTRACT.md):
 *
 *   level 0 values are x[0..n). One level turns len values into ceil(len/32)
 *   values: value j of the next level is the 32-lane warp tree (lane i
 *   combines with lane i+d for d = 16, 8, 4, 2, 1, lower lane on the left)
 *   over values [32j, 32j+32) of this level, positions >= len filled with the
 *   identity (SUM/MEAN: -0.0, MAX/MIN: quiet NaN). At least one level is
 *   applied; levels repeat until one value is left. MEAN = omega_math_div(SUM,
 *   (float)n), 1 <= n <= 2^24.
 *
 * The seam calls omega_reduce_cpu (src/omega_numeric_reduce.h, E1 WP-D,
 * omega #134); the local copy of the order used before #134 merged is gone.
 * The test checks the order string and the bits against an independent
 * reference of the definition.
 */

#include <stddef.h>
#include "omega_tensor.h"

/* Returns 0 or an OMEGA_NUMERIC_* code. n >= 1. */
int omega_tensor_seam_reduce_cpu(OmegaTensorReduceOp op, const float *x, size_t n, float *out);
/* Reduction source linked: "E1_WP_D_OMEGA_REDUCE_CPU". */
const char *omega_tensor_seam_reduce_source(void);

#endif /* OMEGA_TENSOR_REDUCE_SEAM_H */
