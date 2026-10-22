# Existing checker2 boundary: #623 / architecture #248 prerequisite

This is an executed baseline experiment, **not implementation of source translation validation**, and not a new checker or receipt format.

The existing unmodified checker2 gate was built and run at Omega source `c1da86d68fb1d4af20c879a82f373a4556f7e957`:

```
make -j2 OUT_DIR=build/L579-proof test-checker2-lrat
```

Observed exit 0: **20 IR NO-TRAP proofs verified, nine expected-trap programs, 56 refused negative controls, zero failures**. The existing differential and blaster legs also ran; full raw output is retained alongside this document. These results concern their precise bounded IR properties and retain the checker's unsupported-call/division/memory refusals. They do not prove a relation to generated native code.

## Discriminating experiment

`tests/universal/checker_boundary.py` uses the existing `oscc` and checker2 executables. It emits the actual AArch64 image and canonical IR for the repository's `p01_abs8.osc` fixture, requires the emitted IR to equal the IR whose LRAT proof was verified, and changes the first native instruction to RET. The mutated native image is **not executed**.

1. A TEST-class receipt issued for the original image plus the valid IR proof verifies VALID when the supplied bytes match.
2. A separately issued TEST-class receipt for the changed image, same IR and same proof, also verifies VALID when its supplied bytes match.
3. Presenting the original receipt against the changed image refuses `EXECUTABLE_MISMATCH`.
4. Presenting the public TEST-key receipt to the production-default verifier refuses `TEST_SIGNER_IN_PRODUCTION`.

This is exactly the existing documented trust boundary: receipt evidence class 2 proves a bounded property of IR and binds native bytes by hash. It does **not** establish that those native bytes realize that IR. This result is not an authentication bypass or a claim that the receipt implementation violated its contract. The issuing signer is a deliberately public development TEST key; production authority is not exercised.

**Verdict:** existing IR proof and byte binding are demonstrated; **native translation relation remains UNKNOWN**. #623 and architecture #248 must supply an independently checked relation rather than upgrade this receipt to a native equivalence proof. The executable mismatch guard is useful but cannot substitute for translation validation.

## Reproduce

Use the compiler/checker sources from `c1da86d6` (retained in this branch ancestry), AArch64/Linux build environment, C compiler and existing shell/timeout prerequisites. First run the exact LRAT make target above. Then choose an output path that does not yet exist:

```
python3 tests/universal/checker_boundary.py \
  --tool-dir "$PWD/build/L579-proof" \
  --out "$PWD/build/L579-boundary-new-run"
```

The script refuses an existing campaign output directory rather than overwrite historical receipts. It records exact commands, exits, stdout/stderr, input/tool SHA-256 identities and generated artifact identities. Its `experiment.json` is unsigned development reporting, **not** a new production receipt. Signed `.rcpt` artifacts are issued solely by the unchanged checker2 TEST path. No source importer, upper IR, production proof, x86/RV64, GPU execution or physical qualification is claimed.

Prerequisite issue #621 remains under its existing worker L574; this experiment does not modify its tree or interfaces. #625 has its own immutable candidate and desk intake. Renda publication of generic local READY records is currently blocked by the installed adapter gap, separately tracked by L575 and routed to the existing L522 coordinator; no duplicate PR writer was started.
