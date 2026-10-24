# Rename packed-list ingress for Omega #671

Date: 2026-10-23. Parent tracker: #673. Branch: `fix/security673-qualified`.
Platform: Linux AArch64, native execution.

## Threat and protected property

`renames` accepts a packed list and must consume exactly two nonempty path records before calling
`renameat2`. A truncated record, extra record, empty path, embedded zero or trailing byte must trap
before a kernel rename. The protected assets are the caller's filesystem namespace and the
correspondence between the supplied records and the two paths actually renamed.

Enforcement is the existing `hpack` lowering in `seed/omc/omc.om` (line 2533), which is shared with
`execprog` and the other packed-list words. The test consumer is a normal compiler output.

## Cases and results

`renames-corpus.om` writes the packet; `renames-consumer.om` creates
`build/security673-native/ren-src`, then calls `renames pkt 0 n 0`. Exit 0 means the rename
succeeded; exit 103 is trap 3 before the rename.

| Mode | Packet | Expected | Observed | Source after |
| --- | --- | --- | --- | --- |
| 0 | exactly `ren-src`, `ren-dst` | 0 | 0 | renamed to `ren-dst` |
| 1 | one record | 103 | 103 | remains |
| 2 | three records | 103 | 103 | remains |
| 3 | empty first record | 103 | 103 | remains |
| 4 | empty second record | 103 | 103 | remains |
| 5 | valid pair plus one trailing byte | 103 | 103 | remains |
| 6 | embedded NUL in first path | 103 | 103 | remains |

Mode 0 is the positive control: the source disappears and the destination exists. Every malformed
mode leaves the source in place, so the trap occurred before the kernel rename.

## Executed gates

| Command | Result |
| --- | --- |
| `build/gate669` (normal) | exit 0, `GATE669 PASS` |
| `build/gate669 3` (source campaign) | exit 0, `GATE671 SOURCE PASS` |
| `sh seed/host/gate.sh` | exit 0, all host groups |

Raw output is `gate-renames.log`; `identities.txt` binds the compiler, corpus, consumer, gate and log.

## Residual limits

- `execprog` and `renames` are qualified; `argread`, native path words and the legacy
  manifest/receipt/RPC/model/trace/device parsers remain under `ingress-inventory.md`.
- The compiler, corpus and consumer share the seed lineage. No kernel fuzzing or general parser
  proof is claimed.
- This does not close #671 or #673.
