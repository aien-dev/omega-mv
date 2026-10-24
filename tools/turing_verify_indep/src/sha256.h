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

/* Independent SHA-256 (FIPS 180-4), lane D scorer. */
#ifndef IS_SHA256_H
#define IS_SHA256_H
#include <stddef.h>
#include <stdint.h>
typedef struct { uint32_t h[8]; uint64_t len; uint8_t buf[64]; size_t n; } is_sha256;
void is_sha256_init(is_sha256 *c);
void is_sha256_update(is_sha256 *c, const void *p, size_t len);
void is_sha256_final(is_sha256 *c, uint8_t out[32]);
void is_sha256_hex(const uint8_t d[32], char out[65]);
int is_sha256_file(const char *path, uint8_t out[32], uint64_t *bytes);
#endif
