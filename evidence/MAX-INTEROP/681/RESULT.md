# omega-mv#681 — cross-runtime evidence envelope, host result

Source: `spec/max-interop/ENVELOPE-v1.md`, `tests/max-interop/{envelope.py,receipt_tool.c,test_envelope.py,build_receipt.sh}`.

## Exact commands and results
```
sh tests/max-interop/build_receipt.sh
python3 tests/max-interop/test_envelope.py \
  --artifact /home/drakestapleton/workspace/maxinterop/cudapp/evidence/MAX-INTEROP/68/matmul-16x16x8 \
  --fixture tests/max-interop/fixtures/matmul-v1.json
# 22 checks, 0 fail; envelope_id 86aabdb2e296636cd485b33a3f7141955431e3165bf857d86077315494622054
# receipt_id   5c05295af4e9eb482827db52d7f6b6a6b0c2a6b7392d15c8ac9e18b5296a9c27
```
The issued receipt is `receipt.bin` (416 bytes); `envelope.txt` binds the real
matmul artifact `8dcc18e0…`. The verifier accepted it with the existing TEST
anchor and refused a stale epoch (floor 2) and a one-byte signature change.

## What is NOT proven
- No device load or execution: `execution=NOT_RUN`, `oracle_verdict=NOT_RUN`,
  `output_sha256` all zero. The chip is reserved; no window was opened.
- The signature attests provenance (TEST key), not correctness.
- The oracle is bounded to the frozen fixtures and tolerance.
