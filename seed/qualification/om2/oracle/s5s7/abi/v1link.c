/* SPDX-License-Identifier: Apache-2.0
 * OM-2 qualification, lane s5s7: v1 link refusal ORACLE (contract 6 lines 182-184, erratum E1).
 * abiv is an OM-2 unit, so the binding emits omega2_vsum and NO omega_vsum: linking this file
 * against the library MUST fail with an undefined reference to omega_vsum. A successful link is
 * a FAIL (the v1 symbol leaked). */
#include <stdint.h>
uint32_t omega_vsum(uint64_t *result, const void *v, uint64_t v_len);
int main(void)
{
    uint64_t r = 0, w[1] = {1};
    return (int)omega_vsum(&r, w, 1);
}
