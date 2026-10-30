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

/* Independent analytic reference for the DUAL-1a update, derived from ADR 0031
 * section 5.1 on paper, not from rx_dual_update.c. Closed forms for a constant
 * input: with g constant and no clipping, the recursion
 *   l(t+1) = (1 - rho) l(t) + eta g
 * has the solution
 *   l(t) = (1 - rho)^t l(0) + eta g (1 - (1 - rho)^t) / rho       (rho > 0)
 *   l(t) = l(0) + eta g t                                          (rho = 0)
 * and the fixed point l* = eta g / rho. Clipping to [0, lambda_max] is applied
 * per step, so the closed form holds exactly while the trajectory stays inside
 * the bounds, and the implementation must saturate at the bound afterwards. */
#ifndef DUAL_REF_H
#define DUAL_REF_H
double dual_ref_gradient(double estimate, double budget, double scale, double uncertainty, double k_sigma);
double dual_ref_closed_form(double l0, double g, double eta, double rho, unsigned long t);
double dual_ref_fixed_point(double g, double eta, double rho);
#endif
