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

/* PD-0 dev-only regime switch (adaptive-intelligence milestone 3, gap G3 / C2).
 * World side (links the generator). The seal-1 world and generator are untouched: this file
 * redraws the world's constants from the SAME declared ranges (pd0_gen_init) with a seed derived from
 * (world seed, regime), so truth stays hidden behind the guard and the learner only sees records.
 * Regime 0 is the world as built; regime r >= 1 is a new draw. Not part of any sealed run. */
#ifndef PD0_REGIME_H
#define PD0_REGIME_H
#define PD0_WORLD_IMPL 1
#include "physics0/pd0_world.h"

/* Redraw the constants for `regime` (>= 1) and renew the step/episode budget (dev: budgets are per regime).
 * Any open episode is abandoned. The hash chain and noise streams continue.
 * Returns 0 ok, -1 if no valid draw was found. *max_change_ppm (may be NULL) receives the largest relative change of any
 * constant in parts per million (0 when the level has no declared constants: L0 and NULL). */
int pd0_world_switch_regime(pd0_world *w, int level, uint32_t regime, int64_t *max_change_ppm);
#endif
