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

#include "dual_ref.h"
#include <math.h>
double dual_ref_gradient(double estimate, double budget, double scale, double uncertainty, double k_sigma)
{
    double p = (estimate - budget) / scale, band = k_sigma * uncertainty / scale;
    if (p > band) return p - band;
    if (p < -band) return p + band;
    return 0.0;
}
double dual_ref_closed_form(double l0, double g, double eta, double rho, unsigned long t)
{
    double a;
    if (rho == 0.0) return l0 + eta * g * (double)t;
    a = pow(1.0 - rho, (double)t);
    return a * l0 + eta * g * (1.0 - a) / rho;
}
double dual_ref_fixed_point(double g, double eta, double rho) { return eta * g / rho; }
