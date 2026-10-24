# omega-mv#681 — cross-runtime evidence envelope, host result

Source: `spec/max-interop/ENVELOPE-v1.md`, `tests/max-interop/{envelope.py,receipt_tool.c,test_envelope.py,build_receipt.sh}`.

## Exact commands and results
```
sh tests/max-interop/build_receipt.sh
python3 tests/max-interop/test_envelope.py \
  --artifact /home/drakestapleton/workspace/maxinterop/cudapp/evidence/MAX-INTEROP/68/matmul-16x16x8 \
  --fixture tests/max-interop/fixtures/matmul-v1.json
# 22 checks, 0 fail; envelope_id b1268c18adfb949d930e547c34c39f4689aff020894d0fd5d08be101fc70a6be
# receipt_id   8a6e9b8a46c761407b483e68790a1697f87d27196dfd5f1bbeb785c07a5d8a7a
```
The issued receipt is `receipt.bin` (416 bytes); `envelope.txt` binds the real
matmul artifact `6704f28d…`. The verifier accepted it with the existing TEST
anchor and refused a stale epoch (floor 2) and a one-byte signature change.

## What is NOT proven
- No device load or execution: `execution=NOT_RUN`, `oracle_verdict=NOT_RUN`,
  `output_sha256` all zero. The chip is reserved; no window was opened.
- The signature attests provenance (TEST key), not correctness.
- The oracle is bounded to the frozen fixtures and tolerance.
