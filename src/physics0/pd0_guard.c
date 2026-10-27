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

#include "physics0/pd0_guard.h"

int pd0_guard_step_ok(const pd0_desc *d, uint8_t channel, int64_t value) {
    if (channel == PD0_CH_NONE) return 1;
    if (channel >= d->n_channels) return 0;
    return value >= d->chan_min[channel] && value <= d->chan_max[channel];
}

int pd0_guard_reset_ok(const pd0_desc *d, const int64_t *vals, uint8_t n_vals) {
    if (n_vals != d->n_obs) return 0;
    for (unsigned i = 0; i < n_vals; i++)
        if (vals[i] < d->reset_min[i] || vals[i] > d->reset_max[i]) return 0;
    return 1;
}

int pd0_guard_in_box(int64_t v) { return v <= PD0_BOUND && v >= -PD0_BOUND; }

