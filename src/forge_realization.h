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

#ifndef FORGE_REALIZATION_H
#define FORGE_REALIZATION_H

#include "forge_descriptor.h"
#include "forge_types.h"
#include "nvrm.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Omega-side typed realization request wrapper.
 */
typedef struct {
    uint8_t                program_digest[32];
    uint32_t               opcode_count;
    const uint8_t         *ir_payload;
    size_t                 ir_size;
    ForgeMachineDescriptor machine_desc;
    uint8_t                descriptor_digest[32];
} OmegaRealizationRequest;

/*
 * Constructs an OmegaRealizationRequest bound to an observed ForgeMachineDescriptor.
 */
int omega_realization_request_init(OmegaRealizationRequest       *req,
                                  const uint8_t                 *program_digest,
                                  uint32_t                       opcode_count,
                                  const uint8_t                 *ir_payload,
                                  size_t                         ir_size,
                                  const ForgeMachineDescriptor  *desc);

/*
 * Dispatches realization lowering to FORGE.
 * Produces ForgeRealizationResult (unverified).
 */
int omega_forge_realize(const OmegaRealizationRequest *req, ForgeRealizationResult *out_res);

/*
 * Submits a verified realization to hardware through the typed FORGE boundary.
 * Strictly requires ForgeVerifiedRealization.
 */
int omega_forge_submit(Nvrm                           *rm,
                       const ForgeVerifiedRealization *verified_real,
                       const ForgeMachineDescriptor   *live_desc,
                       ForgeExecutionEvidence         *out_evidence);

#endif /* FORGE_REALIZATION_H */
