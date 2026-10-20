# EMIT-STRUCTS round trip (L093)

Commit: 597eb1e6b5eb6a05df4cdb7bf0c27401d19f099d
Date: 2026-10-20T00:41:33Z

A = sha256 of the IR bytes oscc writes. B = sha256 of omega_emit emit(omega_emit lift(those bytes)).
RT passes when A == B. C2 passes when checker2 check gives the same ACCEPT line for both.

| file | IR version | A (oscc) | B (emit of lift) | RT | checker2 (oscc bytes) | checker2 (emitted bytes) | C2 |
|---|---|---|---|---|---|---|---|
| `tests/compiler/progs/arena_structs.osc` | 3 | `f9029fbc626e4a06b12f928b60ec3739c935c0d6afbafe0df9daba7580312bfc` | `f9029fbc626e4a06b12f928b60ec3739c935c0d6afbafe0df9daba7580312bfc` | PASS | `checker2=ACCEPT ir_sha256=f9029fbc626e4a06b12f928b60ec3739c935c0d6afbafe0df9daba7580312bfc` | `checker2=ACCEPT ir_sha256=f9029fbc626e4a06b12f928b60ec3739c935c0d6afbafe0df9daba7580312bfc` | PASS |
| `tests/compiler/progs/df_else.osc` | 2 | `5b63373b25b61efed48a7c3bc373de7e54f1ef076f9c0a291ee927ed883c6fcf` | `5b63373b25b61efed48a7c3bc373de7e54f1ef076f9c0a291ee927ed883c6fcf` | PASS | `checker2=ACCEPT ir_sha256=5b63373b25b61efed48a7c3bc373de7e54f1ef076f9c0a291ee927ed883c6fcf` | `checker2=ACCEPT ir_sha256=5b63373b25b61efed48a7c3bc373de7e54f1ef076f9c0a291ee927ed883c6fcf` | PASS |
| `tests/compiler/progs/df_order.osc` | - | oscc refused: error kind=unit_container message=function eata parameter register 0 has kind 10, which a unit container refuses | - | FAIL | - | - | FAIL |
| `tests/compiler/progs/df_struct.osc` | - | oscc refused: error kind=unit_container message=function eat parameter register 0 has kind 10, which a unit container refuses | - | FAIL | - | - | FAIL |
| `tests/compiler/progs/structs_contracts.osc` | - | oscc refused: error kind=unit_container message=function width parameter register 0 has kind 10, which a unit container refuses | - | FAIL | - | - | FAIL |
| `tests/compiler/progs/structs_dtor.osc` | 2 | `c7b4d22c623f0b1c4d004474c532ca74cbf23911cdcd299ad81843f4ef9a27df` | `c7b4d22c623f0b1c4d004474c532ca74cbf23911cdcd299ad81843f4ef9a27df` | PASS | `checker2=ACCEPT ir_sha256=c7b4d22c623f0b1c4d004474c532ca74cbf23911cdcd299ad81843f4ef9a27df` | `checker2=ACCEPT ir_sha256=c7b4d22c623f0b1c4d004474c532ca74cbf23911cdcd299ad81843f4ef9a27df` | PASS |
| `tests/compiler/progs/structs_fields.osc` | 2 | `865a9a09510cf16363e38345c47544ac1c9b48f7eddec67729f7e1ca40d03461` | `865a9a09510cf16363e38345c47544ac1c9b48f7eddec67729f7e1ca40d03461` | PASS | `checker2=ACCEPT ir_sha256=865a9a09510cf16363e38345c47544ac1c9b48f7eddec67729f7e1ca40d03461` | `checker2=ACCEPT ir_sha256=865a9a09510cf16363e38345c47544ac1c9b48f7eddec67729f7e1ca40d03461` | PASS |
| `tests/compiler/progs/structs_owner.osc` | - | oscc refused: error kind=unit_container message=function total parameter register 0 has kind 10, which a unit container refuses | - | FAIL | - | - | FAIL |
| `tools/omega_emit/structs/rt_array_field.osc` | - | oscc refused: error kind=SYNTAX line=2 col=22 object=cells origin_line=0 other= transition=a field name | - | FAIL | - | - | FAIL |
| `tools/omega_emit/structs/rt_call_ref.osc` | - | oscc refused: error kind=unit_container message=function peek parameter register 0 has kind 10, which a unit container refuses | - | FAIL | - | - | FAIL |
| `tools/omega_emit/structs/rt_two_structs.osc` | 2 | `36244430565dfc85af913873b7750743c9d2849adcaee0dff7ce49f8681fd5f2` | `36244430565dfc85af913873b7750743c9d2849adcaee0dff7ce49f8681fd5f2` | PASS | `checker2=ACCEPT ir_sha256=36244430565dfc85af913873b7750743c9d2849adcaee0dff7ce49f8681fd5f2` | `checker2=ACCEPT ir_sha256=36244430565dfc85af913873b7750743c9d2849adcaee0dff7ce49f8681fd5f2` | PASS |

Gate RT: 5/11 byte-identical. Gate C2: 5/11 checker2 ACCEPT with the same line on both paths.
