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

#ifndef OMEGA_WORLD_GATES_H
#define OMEGA_WORLD_GATES_H

/* Runs all 18 qualification gates for Milestone 19. Returns 0 if all pass, 1 on any failure */
int run_m19_gates(void);

/* Standalone silicon test (not a canonical gate): completion payloads are
 * deliberately decoupled from the GPFIFO put sequence. Returns 0 on pass. */
int run_m19_drain_decoupling(void);

/* M19R world lifecycle gates (memory release, quarantine, fault latch,
 * monotonic completion). Returns 0 if all pass. */
int run_world_lifecycle_gates(void);
int run_m19r_soak(void);

/* Runs persistent accelerator world physical silicon demonstration */
void run_demonstration_accelerator_world(void);

#endif /* OMEGA_WORLD_GATES_H */
