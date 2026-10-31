# Local trace segment qualification

Date: 2026-10-23. Branch: `fix/security673-qualified`. Platform: Linux AArch64, native execution.

## Threat and property

An interrupted append can leave a short tail; a damaged header can make a segment unreadable; a
writer can grow an unbounded file or silently overwrite earlier records. The protected assets are
the existing records and the ability to distinguish a complete record from a partial one.

`seed/trace/segment.om` writes a 16-byte header and 64-byte records. Append is
`O_WRONLY|O_CREAT|O_APPEND` plus `fsync`. `segcount` accepts only complete 64-byte records and flags
a short tail. `segrecover` renames the damaged segment to `segment.bad` and creates a fresh header;
it never treats a partial record as complete.

## Results

| Case | Expected | Observed |
| --- | --- | --- |
| create header | 0 | 0 |
| append 100 records | 0 per append | 0 |
| count complete segment | 100, no tail | 100, 0 |
| append 10-byte partial tail | count 100, tail 1 | 100, 1 |
| recover | quarantine, fresh segment | 1, count 0, tail 0 |
| `segment.bad` exists | open succeeds | succeeds |
| append after recovery | 1 complete record | 1 |
| corrupt header | invalid magic, error 11 | 11 |
| recreate | 0 | 0 |

`trace checks=117 000`, exit 0. The same program runs in `gate669` at exit 0.

## Residual limits

- No consent, exact-byte export, sanitization, retention, rotation or network path. Those remain
  #607 and require a `.om` cryptographic digest plus the Open Humanity receiver contract.
- No concurrent-writer lock; a single writer is assumed.
- `fsync` is per append; throughput is not qualified.
- The test and writer share the seed lineage. This is a local component, not a whole-trace-system
  claim.
