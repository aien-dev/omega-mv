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

/* See pd0_dev_world.h. Links the generator; never linked into a learner. */
#include "pd0_dev_world.h"
#include "physics0/regime/pd0_regime.h"
#include <stdlib.h>

struct dev_world { pd0_world w; int level; };
dev_world *dev_world_open(int level, uint64_t seed)
{ dev_world *d = calloc(1, sizeof *d); if (!d) return NULL; d->level = level; if (pd0_world_init(&d->w, level, seed)) { free(d); return NULL; } return d; }
size_t dev_world_call(dev_world *d, const uint8_t *req, size_t n, uint8_t *resp, size_t cap) { return pd0_world_handle(&d->w, req, n, resp, cap); }
int dev_world_switch(dev_world *d, uint32_t regime, int64_t *ppm) { return pd0_world_switch_regime(&d->w, d->level, regime, ppm); }
void dev_world_close(dev_world *d) { free(d); }
