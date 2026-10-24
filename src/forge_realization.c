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

#include "forge_realization.h"
#include "forge_realize.h"
#include <string.h>

int omega_realization_request_init(OmegaRealizationRequest       *req,
                                  const uint8_t                 *program_digest,
                                  uint32_t                       opcode_count,
                                  const uint8_t                 *ir_payload,
                                  size_t                         ir_size,
                                  const ForgeMachineDescriptor  *desc) {
    if (!req || !program_digest || !desc) return -1;
    memset(req, 0, sizeof(*req));
    memcpy(req->program_digest, program_digest, 32);
    req->opcode_count = opcode_count;
    req->ir_payload = ir_payload;
    req->ir_size = ir_size;
    req->machine_desc = *desc;

    return forge_descriptor_compute_digest(desc, req->descriptor_digest);
}

int omega_forge_realize(const OmegaRealizationRequest *req, ForgeRealizationResult *out_res) {
    if (!req || !out_res) return -1;

    ForgeRealizationRequest f_req;
    memset(&f_req, 0, sizeof(f_req));
    memcpy(f_req.program_digest, req->program_digest, 32);
    f_req.opcode_count = req->opcode_count;
    f_req.ir_payload = req->ir_payload;
    f_req.ir_size = req->ir_size;
    f_req.target_machine = req->machine_desc;
    memcpy(f_req.target_descriptor_digest, req->descriptor_digest, 32);

    return forge_realize(&f_req, out_res);
}

int omega_forge_submit(Nvrm                           *rm,
                       const ForgeVerifiedRealization *verified_real,
                       const ForgeMachineDescriptor   *live_desc,
                       ForgeExecutionEvidence         *out_evidence) {
    return forge_submit_realization(rm, verified_real, live_desc, out_evidence);
}
