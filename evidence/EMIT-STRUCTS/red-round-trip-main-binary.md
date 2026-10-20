# EMIT-STRUCTS round trip (L093)

Commit: 787f456ee0c3606355d1ed3d3192cb53e2716d6a
Date: 2026-10-20T02:33:26Z

A = sha256 of the IR bytes oscc writes. B = sha256 of omega_emit emit(omega_emit lift(those bytes)).
RT passes when A == B. C2 passes when checker2 check gives the same ACCEPT line for both.

| file | IR version | A (oscc) | B (emit of lift) | RT | checker2 (oscc bytes) | checker2 (emitted bytes) | C2 |
|---|---|---|---|---|---|---|---|
| `tests/compiler/progs/arena_structs.osc` | 3 | `f9029fbc626e4a06b12f928b60ec3739c935c0d6afbafe0df9daba7580312bfc` | lift: REFUSED lift: structs are not in the op-list schema | FAIL | `checker2=ACCEPT ir_sha256=f9029fbc626e4a06b12f928b60ec3739c935c0d6afbafe0df9daba7580312bfc` | - | FAIL |
| `tests/compiler/progs/df_else.osc` | 2 | `5b63373b25b61efed48a7c3bc373de7e54f1ef076f9c0a291ee927ed883c6fcf` | lift: REFUSED lift: structs are not in the op-list schema | FAIL | `checker2=ACCEPT ir_sha256=5b63373b25b61efed48a7c3bc373de7e54f1ef076f9c0a291ee927ed883c6fcf` | - | FAIL |
| `tests/compiler/progs/df_order.osc` | 2 | `9755f0b22cc5b28c93f34b202eeab8558a3f826e60cad281c685531b9d4975c9` | lift: REFUSED lift: structs are not in the op-list schema | FAIL | `checker2=ACCEPT ir_sha256=9755f0b22cc5b28c93f34b202eeab8558a3f826e60cad281c685531b9d4975c9` | - | FAIL |
| `tests/compiler/progs/df_struct.osc` | 2 | `2779ddefa16b23586844f5b5ee6de2ba556549642343277626f6324612c35b09` | lift: REFUSED lift: structs are not in the op-list schema | FAIL | `checker2=ACCEPT ir_sha256=2779ddefa16b23586844f5b5ee6de2ba556549642343277626f6324612c35b09` | - | FAIL |
| `tests/compiler/progs/structs_contracts.osc` | 2 | `06b233f9e16e229fc606c8bda44b24e43473a2cd376053341e6ee644e124fe99` | lift: REFUSED lift: structs are not in the op-list schema | FAIL | `checker2=ACCEPT ir_sha256=06b233f9e16e229fc606c8bda44b24e43473a2cd376053341e6ee644e124fe99` | - | FAIL |
| `tests/compiler/progs/structs_dtor.osc` | 2 | `c7b4d22c623f0b1c4d004474c532ca74cbf23911cdcd299ad81843f4ef9a27df` | lift: REFUSED lift: structs are not in the op-list schema | FAIL | `checker2=ACCEPT ir_sha256=c7b4d22c623f0b1c4d004474c532ca74cbf23911cdcd299ad81843f4ef9a27df` | - | FAIL |
| `tests/compiler/progs/structs_fields.osc` | 2 | `865a9a09510cf16363e38345c47544ac1c9b48f7eddec67729f7e1ca40d03461` | lift: REFUSED lift: structs are not in the op-list schema | FAIL | `checker2=ACCEPT ir_sha256=865a9a09510cf16363e38345c47544ac1c9b48f7eddec67729f7e1ca40d03461` | - | FAIL |
| `tests/compiler/progs/structs_owner.osc` | 2 | `11dd47aca0387de3b7fc3f546272b165c9f2d2675626dda03a255a4dd4942807` | lift: REFUSED lift: structs are not in the op-list schema | FAIL | `checker2=ACCEPT ir_sha256=11dd47aca0387de3b7fc3f546272b165c9f2d2675626dda03a255a4dd4942807` | - | FAIL |
| `tools/omega_emit/structs/rt_array_field.osc` | 2 | `ebab81acf15b4ddb2237a23b6bbd6f92f7d0b78576e307fb9ab5614fa9179971` | lift: REFUSED lift: structs are not in the op-list schema | FAIL | `checker2=ACCEPT ir_sha256=ebab81acf15b4ddb2237a23b6bbd6f92f7d0b78576e307fb9ab5614fa9179971` | - | FAIL |
| `tools/omega_emit/structs/rt_call_ref.osc` | 2 | `b944a8f015eca289257a14e8520d7a33dddbfb131a9864221008e40236451250` | lift: REFUSED lift: structs are not in the op-list schema | FAIL | `checker2=ACCEPT ir_sha256=b944a8f015eca289257a14e8520d7a33dddbfb131a9864221008e40236451250` | - | FAIL |
| `tools/omega_emit/structs/rt_two_structs.osc` | 2 | `36244430565dfc85af913873b7750743c9d2849adcaee0dff7ce49f8681fd5f2` | lift: REFUSED lift: structs are not in the op-list schema | FAIL | `checker2=ACCEPT ir_sha256=36244430565dfc85af913873b7750743c9d2849adcaee0dff7ce49f8681fd5f2` | - | FAIL |

Gate RT: 0/11 byte-identical. Gate C2: 0/11 checker2 ACCEPT with the same line on both paths.
