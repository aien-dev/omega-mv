# omega-mv#681 — cross-runtime evidence envelope, host result

Source: `spec/max-interop/ENVELOPE-v1.md`,
`tests/max-interop/{envelope.py,receipt_tool.c,test_envelope.py,build_receipt.sh}`.
Artifact source commit bound: `8eb8e7df0d1b04eeb8422d5ecd5355dddd4eb16a`.

## Exact commands and results
```
sh tests/max-interop/build_receipt.sh
python3 tests/max-interop/test_envelope.py \
  --artifact /home/drakestapleton/workspace/maxinterop/cudapp/evidence/MAX-INTEROP/68/matmul-16x16x8 \
  --fixture tests/max-interop/fixtures/matmul-v1.json
# {"checks": 26, "fail": 0, "envelope_id": "4cd9a49c…", "receipt_id": "daae94b7…"}
```
The committed envelope (`envelope.txt`) binds the real matmul artifact
`7c5ab77748c1cd29a50b9cb91efcce6d8200eb30646ce88142f31ba4d7d5544c`:
`envelope_id=4b17c82188c4bfcbdefb3f7c5ca66f6b06245866b4df6f7f12cdf787dc177b00`,
`receipt_id=f13b1bff8aac38cbee9c6b23c1557bd0c81f954555e84ebbb17b1f52d618ba76`.
(The test builds its own envelope with `source_commit=UNKNOWN`, hence the
different ids in its summary.) The receipt is 416 bytes and verifies with the
existing TEST anchor. Changed receipt bytes, a different receipt id, a stale
epoch, a one-byte signature change, a reused artifact, a wrong executable and an
op/contract mismatch are refused.

## What is NOT proven
- No device load or execution: `execution=NOT_RUN`, `oracle_verdict=NOT_RUN`,
  `output_sha256` all zero. The chip is reserved; no window was opened.
- The signature attests provenance (TEST key), not correctness.
- The oracle is bounded to the frozen fixtures and tolerance.
- This is a C2-format **envelope profile**, not a checker2 IR_CHECKED receipt:
  `program` carries the envelope id and the evidence class is the only one the
  format defines. `requirements` names the profile.
