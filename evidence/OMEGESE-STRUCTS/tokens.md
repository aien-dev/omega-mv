# Struct spelling measurement (L094)

Phase 1 method (docs/one_layer/OMEGESE.md section 5): Qwen3 tokenizer, offline (`tokcount`, ~/workspace/glyph-tok,
tokenizers 0.21.4, the tokenizer.json of qwen3-30b-a3b-instruct-2507-fp8). Made by
`bash tools/omegese/tokens_structs.sh <tokcount> <tokenizer.json> <out>`. OSC text = the .osc twin without its
`//` comment lines; Omegese A = tests/one_layer/omegese/structs/spelling_a (`p = new S`, then one `put` per field
cell); Omegese B = the twin itself (`p = new S v1 .. vn`).

| twin | OSC text | Omegese A | Omegese B |
|---|---|---|---|
| t1_scalar | 61 | 51 | 43 |
| t2_array_field | 81 | 85 | 54 |
| t3_two_structs | 92 | 80 | 68 |
| t4_call_view | 79 | 64 | 56 |
| t5_call_edit | 99 | 81 | 73 |
| t6_loop | 97 | 100 | 69 |
| **total** | **509** | **461** | **363** |

Chosen: B, 363 tokens against 461 for A (98 fewer) and 509 for OSC text. A costs most where a struct has an
array field (t2, t6): every element needs its own `put`. The fixed words `struct`, `new`, `get`, `put`, `end`, `free`
are one token each with their leading space (`tokcount ... words`).
