# L574 / Omega #621 development evidence

Author verification only. Independent reviewer and full local judge PENDING.
No signed PASS receipt, production retirement, general conversion or hardware claim.
Parent #625 dependency 69790fb4ad1e803ca79245430745b96129d1dcf4 remains explicit.

The early design RED ran all five public commands and observed five failures.
The first native build failed on a reserved function name (byte); changed to
an ordinary identifier. The test-only batch entry required splitting a helper
to fit the existing 16-register grammar and declaring its new local explicitly.
No new syntax/compiler words or production C fallback was introduced.

Historical failures are preserved. Gate1 failed in the Node reporter because it
attempted to stringify a BigInt. Gate2 completed the structural checks but the
harness mishandled EPIPE from a deliberately broken output-reset mutant. Gate3
completed structural/edge/command/mutant checks, then exposed incomplete legacy
fixture enumeration (56 selected versus >=59 required). The final enumeration
covers all original 59 fixtures across compiler, shell and physics source pools,
plus the parent arithmetic fixture. No gate threshold was lowered or test waived.

The pre-freeze author receipt records 10000 unique valid and 100000 unique
malformed units, with 131000 actual native requests: roundtrip, semantic hashes,
source replay, provenance hashes and malformed negatives. Every response's
consumed mode+input SHA256 is independently verified. Corpus membership is checked
by an independent Node decoder before execution. 37 malformed category counts
sum to 100000. All nine mutants compile to verified nonempty AArch64 executables
and run before being counted as caught; the output-reset mutant differs on 42
actual responses then traps on its overflowing output buffer, with all other
mutants completing their requested controls. No compile failure counts as caught.

60 OSC fixture encodings are compared byte-for-byte with the pinned base, decoded
and reencoded by the existing strict reader and accepted by unchanged checker2.
The C adapter is explicitly test-only legacy host orchestration; it supplies no
production serializer/checker/runtime. Lower source files are also unchanged
against the pinned base. Existing parent broader compiler/checker receipts remain
in evidence/UNIVERSAL-625/initial; they are not relabelled as this issue's results.

Reproduction: `make -j2 PHYSICS_DIR=/home/drakestapleton/workspace/hermes-desk/universal-L569-physics test-source-aware`.
The physics directory is read-only and this new host-only target links no physics
code. Production uses the existing source-only bridge bootstrap at its pinned
commit; Node and a host C compiler are independent test dependencies only.
SA_SEED_REPO can select a local clone containing that exact bridge commit. All new
builds use build/L574; no network service or device submission is involved.

Finite bounds, exact fields/statuses/API and unlowerable typed operations are in
spec/universal/source-aware-v1.md. Source frozen after the first author pass;
a committed-source cold replay and exact source/head readbacks will be added.
