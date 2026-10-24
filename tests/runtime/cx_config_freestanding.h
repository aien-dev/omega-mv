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

/* cx_config_freestanding.h -- allocation hooks for the CX_NO_POSIX build of
 * rx_cortex.c (tests/runtime/rx_cortex_journal.c supplies them). A real
 * freestanding host (AIENOS) names its own header through CX_CONFIG_H. */
#ifndef CX_CONFIG_FREESTANDING_H
#define CX_CONFIG_FREESTANDING_H
#include <stddef.h>
void *cx_fs_alloc(size_t n);
void *cx_fs_realloc(void *p, size_t n);
void cx_fs_free(void *p);
#define CX_MALLOC(n) cx_fs_alloc(n)
#define CX_REALLOC(p, n) cx_fs_realloc((p), (n))
#define CX_FREE(p) cx_fs_free(p)
#endif
