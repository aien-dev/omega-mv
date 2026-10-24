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

/* tg_sgd.c -- plain C float32 SGD over a tg_store shadow. See tg_sgd.h. */
#include "train/tg_sgd.h"

#include <math.h>
#include <string.h>

static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

int tg_sgd_step(tg_store *st, tg_shadow *sh, const float *grad, size_t n,
                float lr, float momentum, uint64_t grad_ref)
{
    if (!st || !sh || !grad || n == 0 || n > (SIZE_MAX / 4)) return TG_E_ARG;
    if (!isfinite(lr) || !isfinite(momentum) || lr <= 0.0f || momentum < 0.0f || momentum >= 1.0f)
        return TG_E_ARG;
    if (sh->param_bytes != n * 4) return TG_E_ARG;
    int use_v = momentum != 0.0f;
    if (use_v && sh->opt_bytes != n * 4) return TG_E_ARG;

    tg_ref refs[TG_MAX_REFS];
    uint32_t nr = 0;
    refs[nr++] = (tg_ref){TG_BUF_PARAMS, 0, (uint64_t)n * 4};
    if (use_v) refs[nr++] = (tg_ref){TG_BUF_OPT, 0, (uint64_t)n * 4};
    refs[nr++] = (tg_ref){TG_BUF_EXTERNAL, grad_ref, (uint64_t)n * 4};
    uint64_t arg0 = (uint64_t)fbits(lr) | ((uint64_t)fbits(momentum) << 32);
    int r = tg_dispatch(st, sh, TG_OP_SGD, arg0, grad_ref, refs, nr);
    if (r) return r;

    float *p = (float *)(void *)sh->params;
    if (!use_v) {
        for (size_t i = 0; i < n; i++) {
            float step = lr * grad[i];
            p[i] = p[i] - step;
        }
    } else {
        float *v = (float *)(void *)sh->opt;
        for (size_t i = 0; i < n; i++) {
            float mv = momentum * v[i];
            v[i] = mv + grad[i];
            float step = lr * v[i];
            p[i] = p[i] - step;
        }
    }
    return TG_OK;
}
