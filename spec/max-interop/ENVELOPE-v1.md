# Cross-runtime evidence envelope v1 (normative)

Status: implemented and host-tested (`tests/max-interop/envelope.py`,
`tests/max-interop/test_envelope.py`, 22 checks). No device execution is claimed;
`execution=NOT_RUN` is a first-class value. This envelope **reuses** the signed
`checker2` receipt format (`spec/one-layer-receipts.md`); it does not replace,
reinterpret or weaken it.

## 1. Proof tiers are separate

| Tier | What it establishes | What it does not establish |
|---|---|---|
| artifact hash | identity of exact bytes | that the bytes are safe or correct |
| C2 signature | a TEST/OWNER signer vouched for the bound hashes | correctness, authorization, capability |
| bounded oracle | a result within the frozen contract and tolerance | universal equivalence or another implementation's correctness |
| `execution=measured` | work actually ran on the named device | that timings are comparable outside the declared protocol |

`identity:hash,provenance:signature,result:not-run,execution:not-run` and the
`identity:hash,provenance:signature,result:bounded-check,execution:measured`
variant are the only accepted tier strings. A `NOT_RUN` envelope must not claim
a result it did not check.

## 2. Canonical layout

UTF-8/ASCII, LF, one `key=value` per line, fixed order. 26 lines:

```
omega-cross-runtime-envelope/1
envelope_id=<64 hex>
op=matmul|gqa-causal-attention
contract_id=<64 hex>
artifact_id=<64 hex>
artifact_cubin_sha256=<64 hex>
artifact_qmd0_sha256=<64 hex>
artifact_qmd1_sha256=<64 hex>
semantic_spec_id=<64 hex>
isa=sm_121
machine_profile=<64 hex>
runtime=omega-native|modular-max
runtime_version=<token 1..64>
runtime_source_commit=<40 hex|UNKNOWN>
device_backend=cuda|omega-native
device_identity=<64 hex; all zero = unknown>
input_sha256=<64 hex>
output_sha256=<64 hex>
oracle=independent-fp64-v1
oracle_verdict=PASS|FAIL|NOT_RUN
tolerance=<token 1..64>
execution=measured|NOT_RUN
timings=<token 1..64>
receipt_sha256=<64 hex>
receipt_id=<64 hex>
proof_tiers=<tier string>
```

`envelope_id = SHA-256` of the bytes from `op=` through `timings=` inclusive,
each line newline-terminated. Receipt fields are **excluded** from the id: the
receipt binds the id, so including the receipt in the id would be circular.
`timings` is included, so a timing edit changes the id and invalidates the
receipt's program binding.

`op` and `contract_id` are bound: `matmul` must carry
`647116fc…675704` and `gqa-causal-attention` must carry `003696e4…a115359`
(full ids in the frozen contracts). Any other pairing is `E_CONTRACT`.

`input_sha256` is the SHA-256 of the canonical JSON of the fixture's input
bit-strings (`{"a":[...],"b":[...]}` or `{"q":[...],"k":[...],"v":[...]}` with
`separators=(",", ":")`). `output_sha256` is the SHA-256 of the raw little-endian
f32 output; all zero means no output was bound. `oracle_verdict=PASS` with an
all-zero output and `execution=measured` with an all-zero output are `E_RESULT`;
`execution=NOT_RUN` with a nonzero output is also refused.

## 3. Receipt binding

`tests/max-interop/receipt_tool.c` fills one existing 416-byte `C2Receipt`:

| field | value |
|---|---|
| requirements | SHA-256(`"omega-cross-runtime-envelope/1"` ‖ `envelope_id`) |
| program | `envelope_id` bytes |
| executable | `artifact_cubin_sha256` bytes |
| dependencies | `c2r_deps_digest` over the single `artifact_id` |
| checker | SHA-256(`"omega-envelope-checker-v1"`): profile tag, not a checker2 build |
| producer_checker | SHA-256(`"cudapp-artifact-export/1"`): profile tag |
| target | `c2r_target_id("gpu-sm121-max-interop")` |
| firmware | all zero: no firmware image was read |
| input | `input_sha256` bytes |
| class / alg / evidence | TEST / Ed25519 / IR_CHECKED |

**Envelope profile caveat.** This reuses the 416-byte C2 container and its TEST
signature path, but it does **not** claim that checker2 decoded an OSC program:
`program` carries the envelope id by this profile, and `IR_CHECKED` is the only
evidence class the existing format defines. The `requirements` domain tag
(`omega-cross-runtime-envelope/1`) is what distinguishes the profile. A consumer
that requires a genuine checker2 receipt must not accept this one; production
signers are out of scope here.
| epochs | 1 .. 1000000 |

The verifier uses the TEST anchor from the existing code, a checker allow-list,
an epoch floor, and expects target/program/executable/input/dependencies. The
verifier first hashes the receipt file and compares it with `receipt_sha256`,
then compares the tool's `receipt_id` with `receipt_id`; a changed receipt file
or a different receipt is refused before the C2 check. A changed cubin,
envelope, input or artifact id fails; a stale epoch, tampered signature or wrong
target fails with the existing named `C2R_E_*` reason.

The signature is provenance only. It grants no capability and never upgrades a
`NOT_RUN` execution to a measured one.

## 4. Refusals

Structural validation refuses `E_ORDER`, `E_HEADER`, `E_ID`, `E_VALUE`, `E_OP`,
`E_ISA`, `E_RUNTIME`, `E_BACKEND`, `E_ORACLE`, `E_VERDICT`, `E_EXECUTION`,
`E_TIERS`, `E_CONTRACT`, `E_RESULT`. The receipt layer refuses with its existing `C2R_E_*` codes. A
`measured` envelope with a non-`PASS` oracle verdict is refused.

## 5. Present status

`evidence/MAX-INTEROP/681/` binds the real matmul artifact
(`artifact_id=7c5ab777…`) with `execution=NOT_RUN` and a TEST receipt. Device
load/execution remain `NOT_RUN` because the chip is reserved; the envelope must
be re-issued after a measured run rather than edited.
