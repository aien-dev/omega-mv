# R6 compiler gate migrated to native Omega helpers

`seed/omc/gate6.sh` now reuses the qualified native ELF checker, byte comparator, SHA256 command and stream copier for compiler-stage verification and publication. Its `sha256sum`, `cut`, `cp` and `grep` invocations are removed. The runtime `sed` field-swap is replaced by the handwritten `t1x-swap.om` fixture, copied by Omega. Original compiler source and all original struct programs remain unchanged.

## Preserved acceptance criteria

All 12 positive programs, 13 refusal programs, and the field-swap mutation still run with their original names and expected exit values. `gate6-source-check.py` compares the entire ordered call list against `cd4436e8ffd5fc0b61d355cd6686f23a9f97472b`, then checks executed PASS rows independently of the runner's own total. It requires exactly the expected diagnostic-fixture membership and binds each fixture to its original input hash.

The 13 full diagnostics are captured from the qualified compiler, after verifying its fingerprint and the independently specified original error codes. These are regression snapshots, not independent proof of compiler correctness. Exact native comparison now rejects changed or missing diagnostic fixtures instead of accepting substrings. Newly built test executables are ELF-checked before execution, so an empty output cannot be interpreted as a successful empty shell script.

The checked-in field-swap fixture differs from t1x only in `get p x` -> `get p y`. The host verifier checks this exact relationship; no source translator is used in the gate. Wolfram independently confirms the two fixture hashes, expected exits 20/22 and the case-count sum. The first oracle input omitted the original file's final newline; the hash gate caught that. The fixture was corrected to preserve that byte, Wolfram was rerun, and the initial oracle/failure are retained rather than silently overwritten as passing evidence.

## Executed controls

`gate6-native.py` exports isolated seed copies and runs six scenarios: intact; all five removed host commands blocked; wrong diagnostic; missing diagnostic; unchanged mutation fixture; and an empty emitted executable. Named refusals are required. The baseline passes the intact run but fails five new requirements; the migrated gate passes all six. The blocked-tools run still executes all 26 compiler cases.

`gate6-native-execution.txt` retains the counted run and source/oracle checks. `gate6-native-source-replay.txt` records a fresh committed-source birth and gate run. Default repository `make test` is recorded separately against the isolated locked physics revision; it is not every repository target.

## Remaining boundary

The runner still uses shell, mkdir/chmod and cat for diagnostic display. Compiler production ancestry remains seeded Omega; host Python/Wolfram are external verification. The native tools establish consistency, not independent compiler trust/DDC. No full native test orchestrator, live driver/service cutover, or complete system retirement is claimed. Independent review of this gate migration remains pending.
