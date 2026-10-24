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

/* Byte-level handle to an in-process PD-0 world for the v2 dev drivers (harness side). The driver sees
 * request and response BYTES only; the generator, constants and world struct stay inside pd0_dev_world.c.
 * The same bytes the pipe transport carries (spec 2.3). */
#ifndef PD0_DEV_WORLD_H
#define PD0_DEV_WORLD_H
#include <stddef.h>
#include <stdint.h>
typedef struct dev_world dev_world;
dev_world *dev_world_open(int level, uint64_t seed);               /* level 0..6, or 7 for the null world */
size_t dev_world_call(dev_world *w, const uint8_t *req, size_t n, uint8_t *resp, size_t cap);
int dev_world_switch(dev_world *w, uint32_t regime, int64_t *max_change_ppm);
void dev_world_close(dev_world *w);
#endif
