# Bounded source ingress qualification

Threat: external source text exceeds supported syntax, numeric or storage limits, or consumes
pathological parsing work. Protected assets are compiler acceptance/refusal accuracy, emitted
program semantics and bounded resources for this finite campaign. Enforcing code is the existing
`seed/omc/omc.om` lexer, `tok_num`, `rdval`, `pushb`, function/name tables, pool limits and input
read bound. No compiler production change was needed for these selected source cases.

Author final command: `/usr/bin/time -v build/security673-hostile/gate 3`, stdout `gate5.log`,
stderr/resource record `gate5.err`, exit0. The gate was compiled by exact existing seeded compiler
6b5d9403 with successful build and ELF inspection before execution. Source/compiler/runner/tool
and every generated fixture identity are in `identities.txt`; `artifacts.tar.gz` retains them all.
All individual generated commands, exits, expected exits and elapsed nanoseconds are in the log.
The independent eighteen-row result and raw archive are in `../independent671`.

The oracle is the preexisting language profile, integer bounds and Boolean-negation parity, not
a copy of the parser. Cases include20 accepted versus21/million-digit refused decimals;1000,
1021 and1022 Boolean negations versus1023 refusing value-depth capacity;512/513 nested blocks;
million-byte literal;511/512 auxiliary functions; NUL; oversized global pool; unterminated quote;
unknown name; million spaces; and input exceeding8MiB. Eight accepted fixed-compiler programs
execute with predicted results, including odd/even Boolean outcomes; ten refusals produce empty
output. Expected refusals are6,14,16,23,22 or3 as specified in the raw table.

Meaningful compiled negative control: an exact-once transform changes only `if gt ndig 20` to21.
That compiler incorrectly admits a21-digit zero literal, emitting an ELF216 bytes long. The
fixed compiler refuses the same bytes with6 and empty output. The violation is the profile's
numeric token limit, independently documented before the experiment. The mutant ELF is retained
and format-inspected but not executed. No build failure, crash or timeout counts as mutant success.

Observed author wall time was recorded by GNU time, including0.09seconds and8160KiB maximum
resident set in the prior final-profile run; use `gate5.err` for the final exact observation.
Independent final replay similarly observed0.09seconds/8160KiB. These are finite host observations,
not enforced memory/CPU limits or universal complexity guarantees. The existing30-second direct-
child deadline remains in the coordinator. Native Linux AArch64 only, no device execution.

Failed attempts remain: generator error13 came from an overlong developer identifier; the first
deep-expression fixture incorrectly used two bare literals (11), and its successor attempted an
unsupported bool-to-int conversion (9). Both gate attempts failed99 and are excluded. The final
fixture is a valid Boolean condition, with results derived from parity. No production bug is
inferred from those invalid development fixtures. Initial and superseded log files are historical.

Residual obligations for #671: other source/import, manifest/receipt/RPC/trace/device parsers;
authentication/canonicalization equivalence; complete systematic truncation/mutation fuzzing;
kernel resource isolation; cross-platform and compiler-independent semantic proofs. This slice
does not close #671 or #624 and does not qualify an untrusted-workload sandbox.
