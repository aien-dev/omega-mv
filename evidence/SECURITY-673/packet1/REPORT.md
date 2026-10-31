# Packed-list ingress qualification for Omega #671

Date: 2026-10-23. Parent tracker: #673. Branch: `fix/security673-qualified`.
Platform: Linux AArch64, native execution. No device, GPU or network ingress was exercised.

## Threat and protected property

A caller-supplied packed list can be truncated, over-counted, padded with trailing bytes, contain an
embedded zero in a path, or carry an offset/length that exceeds the actual array. The protected
assets are the compiler's host-word acceptance decision and the kernel argument vector: a malformed
packet must trap before `execve`, and a valid one must be consumed exactly.

Enforcement is the existing `hpack` lowering in `seed/omc/omc.om` (line 2533) for `execprog`,
`renames` and the other packed-list words. At runtime it requires a `u16` count in 1..64, a total
packet of at most 4096 bytes, a `u16` length plus that many bytes per record, a nonempty first
record for `execprog`, no embedded zero, and no trailing bytes. Failures trap 3 before descriptor
closure and before `execve`. The parser is emitted into the compiled program; the test consumer is
a normal compiler output, not a copied implementation.

## Cases and results

`packet-corpus.om` writes raw packets; `pkt-consumer.om` reads up to 4096 bytes and calls
`execprog pkt 0 n`. The coordinator compiles both with the rebuilt compiler, generates each packet,
and compares the consumer exit. Exit 0 means `/bin/true` was executed; exit 103 is trap 3.

| Mode | Packet | Expected exit | Observed |
| --- | --- | --- | --- |
| 0 | one record `/bin/true` | 0 | 0 |
| 1 | count 0 | 103 | 103 |
| 2 | count 65 | 103 | 103 |
| 3 | count 1, length 5, three bytes | 103 | 103 |
| 4 | valid record plus one trailing byte | 103 | 103 |
| 5 | first record empty | 103 | 103 |
| 6 | embedded NUL in `/bin/tr\0ue` | 103 | 103 |
| 7 | record length 4093 with 4092 bytes present | 103 | 103 |
| 8 | 64 records, first `/bin/true` | 0 | 0 |

Mode 7 is exactly 4096 bytes on stdin; the declared record length exceeds the bytes actually
present, so the parser refuses rather than reading past the packet. The accepted modes execute the
real executable, so the test also proves the parser produced a usable argument vector for the
kernel, not just a return value.

## Executed gates

| Command | Result |
| --- | --- |
| `build/gate669-pkt` (normal) | exit 0, `GATE669 PASS` |
| `build/gate669-pkt 3` (source campaign) | exit 0, `GATE671 SOURCE PASS` |
| `sh seed/host/gate.sh` | exit 0, all host groups pass |

Raw output is `gate-packet.log`, `gate-hostile.log` and `host-gate.log`; stderr for the gate runs is
empty. `identities.txt` binds the compiler, corpus, consumer, gate and logs by SHA256.

## Residual limits

- `execprog` is qualified; `renames` and other `hpack` words share the parser but were not separately
  exercised with their own record-count and path rules.
- `argread`, native path words, environment strings, manifests, receipts, RPC arguments, model
  metadata, trace bundles and device command descriptions remain under the inventory in
  `../ingress-inventory.md`. Only the compiler source and packed-list paths are currently active
  seeded `.om` ingress.
- This is a deterministic case set, not grammar-aware fuzzing, a complete parser proof, or a kernel
  resource limit. The compiler, corpus and consumer share the seed lineage.
- This does not close #671 or #673.
