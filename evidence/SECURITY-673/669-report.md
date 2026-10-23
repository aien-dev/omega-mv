# Omega #669 qualification: AArch64 conditional branch displacement

Date: 2026-10-23. Parent tracker: #673. Branch: `fix/security673-branch-range`.
Base commit: `f0499f415571bdd7496b6de391096915e78f7556`.
Status: **PASS for the bounded forward property; parent #673 remains open.** No formal proof and no claim of an unhackable system.

## Threat model and protected asset

The compiler emits executable machine code from untrusted `.om` source. The protected asset is the
correspondence between the source's structured control flow and the branch targets in the accepted
artifact. A silently truncated `B.cond`, `CBZ` or `CBNZ` displacement can send execution to a different
instruction than the source required. The baseline showed that this is reachable and produces a program
that Linux kills with SIGSEGV, so the immediate impact is loss of integrity and availability of the
compiled program, not a proof of privilege escalation.

Out of scope for this slice: physical attacks, malicious firmware, a compromised compiler binary, and
source that is already trusted to be correct. Those remain #673 assumptions.

## Authoritative source and artifact identities

| Item | Identity |
|---|---|
| Compiler source before fix | `seed/omc/omc.om` sha256 `74620988c967f18f9757807fc1d6f713c3120513c2a4a4aaba671e3019e8865e` |
| Compiler source after fix | `seed/omc/omc.om` sha256 `35cab5e41fc2c020903f1318c1ff8d83f8701255b41b5ca8ef5395f706c015a4` |
| Baseline compiler binary | sha256 `e5d60db892c8f21cc8c9128af26d8bded0e96d7f92946f5a549397c43a872649` (om4-built, self-stable) |
| Fixed compiler binary | sha256 `2c189f308c78e64e1b51ac86b139e8a662d5e622b764674f068b07e178fab0e9` |
| Fixed stage 1 = stage 2 = stage 3 | yes, all three 109,048 bytes |
| Independent decoder | GNU objdump (GNU Binutils for Ubuntu) 2.42, outside the seed compiler closure |
| Fixture generator | `seed/qualification/security673/branch-corpus.om` sha256 `1cb80b66340e0deb2fbb04c49389884217b828677e745abf16f763464b6405ba` |
| Platform | AArch64 Linux (arm64 workstation), native execution, not emulation |

The branch base revision was reconciled from the issue: the concern exists at
`57f5f425` (seed/omega-in-omega) and at `f0499f41` (feat/seed-native-host). The security branch was cut
from `f0499f41`; public main `cd5897e3` has no `seed/` tree.

## Inventory of relocation encoders

| Encoder | Field | Instruction | Source-controlled distance | Control |
|---|---|---|---|---|
| `disp19` / `patch19` | signed imm19 words | B.cond, CBZ, CBNZ | yes: `s_else`, `s_end` (if/else/while/for exit) | checked range and alignment, error 24 |
| `patch19` via `finish` | signed imm19 words | B.cond at each trap site | yes: size of the function body before the handler | same check |
| `blo` | signed imm19 words | B.LO in the local-array fill loop | no: fixed loop body, a few words | routed through `disp19` |
| `patch26` / `back` | signed imm26 words | B | output pool is 8 MiB = 2^21 bytes, below the 2^25-word reach | structural invariant documented at `patch26` |
| `patch19` callers in host wrappers | signed imm19 words | B.cond | fixed wrapper code, at most hundreds of bytes | same check |
| `bl` (calls) | signed imm26 words | BL | output pool bound as above | not changed |

## Reachability and reproduction

`patch19` computed `d = (target - at) >> 2` and masked it with `524287`, with no local range test. For a
forward displacement in `262144 .. 524287` words the mask is an identity and bit 18 is then interpreted
as the sign bit by the CPU. The displacement is silently sign-extended.

The baseline reproduction is `evidence/SECURITY-673/baseline669.log`:

- `if` body of 300,000 `x = add x 1` statements, 3,600,083 source bytes, 1,200,256-byte artifact;
- compile exit 0, no diagnostic;
- intended target `0x124f94`, decoded target `0xfffffffffff24f94`, intended displacement 300,001 words;
- the encoded branch jumps 224,283 words backward into the body; running it exits 139 (SIGSEGV).

A generator error in the abandoned session (`omc: line 14: error 16`) was a broken fixture, not a
compiler result; it was replaced before any conclusion was drawn.

## Enforcing code

`seed/omc/omc.om` now has one checked encoder:

```
fn disp19 at ulong target ulong to ulong
diff = sub target at
if ne and diff 3 0
fail 24
end
if and gt diff 1048572 lt diff 18446744073709550592
fail 24
end
ret and shr diff 2 524287
end
```

- `1048572` = `(2^18 - 1) * 4` bytes, the largest forward displacement.
- `18446744073709550592` = `2^64 - 1048576`, the smallest backward displacement.
- `and diff 3` rejects an unaligned target (structurally impossible in current lowering, checked anyway).
- `patch19` and `blo` both call `disp19`. Error 24 is a stable diagnostic and the compiler exits before
  `writeout`, so no accepted artifact is produced. Error codes 1..23 keep their existing meanings.

## Properties, oracles and results

Raw results: `evidence/SECURITY-673/gate669.log`. Driver:
`seed/qualification/security673/gate669.sh`; fixture generator: seeded `.om`.

| Property | Test | Independent oracle | Result |
|---|---|---|---|
| Small layout still correct | N=16, run exit 0 | objdump: branch target = `mov x1,#12345` marker at `0x54` | PASS |
| Exact forward maximum | N=262,142, displacement 262,143 words = 1,048,572 bytes | run exit 0 and objdump target = marker | PASS |
| One word beyond | N=262,143, displacement 262,144 words | compile exit 24, empty stdout, `omc: line 262148: error 24` | PASS |
| Signed wrap inside 19 raw bits | N=524,287 | compile exit 24, empty stdout | PASS |
| Nested if | N=300,000 | error 24 before artifact | PASS |
| While exit | N=300,000 | error 24 before artifact | PASS |
| Trap-site branch | N=300,000 | error 24 before artifact | PASS |
| Negative control | bound moved by one word to `1048580`, N=262,144 | mutant compiles; objdump target `0xfffffffffff00014` != marker `0x100014`; run exit 139 | PASS (decoder catches it) |
| Existing regressions | `sh seed/omc/gate5.sh` | 177 cases, 0 FAIL, GATE5 PASS | PASS |

The mutant matters because the fixed compiler refuses that input but the mutant's program exits through
a wrong path; the decoder, not the exit status, is the oracle.

## Reproduce

```
sh seed/omc/gate5.sh                          # exit 0, GATE5 PASS (177 cases)
sh seed/qualification/security673/gate669.sh  # exit 0, GATE669 PASS (9 cases)
```

Both scripts were run at source hash `35cab5e4...`; the gate log records the compiler and decoder
identities. The bootstrap is stage1 = om4(omc.om), stage2 = stage1(omc.om), stage3 = stage2(omc.om).

## Backward direction and remaining limits

The check accepts backward displacements down to -1,048,576 bytes. No source construct currently emits
a long backward conditional branch: `blo` is the only negative imm19 encoder and its target is the head
of a fixed-size array-fill loop. Its small-distance path is replayed by gate5's local-array cases, but the
backward extremum itself is inspection-only, not a bounded dynamic test. The 26-bit unconditional B
range is structurally unreachable because the output pool is 8 MiB; that invariant is stated in the
compiler source and would need review if the pool size changed.

No formal proof. The evidence is bounded synthetic testing plus an independent decoder on this platform.
