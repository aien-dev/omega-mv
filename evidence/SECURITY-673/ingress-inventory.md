# Active and planned .om ingress inventory for Omega #671

Date: 2026-10-23. Source of truth: the seeded compiler on `fix/security673-qualified`, plus the
legacy owners named in each row. This is an inventory of what exists, not a claim that every row is
qualified.

## Active seeded `.om` ingress

| Path | Implementation owner | Bound and refusal | Current qualification |
| --- | --- | --- | --- |
| Compiler source text (`seed/omc/omc.om` stdin) | `tok_num`, `tok_text`, `rdval`, `pushb`, `s_fn`, `main` | 20-digit numbers; 1024 value frames; 512 blocks; 512 functions; 4095-byte paths; 8 MiB input; explicit errors | `hostile1` (18 cases) and `branchforms1` |
| Packed host-word lists, including `execprog` | `hpack` in `seed/omc/omc.om` | count 1..64; total at most 4096 bytes; exact record lengths; no trailing bytes; no embedded zero; nonempty executable | `packet1` (9 cases, `execprog`) |
| Native argument vector read by a program | `argread` | 1 MiB scan bound; short-buffer refusal | NOT_RUN at the adversarial boundary; ordinary host tests pass |
| Native path words (`fopen`, `readlnk`, `cwd`, `fchmod`) | `hpath`/`hpathcp` in `seed/omc/omc.om` | path at most 4095 bytes; embedded zero traps 3; array bounds and byte types checked | NOT_RUN at the adversarial boundary; host tests pass |
| Rename packet (`renames`) | `hpack` | exactly two nonempty path records; same packet limits | NOT_RUN separately; shares the `execprog` parser |

## Legacy or planned paths without a seeded `.om` implementation

These are named by #671 but are not implemented in the seeded `.om` closure today. They must not be
counted as PASS. The replacement owner is the existing component owner under the `.om` migration
programme; this workstream does not create a second parser, authority or store.

| Path | Current owner | Status |
| --- | --- | --- |
| Manifests and lock files | repository build/release owners | legacy / migration dependency |
| Signed receipts and evidence bundles | receipt/checker owners (`c2_receipt`, existing runners) | legacy; not canonicalized under a single `.om` parser |
| RPC and tool arguments | interplane/runtime owners | legacy C/Rust; no `.om` ingress |
| Model, tokenizer and artifact metadata | model loader owners | legacy; no `.om` parser |
| Trace bundles | `rx_argus` / trace owners | legacy; `#607` privacy work remains open |
| Device command descriptions | device/runtime owners (`#672`) | not implemented; hardware profiles remain NOT_RUN |
| Network protocol framing | OSH/transport owners | legacy; no `.om` parser |

## Canonicalization and authentication notes

Only the packed-list format has an exact byte-consumption rule in the active `.om` path: the count,
each record length and the end of the packet must agree, and trailing bytes are refused. Source text
is line-oriented; numeric and name tokens are bounded, but the compiler does not authenticate its
input and makes no canonical-form claim. Receipt and manifest authentication remains with the legacy
receipt owners until their `.om` successors exist.

## Next bounded steps

1. Repeat the packed-list campaign for `renames` and the path words.
2. Add adversarial `argread` cases (no zero terminator, 1 MiB bound, short output).
3. When the receipt/checker `.om` successor exists, bind authentication and interpretation to the
   same bytes and test duplicate/trailing authority fields.
4. Feed accepted coverage into #624; keep device and network paths explicitly NOT_RUN.

No private data, live service or device was used for this inventory.
