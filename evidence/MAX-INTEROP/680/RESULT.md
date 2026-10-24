# MAX interop contracts (omega-mv#680)

Branch: `feat/max-interop-contracts` (base `aien-dev/main`).
Scope: `omega-mv#680` only. No existing Omega C sources and no `checker2` were
modified. New files are confined to `spec/max-interop/`, `tools/max-interop/`,
`tests/max-interop/`, `evidence/MAX-INTEROP/680/`, plus the ignored build output
`build/contract-check`.

## Seed compiler

```
path    /home/drakestapleton/workspace/omega-bridge/build/bootstrap/omc
sha256  191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9
```

`tools/max-interop/build.sh` enforces that fingerprint before compiling, then
refuses any output that `file` does not identify as `ELF 64-bit ... ARM aarch64`.

## Contract identities

The identity is the SHA-256 of the exact `.contract` file bytes. The `.json`
files are machine-readable mirrors and are not identities.

| contract | sha256 |
|---|---|
| `spec/max-interop/matmul-bf16-v1.contract` | `647116fc1ba1e1394982fc8ba181ce565293f160a6b56afc0f02b2128a675704` |
| `spec/max-interop/gqa-causal-v1.contract` | `003696e422bba7274877ba9e01a61076ea0f862f32760cbaa06c60f85a115359` |

## Exact commands and results

```
$ sh tools/max-interop/build.sh
contract-check: seed=191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9
contract-check: output=bb9ef9a505d5ff9cb4298cf7bf2fffa8e199d4dce8c5e94baeb2974744918d6d
# exit 0; log: build.log

$ python3 tests/max-interop/test_contracts.py
...
SUMMARY {"fail": 0, "pass": 26, "total": 26}
# exit 0; log: tests.log

$ sha256sum spec/max-interop/*.contract
003696e422bba7274877ba9e01a61076ea0f862f32760cbaa06c60f85a115359  spec/max-interop/gqa-causal-v1.contract
647116fc1ba1e1394982fc8ba181ce565293f160a6b56afc0f02b2128a675704  spec/max-interop/matmul-bf16-v1.contract

$ file build/contract-check
build/contract-check: ELF 64-bit LSB executable, ARM aarch64, version 1 (SYSV), statically linked, no section header
```

`build.log`, `tests.log`, `env.log`, `sha256sums.txt`, and `manifest.txt` are the
raw evidence. The final test run had 26 passing tests and no failures.

## Checker protocol (`tools/max-interop/contract_check.om`)

stdin is a first line `mode=id|check|manifest|sha256` followed by the payload
(the bytes after that first `\n`).

* `mode=id` prints the 64 lowercase hex digits of the payload SHA-256.
* `mode=sha256` prints the payload SHA-256.
* `mode=check` validates the canonical line order and values of both frozen
  contracts and prints `OK <sha256>` (exit 0) or `REFUSED <code>` (exit 1).
  Codes: `E_HEADER`, `E_ORDER`, `E_VALUE`, `E_MISSING`, `E_UNKNOWN`,
  `E_TOLERANCE`, `E_DTYPE`, `E_LAYOUT`, `E_TARGET`.
* `mode=manifest` payload:

  ```
  expect=<hex64>
  omega-input-manifest/1
  op=<matmul|gqa-causal-attention>
  contract=<hex64>
  fixture=<name>
  input_sha256=<hex64>
  seed=<u64>
  count=<u32>
  [payload_hex=<even-length lowercase hex>]
  ```

  The contract must be the frozen id for `op`, equal `expect`, and `payload_hex`
  (when supplied) must hash to `input_sha256`. Prints `OK <contract-id>` or a
  named refusal.
* Exit 65 is the 1,048,576-byte input-capacity refusal; exit 74 is an I/O error.
  Capacity and I/O refusals write nothing to stdout.

The checker uses no heap and no local `new` arrays; it hashes with the adapted
native SHA-256 from `benchmarks-om/build/base/om-sha256.om` (AGPL-3.0-or-later,
attributed in the source).

## Oracle and tests

`tests/max-interop/oracle.py` is pure stdlib Python: exact bf16 conversion,
fp64 matmul with exact bf16 products, fp64 causal GQA with max-subtracted
softmax, fp32 rounding, splitmix64, and deterministic fixture generation. It
never calls CUDApp or Omega code.

`tests/max-interop/test_contracts.py` covers:

* contract ids against `hashlib` and the `sha256sum` binary;
* both contracts accepted and a mutation corpus for each rejected by both the
  native checker and an independent Python validator (reordered key, tolerance,
  dtype, layout, stride, accumulation/value, unknown key, missing key, bad
  header, trailing junk, missing final newline);
* hand-computed matmul 2x2 and 1x1 answers, bf16 round-to-nearest-even and
  non-finite rejection;
* attention single-token, causal mask boundary, zero length, and head map;
* deliberate wrong implementations: non-causal mask, wrong head map, wrong
  stride, and fp32 accumulation (a cancellation case that loses 8190.0 versus
  the fp64 reference);
* stale/foreign manifests, bad checksums, bad hex, bad decimal fields, unknown
  fields, and ordering;
* deterministic regeneration and checksum verification of the fixtures.

Fixtures are under `tests/max-interop/fixtures/{matmul-v1,gqa-causal-v1}.json`.
The ticket's literal `data/fixtures/` path would have written outside the
allowed new-file roots, so the committed fixture directory was placed under
`tests/max-interop/`; `test_contracts.py` regenerates it byte-for-byte.

Attention tolerance: the causal-rows fixture/reference uses the
`tolerance_large_scores` bound (`rel<=1e-3, abs<=1e-4`) as the primary
comparison, with the tighter normal bound (`rel<=2e-4, abs<=2e-5`) exposed by
the oracle for the normal-case check.

## NOT_RUN

* No MAX runtime, CUDApp kernel, GPU, or device execution: this ticket is the
  contract, checker, oracle, and fixture layer only.
* No non-AArch64 host run. The host is `aarch64`; `build.sh` refuses non-AArch64
  output by inspecting the produced ELF.
* No external third-party audit of the SHA-256 implementation; it is checked
  against Python `hashlib` and multi-block vectors in the test suite.
* No changes to existing Omega C sources, `checker2`, or other repositories.
