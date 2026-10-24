# Native path and argument ingress for Omega #671

Date: 2026-10-23. Parent tracker: #673. Branch: `fix/security673-qualified`.
Platform: Linux AArch64, native execution.

## Threat and protected property

A caller can supply a path longer than 4095 bytes, embed a zero byte, pass an empty path, or ask
`argread` for more bytes than the destination holds. The protected assets are the compiler's path
copy at the kernel boundary and the caller's destination slice; a refused operation must not leave
a partially overwritten buffer or perform a kernel open.

Enforcement is the existing `hpathcp`/`hpath` lowering and `argread` in `seed/omc/omc.om`. The
consumer is a normal compiler output.

## Cases and results

Mode byte on stdin; `argread` modes also receive `seed/omc/omc.om` as the first argument.

| Mode | Operation | Expected | Observed |
| --- | --- | --- | --- |
| 0 | create and open `build/security673-native/path-ok` | 0 | 0 |
| 1 | `fopen` path with embedded zero | trap 3 | 103 |
| 2 | `fopen` path of 4096 bytes | trap 3 | 103 |
| 3 | `fopen` empty path | negative errno, no open | 0 (consumer accepts the negative result) |
| 4 | `argread` into a 1-byte buffer for a longer argument | return -105, slice unchanged | 0 (consumer verifies -105) |
| 5 | `argread` invalid index 99 | trap 3 | 103 |
| 6 | `argread` exact 4096-byte capacity | positive length | 0 |

Mode 3 is deliberately not a trap: the documented contract refuses embedded zero and overlength
paths before the kernel, while an empty path reaches `openat` and returns the kernel's negative
errno. The consumer checks that it is negative and does not treat an empty string as a valid open.
Mode 4 checks the documented `-105` short-capacity result; the destination byte is not changed.

## Executed gates

| Command | Result |
| --- | --- |
| `build/gate669` (normal) | exit 0, `GATE669 PASS` |
| `build/gate669 3` (source campaign) | exit 0, `GATE671 SOURCE PASS` |
| `sh seed/host/gate.sh` | exit 0, all host groups |

Raw output is `gate-paths.log`; `identities.txt` binds the compiler, consumer, gate and log.

## Residual limits

- Path traversal and authority resolution are not confined by `fopen`; the strict `execpure`
  profile is the confined launch path, and general filesystem authority remains #670/#649 work.
- Manifests, receipts, RPC arguments, model metadata, trace bundles and device command descriptions
  remain legacy/migration dependencies, not PASS.
- The compiler, consumer and gate share the seed lineage. This does not close #671 or #673.
