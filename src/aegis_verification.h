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

#ifndef AEGIS_VERIFICATION_H
#define AEGIS_VERIFICATION_H

#include "forge_realization.h"
#include <stdbool.h>

/*
 * Omega Aegis Verification API:
 * Verifies that a lowered ForgeRealizationResult satisfies all contracts
 * and invariants before granting a ForgeVerifiedRealization token.
 */
int omega_aegis_verify(const OmegaRealizationRequest  *req,
                       const ForgeRealizationResult   *res,
                       const ForgeMachineDescriptor   *desc,
                       ForgeVerifiedRealization       *out_verified);

#endif /* AEGIS_VERIFICATION_H */
