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

/* PRD1 prediction file reader/validator/encoder (little-endian).
 * Header: "PRD1", u32 version = 1. Then one record per held-out index:
 * u32 index, u32 family, f64 loc, f64 scale. Wave 1 accepts family 0 only;
 * every other family and every malformed byte pattern is TYQ_FAIL_PROTOCOL. */
#ifndef TURING_TY_PRD_H
#define TURING_TY_PRD_H

#include "turing/ty_qcont.h"

typedef struct { uint32_t n, first_index; tyq_pred *rec; } ty_prd;

/* Refusals: family != 0, non-finite loc or scale, scale <= 0. */
int  ty_prd_validate(const tyq_pred *p);
/* The indices must be exactly first_index .. first_index+count-1, in order. */
int  ty_prd_parse(const uint8_t *buf, size_t len, uint32_t first_index, uint32_t count, ty_prd *out);
int  ty_prd_read(const char *path, uint32_t first_index, uint32_t count, ty_prd *out);
int  ty_prd_encode(const ty_prd *p, uint8_t **buf, size_t *len);
void ty_prd_free(ty_prd *p);

#endif
