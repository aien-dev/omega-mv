# Gate TWIN and Gate C2 (L094)

Six programs, each written in OSC and in Omegese (tests/one_layer/omegese/structs). A = `oscc t.osc` ir_sha256
(oscc without OUT: the SHA-256 of the encoded IR, osc_ir_digest; OUT's unit container refuses struct-reference
parameters, twins 4 and 5). B = sha256 of `omega_emit emit (omegese emit t.omegese)`. checker2 on B's bytes.
Made by `bash tools/omegese/twin_structs.sh build/omegese/omegese build/omega_emit/omega_emit build/compiler/oscc build/checker2/checker2 <out>`
at branch feat/omegese-structs 50a45823 (built through `make test-omegese`).

| twin | covers | sha256 of oscc IR (OSC) | sha256 of omega_emit IR (Omegese) | identical | checker2 |
|---|---|---|---|---|---|
| t1_scalar | one scalar struct | `a6d7d740220d64ea518513268b40b8d0f8de23dbff87bf4f42e6bf511467119f` | `a6d7d740220d64ea518513268b40b8d0f8de23dbff87bf4f42e6bf511467119f` | yes | ACCEPT |
| t2_array_field | an array field | `5bd5b054aacecb1c492c7d9332f43e941c8eba7e022e2ad293470d0c439cb5fb` | `5bd5b054aacecb1c492c7d9332f43e941c8eba7e022e2ad293470d0c439cb5fb` | yes | ACCEPT |
| t3_two_structs | two struct types | `ce8d6845fa80745834b1f9fd6cdaa16794e9daff6ca4ed67161b9220efcc701e` | `ce8d6845fa80745834b1f9fd6cdaa16794e9daff6ca4ed67161b9220efcc701e` | yes | ACCEPT |
| t4_call_view | a struct passed to a call as a view | `c6a26e54dbcf4f80424829a07ff1035b040d6f8c14eeb7774ac57a9b171a2157` | `c6a26e54dbcf4f80424829a07ff1035b040d6f8c14eeb7774ac57a9b171a2157` | yes | ACCEPT |
| t5_call_edit | a struct passed to a call as edit, written inside | `3ecd32d82a689a86945a19a4144c3b7f26594184d4b0d14ad7b5a0d014b37a40` | `3ecd32d82a689a86945a19a4144c3b7f26594184d4b0d14ad7b5a0d014b37a40` | yes | ACCEPT |
| t6_loop | a struct used inside a loop | `8738472023557759ef8e2a03ebcd2c353c4245764755244b7912d8bff2a0e562` | `8738472023557759ef8e2a03ebcd2c353c4245764755244b7912d8bff2a0e562` | yes | ACCEPT |

Result: struct twins: 6/6 give byte-identical IR through oscc and through omegese -> omega_emit, checker2 ACCEPT
