# Gate NEG (L094): struct refusals, red before green

Cases: tests/one_layer/omegese/structs/refusals.txt (each a bad program plus its fixed control). Runner:
`bash tools/omegese/neg_structs.sh <omegese> <out>`. Branch feat/omegese-structs 50a45823.

## Red: omegese of origin/main a8ffd2b9 (before structs)

| case | expected refusal | omegese said | control | result |
|---|---|---|---|---|
| 1 | line 4: P has no field 'z' | REFUSED omegese: line 1: 'struct' outside a function (start one with fn) | refused | FAIL |
| 2 | line 8: 'q' is own Q; total takes view P there | REFUSED omegese: line 1: 'struct' outside a function (start one with fn) | refused | FAIL |
| 3 | line 3: 'p' is view only; put needs an own or edit struct | REFUSED omegese: line 1: 'struct' outside a function (start one with fn) | refused | FAIL |
| 4 | line 1: struct P needs end after its fields | REFUSED omegese: line 1: 'struct' outside a function (start one with fn) | refused | FAIL |
| 5 | line 1: field x is declared twice in P | REFUSED omegese: line 1: 'struct' outside a function (start one with fn) | refused | FAIL |
| 6 | line 2: struct declarations come before the first fn | REFUSED omegese: line 2: 'P' is not a name in scope here, nor an op, a type or a function | refused | FAIL |
| 7 | line 3: field x of P holds uint; 'b' is bool | REFUSED omegese: line 1: 'struct' outside a function (start one with fn) | refused | FAIL |
| 8 | line 3: new has 2 values after it; it takes 3 | REFUSED omegese: line 1: 'struct' outside a function (start one with fn) | refused | FAIL |
| 9 | line 3: get has 2 values after it; it takes 3 | REFUSED omegese: line 1: 'struct' outside a function (start one with fn) | refused | FAIL |
| 10 | line 5: 'p' holds no array here | REFUSED omegese: line 1: 'struct' outside a function (start one with fn) | refused | FAIL |
| 11 | line 2: a function returns a number or bool, not an array | REFUSED omegese: line 1: 'struct' outside a function (start one with fn) | refused | FAIL |

struct refusals: 0/11 refused with the expected reason and place, control accepted -> FAIL, as it must

## Green: omegese of this branch

| case | expected refusal | omegese said | control | result |
|---|---|---|---|---|
| 1 | line 4: P has no field 'z' | REFUSED omegese: line 4: P has no field 'z' | accepted | PASS |
| 2 | line 8: 'q' is own Q; total takes view P there | REFUSED omegese: line 8: 'q' is own Q; total takes view P there | accepted | PASS |
| 3 | line 3: 'p' is view only; put needs an own or edit struct | REFUSED omegese: line 3: 'p' is view only; put needs an own or edit struct | accepted | PASS |
| 4 | line 1: struct P needs end after its fields | REFUSED omegese: line 1: struct P needs end after its fields | accepted | PASS |
| 5 | line 1: field x is declared twice in P | REFUSED omegese: line 1: field x is declared twice in P | accepted | PASS |
| 6 | line 2: struct declarations come before the first fn | REFUSED omegese: line 2: struct declarations come before the first fn | accepted | PASS |
| 7 | line 3: field x of P holds uint; 'b' is bool | REFUSED omegese: line 3: field x of P holds uint; 'b' is bool | accepted | PASS |
| 8 | line 3: new has 2 values after it; it takes 3 | REFUSED omegese: line 3: new has 2 values after it; it takes 3 | accepted | PASS |
| 9 | line 3: get has 2 values after it; it takes 3 | REFUSED omegese: line 3: get has 2 values after it; it takes 3 | accepted | PASS |
| 10 | line 5: 'p' holds no array here | REFUSED omegese: line 5: 'p' holds no array here: it was freed or moved on every path to this line | accepted | PASS |
| 11 | line 2: a function returns a number or bool, not an array | REFUSED omegese: line 2: a function returns a number or bool, not an array | accepted | PASS |

struct refusals: 11/11 refused with the expected reason and place, control accepted -> PASS
