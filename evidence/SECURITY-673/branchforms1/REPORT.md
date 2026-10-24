# Branch-form semantic qualification for Omega #669

Date: 2026-10-23. Parent tracker: #673. Branch: `fix/security673-qualified`.
Base: `540e8bcd` plus the changes in this directory. Platform: Linux AArch64, native execution only.

## Threat and protected property

Source can place a conditional or loop edge at or beyond the AArch64 imm19 range, or at a function
boundary where an encoder could patch the wrong place. The protected asset is correspondence between
the source control flow and the emitted branch target. The failure mode under test is silent
truncation or a branch to a different instruction, not host compromise.

Enforcement remains the single checked path in `seed/omc/omc.om`: `disp19` (line 935) derives and
checks the signed word displacement, and `patch19` (line 947) is the only imm19 writer. `patch26`
(line 952) writes unconditional B and relies on the inspected 8 MiB output-pool capacity, which is
below the signed 26-bit reach.

## Encoder inventory

| Source form | Enforcing emitter | Branch instruction | Patching path |
| --- | --- | --- | --- |
| `if`/`while` Boolean condition | `cond` | CBZ Xr for a materialized value; B.cond for condition flags | `patch19` in `s_end` |
| `if`/`while` condition with relational operands | `binop`/`conds` | B.cond with the inverted condition | `patch19` in `s_end` |
| `else` | `s_else` | B (unconditional) over the else part | `patch26` |
| `while`/`for` back edge | `s_end`, `back` | B to the loop head | direct imm26 encoding in `back`; inspected output-capacity bound |
| Local array fill | `sa_arr` | B.LO back to the fill loop | `patch19` via `blo` |
| Trap sites, including shift/width checks | `trapsite` (line 1813) | CBNZ/B.HS to the trap stub | `patch19` in `finish` |
| Boolean branch materialization | `bbr` paths | B.cond | `patch19` |
| Host-word packet/error branches | `hpack`, `hpath`, `hargs`, `hwaitio` | B.cond/CBZ to a return/error path | `patch19` |

All imm19 users flow through `disp19`; there is no second relocation encoder. The compiler does not
expand a branch or relax layout. A displacement outside `-1048576..+1048572` bytes, or an unaligned
target, is refused with error 24 before an artifact is accepted. The declared refusal bound is
therefore 2^18-1 words forward and 2^18 words back, matching the ISA field.

## Added semantic cases

`branch-forms.om` generates fifteen fixed programs. Each is compiled by the rebuilt compiler and
executed natively; the coordinator compares the actual exit:

| Mode | Form | Expected exit |
| --- | --- | --- |
| 0 | `if eq` register, taken | 11 |
| 1 | `if ne` register, not taken | 12 |
| 2 | `if lt` taken | 11 |
| 3 | `if le` taken | 11 |
| 4 | `if gt` taken | 11 |
| 5 | `if ge` taken | 11 |
| 6 | `if eq` against zero (CMP/B.NE in independent disassembly) | 11 |
| 7 | `if ne` against zero, false | 12 |
| 8 | `if eq` false | 12 |
| 9 | `while` back edge | 3 |
| 10 | `for` back edge | 3 |
| 11 | `if`/`else` unconditional B | 21 |
| 12 | branch inside a called function | 30 |
| 13 | shift-width trap site | 107 |
| 14 | divide-by-zero trap site (CBZ in independent disassembly) | 102 |

These complement, rather than replace, the existing range cases: `boundaries` checks the encoder
extrema directly, `positive`/`negative` check source-level range refusal, `array-fill` exercises the
real backward B.LO caller, and `mutant` proves a weakened guard is caught by independent decoding.

## Executed results

| Command | Result |
| --- | --- |
| `build/gate669-new` (normal) | exit 0, `GATE669 PASS` |
| `build/gate669-new 3` (source campaign) | exit 0, `GATE671 SOURCE PASS` |
| `sh seed/host/gate.sh` | exit 0, all nine host groups pass |

Raw stdout is `gate-branch.log`, `gate-hostile.log` and `host-gate.log`; stderr for the gate runs is
empty. `identities.txt` binds the compiler source, gate, corpus, rebuilt compiler and logs by SHA256.

## Residual limits

- Long backward source reachability is still not established. The backward B.LO caller is a short
  array-fill loop; the direct encoder extrema are synthetic. This is a bounded test obligation, not
  a reachability proof.
- The gate, corpus and compiler share the seed lineage. GNU objdump supplies independent decoding for
  the range probe; no diverse-double-compilation claim is made.
- This does not close #669, #671 or #673. Release integration, the remaining #670 authority contract,
  and independent final-source review remain with their existing owners.
