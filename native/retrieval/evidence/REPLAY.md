# Replay record

All commands below invoke existing build/inspection tools. There is no new shell,
Rust, C, JavaScript or Python implementation/test harness in this candidate.
Use the consumer compiler whose SHA256 is pinned in the module README.

Build the native verification suite by concatenating these files, in order,
and feeding them to that compiler on stdin:

1. `vendor/sha256.om`
2. `vendor/json.om`
3. `src/pins.om`
4. `src/codec.om`
5. `src/verify.om`
6. `src/request.om`
7. `src/packet.om`
8. `src/pack.om`
9. `tests/verify.om`

The compiler writes the executable to stdout. Check compiler exit0 before
making the output executable. Running it produces **41 checks,0 failures**,
exit0. Replace the final input with `tests/request.om`, `tests/pack.om` or
`tests/replay.om` to build those respective tests. The request test emits
`request.json` and exits0. The pack test exits0 after checking all four packet
shapes, exact budgets, one-token-short refusals, whole-record drops, a changed
tokenizer pin and a changed serialized packet.

The replay consumes `search.response`, emits `replay.packet`, exits0, and is
byte-identical to `packet-3.json`. `nomatch.response` produces the explicit
`replay.nomatch` outcome and exit0. `scope.response` exits10 with no evidence
bytes (`replay.refusal` is empty).

For `tests/export.om`, concatenate only `vendor/sha256.om`, `src/pins.om`, and
that test. Feed `corpus-t1.response` followed by `corpus-t2.response`; exit0
verifies the exact raw compiler records against the embedded verifier pins.

For packet fixtures, concatenate `vendor/sha256.om`, `src/pins.om`,
`src/codec.om`, `src/packet.om`, `tests/packets.om`. Give one ASCII digit1..4,
without a newline. Each output is the corresponding `packet-N.json`.
For administrative NDJSON, use `vendor/sha256.om`, `src/pins.om`,
`src/codec.om`, `src/ingest.om`; its output was loaded through the external
server's administrative `_bulk` endpoint. Neither utility contains credentials.

The three `mutant-*.diff` files specify one deliberately removed guard in
`src/verify.om`. Compile each changed module against the unchanged final41-check
test. All three compile with exit0, then exit1: identity fails4 assertions,
authority fails3, index scope fails1. These mutations are controls, not fixes.

The exact tokenizer reference invocation, with each packet at most2000bytes:

```
qwen38_prefill prompts /home/drakestapleton/models/qwen3.6-35b-a3b-fp8-95a723d0 native/retrieval/evidence/packet-3.json 2147483647 /tmp/om-xerj-L779/unused-token-output
```

Expected stderr reports502tokens and exit2. No output directory is created.
The source/binary/library/tokenizer pins and early-error path are documented in
the module README. `tokenizer.trace` is a second observation of that same path;
it opens only loader metadata/libraries, the tokenizer and packet. Runtime
source paths in build logs are receipts of the unchanged reference build.

Each `.costs` file records `/usr/bin/time` wall/CPU/peak-memory/exit observations.
The printed elapsed-time resolution is0.01seconds; `0.00` is below that
resolution, not zero cost. Refusals and failed builds remain in the accounting.
There is no model-generation, GPU-energy or complete workflow-cost result.

External authorization observations, taken from curl's actual HTTP status:

| Operation | Expected and observed HTTP status | Raw body |
| --- | ---: | --- |
| Admin bulk import | 200, both items201 | bulk.response |
| Scoped corpus read | 200 | search.response |
| Read outside scope, before index exists | 403 | scope.response |
| Scoped write attempt | 403 | write.response |
| Read without credential | 401 | noauth.response |
| Admin write to other index | 201 | admin-write.response |
| Admin read of other index | 200 | admin-read.response |
| Scoped read of existing other index | 403 | existing-scope.response |
| Valid no-match query | 200 | nomatch.response |

These controls used the pinned external executable with authentication enabled,
not the shared unauthenticated search service. Original HTTP timing/startup and
administrative setup costs were not captured completely; this prevents a full
cost qualification. No private source or secret credential is in these artifacts.

The final native `request.json` was also submitted through a fresh exact-index
read credential to the same pinned external build. It returned HTTP200,
1266response bytes and two hits; native verification and packing exited0.
`native-request.response`, `native-request.packet`, and `native-request.costs`
retain this single observation (curl wall0.018379seconds). This is a protocol
integration observation, not a latency benchmark or native HTTP implementation.
