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

/* Positive control for gates/isolation.sh: a compute-like object that secretly
 * reads the wall clock. The gate must FAIL on this object. Never linked into
 * anything else. */
#include <time.h>
double at0_mutant_phase(double x) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return x + (double)(ts.tv_nsec % 7) * 1e-18;
}
