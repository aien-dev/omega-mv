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

/* Report statistics for the BRW-ACT runner and its re-analysis tool (harness side, not candidate code).
 * Kept apart from brw_active_dev0.c so the unit test can check them. */
#ifndef BRW_ACT_REPORT_H
#define BRW_ACT_REPORT_H

#include <math.h>

/* Coverage over n worlds, k[i] of m held-out readings covered in world i. Mean and 95% interval.
 * The world is the independent unit (its readings share one fit), so the interval is the normal interval of
 * the n per-world proportions, clipped to [0, 1]. */
static inline void brwr_coverage(const double *k, int n, int m, double *mean, double *lo, double *hi)
{
    double s = 0, ss = 0;
    for (int i = 0; i < n; i++) s += k[i] / m;
    double p = s / n;
    for (int i = 0; i < n; i++) ss += (k[i] / m - p) * (k[i] / m - p);
    double h = n > 1 ? 1.959964 * sqrt(ss / (n - 1) / n) : 1.0;
    *mean = p;
    *lo = p - h < 0.0 ? 0.0 : p - h;
    *hi = p + h > 1.0 ? 1.0 : p + h;
}

/* P4 control verdicts: noise worlds always; diffusion (nested null) worlds only under DEV1. Returns both. */
static inline int brwr_p4(int claims_noise, int claims_diff, int nper, int dev1, int *noise_ok, int *diff_ok)
{
    int all = (double)claims_noise <= 0.05 * nper && (!dev1 || (double)claims_diff <= 0.05 * nper);
    *noise_ok = (double)claims_noise <= 0.05 * nper;
    *diff_ok = !dev1 || (double)claims_diff <= 0.05 * nper;
    return all;
}

#endif
