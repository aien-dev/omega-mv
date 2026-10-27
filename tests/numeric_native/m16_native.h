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

#ifndef M16_NATIVE_H
#define M16_NATIVE_H
#include <stddef.h>
#include <stdint.h>
typedef struct { struct { char err[256]; } rm; } M16NativeContext;
int m16_native_open(M16NativeContext *ctx);
int m16_native_close(M16NativeContext *ctx);
int m16_native_wait_marker(volatile uint32_t *word, uint32_t want, uint64_t ms);
#endif
