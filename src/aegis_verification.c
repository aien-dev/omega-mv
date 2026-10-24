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

#include "aegis_verification.h"
#include "forge_realize.h"
#include <string.h>

int omega_aegis_verify(const OmegaRealizationRequest  *req,
                       const ForgeRealizationResult   *res,
                       const ForgeMachineDescriptor   *desc,
                       ForgeVerifiedRealization       *out_verified) {
    if (!req || !res || !desc || !out_verified) return -1;

    ForgeRealizationRequest f_req;
    memset(&f_req, 0, sizeof(f_req));
    memcpy(f_req.program_digest, req->program_digest, 32);
    f_req.opcode_count = req->opcode_count;
    f_req.ir_payload = req->ir_payload;
    f_req.ir_size = req->ir_size;
    f_req.target_machine = req->machine_desc;
    memcpy(f_req.target_descriptor_digest, req->descriptor_digest, 32);

    return aegis_verify_realization(&f_req, res, desc, out_verified);
}
