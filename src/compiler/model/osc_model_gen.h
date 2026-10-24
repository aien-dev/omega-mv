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

/*
 * osc_model_gen.h -- seeded deterministic sequence generator for the II.11
 * model (OSC-0B exit gate).
 *
 * The generator keeps its own shadow state (independent of osc_model.c) and
 * uses it to choose only VALID events. For an injected sequence it then
 * builds (with valid setup events if needed) exactly one labelled invalid
 * event of the requested named class, followed by a few more valid events
 * (a rejected event must not change the model's state). The label is the
 * ground truth the model's verdict is compared against.
 *
 * Fully determined by (seed, index, inject_class): splitmix64 over
 * seed ^ golden * (index + 1). No globals, no malloc.
 */
#ifndef OSC_MODEL_GEN_H
#define OSC_MODEL_GEN_H

#include <stdint.h>
#include "osc_model.h"

#define OSC_GEN_MAX_EVENTS 256

typedef struct {
    uint64_t gen_base, gen_max;   /* pass to osc_model_init */
    int inject_at;                /* index of the invalid event, -1 = none */
    OscModelReject expect;        /* label at inject_at (OSC_REJ_NONE if none) */
    int variant;                  /* which construction of the class was used */
    int n;
    OscModelEvent ev[OSC_GEN_MAX_EVENTS];
} OscGenSeq;

uint64_t osc_gen_splitmix64(uint64_t *state);

/* inject_class: OSC_REJ_NONE for an all-valid sequence, else 1..13.
 * Returns 0 on success, -1 if the requested injection could not be built
 * (never expected; the caller counts it as a generator failure). */
int osc_gen_sequence(uint64_t seed, uint64_t index, OscModelReject inject_class, OscGenSeq *out);

#endif
