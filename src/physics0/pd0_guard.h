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

/* STAND_IN range guard (AEGIS stand-in, PHYSICS0_DISCOVERY_ENGINE.md section
 * 4.2: "stand-in range check", label STAND_IN in every receipt). Pure
 * functions over the describe record: an intervention or reset request is
 * either inside the declared bounds or refused. A refusal is itself a recorded
 * outcome (status REFUSED_RANGE) and still costs the step or episode
 * (spec 2.3). No generator knowledge. */
#ifndef PD0_GUARD_H
#define PD0_GUARD_H

#include <stdint.h>

#include "physics0/pd0_wire.h"

#define PD0_GUARD_LABEL "STAND_IN"

/* 1 if (channel, value) is inside the bounds: channel PD0_CH_NONE always ok;
 * otherwise channel < n_channels and chan_min <= value <= chan_max */
int pd0_guard_step_ok(const pd0_desc *d, uint8_t channel, int64_t value);
/* 1 if n_vals == n_obs and every value i is in [reset_min[i], reset_max[i]] (per-variable box, PD0DESC2) */
int pd0_guard_reset_ok(const pd0_desc *d, const int64_t *vals, uint8_t n_vals);
/* 1 if a post-step observed or hidden value is inside |v| <= PD0_BOUND */
int pd0_guard_in_box(int64_t v);

#endif
