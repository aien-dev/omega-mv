# L615 bounded target evidence

Final Spark gate: **exit0,66 checks**, including10 successfully compiled native
Linux fixtures,5 successfully compiled discriminating adapter controls and172
single-byte exact-hello refusals. Native Mac gate: **NOT_RUN**, host guard exit77
on Spark. Independent human/agent review: **NOT_RUN**, parent/desk owned.
No emulator is used. Issue634 remains OPEN.

Build and repeatable gate commands, from repository root:

```sh
ports/macos-arm64/build.sh
quiet-build node ports/macos-arm64/tests/run.mjs
ports/macos-arm64/tests/export-replay.sh
```

Build consumes only the pinned, read-only shared seed compiler. Its hash is
checked before each compiler invocation. Successful compilation, nonempty ELF,
AArch64 ISA and expected native source behavior precede artifact/control claims.
Native Linux behaviors: return42=42, sum=45, branch=23, overflow=101, bitwise=58,
divide=25, divzero=102, shift=24, hello=0/exact16 stdout bytes, largest=203.
The largest valid input is16384 bytes from4043 source additions;16388 bytes is
separately compiled and refused. These boundary sources are generated test
fixtures from explicit repeatable Omega statements, not production generators.

Every positive target is exactly32784 bytes. Independent Node byte/format checks
verify commands, addresses, permissions, entrypoint, absence of relocations,
byte preservation and every allowed change. LLVM18 independently parses the
nine regular target files. LLVM's assembler independently confirms ADR,
Darwin exit/write register setup, SVC80 and CNEG encodings. The largest file
also passes the full independent byte/format oracle.

Five source-compiled controls:

1. Linux exit register left in target: full target produced, semantic oracle catches it.
2. Entry points at wrong location: full target produced, entry oracle catches it.
3. Darwin error negation omitted: full target produced, exact thunk oracle catches it.
4. Hello text address shifted four bytes: full target produced, address oracle catches it.
5. Unknown-instruction validator bypassed: full target produced, refusal gate catches it.

These are Spark format/refusal controls, not native Mac runtime passes. The Mac
replay runs the fourth control only after signed native hello passes, and demands
successful loader execution/exit0/nonempty16-byte output with different bytes.
A compilation, signing, loader, signal or timeout failure cannot count as catching
that runtime control. Other controls are exported for review, not automatically
executed as unknown host syscall programs.

Refusals cover size/truncation, magic, ISA, endianness, ELF tables, addresses,
segments/globals, startup, unknown/memory/indirect/SP/reserved-register operations,
branches outside the body or into syscalls, malformed syscall shapes, altered
hello bytes and compiled unsupported memory/calls/stdin. All refusal cases
check empty adapter stdout, exact exit and unchanged preexisting output bytes
and mode through target.sh. Error precedence is checked with combined defects.
The output-error control uses /dev/full and observes exact exit29.

All gate generations and initial compiler failures are preserved. First gate
reported54 checks and incorrectly printed156 for its172 actual byte mutations;
the label was corrected in the second gate (55 checks). Subsequent gate65
added boundaries/precedence/host-write/assembler/refusal control; final66 adds
native hello-address replay control. Earlier passes are historical, not final
source qualification. Logs retain their real output.

Artifact hashes are in qualification-sha256.txt. Primary production hashes:

|Artifact|SHA256|
|---|---|
|adapter.om|b4c02c7dc2c14a420bf305cd3dbf2a4880da8c4e40c7ac8387d4a609935d56fb|
|seed-built Linux adapter|0885e00ff1405e05ed93790ad4c7452ea8cea87ac2d8d4dad44eac1679ffead1|
|unsigned hello Mach-O|874529dd25701c4df2cd1bd94de419035ec5e702bffa221a22d2175acf81864b|
|unsigned return42 Mach-O|5ed81b247c443a6f8b572b3e00bd36f191c47e14e2d7aa2ed4a27f9423367f9e|
|shared compiler|191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9|

Raw generated evidence and binaries are retained at
`ports/macos-arm64/build/qualification/` on Spark. Parent export:
`/home/drakestapleton/handoffs/omega-developer-gaps/L615/mac-replay/final/`.
Its manifest includes exact source, original compiler output, converted output,
all controls, compiler pin and literal native replay gate. The earlier
`mac-replay/first-candidate/` is preserved separately. The final export is the
one to replay. Source files must match the export manifest before running it.

Parent replay, in the copied final export directory on native ARM64 macOS:

```sh
shasum -a 256 -c SHA256SUMS
sh mac-replay.sh
```

The gate creates a fresh native-receipt directory per attempt and saves host,
Apple tool hashes, strict signing results, inspection output, signed binary
hashes, actual stdout/stderr and exits. Perl is disclosed host test orchestration
for a5-second alarm per child; target computation executes native ARM64.
Return the complete receipt directory to the parent handoff. Do not promote
minimum-OS, SDK, offline installer, stdin, general ABI or platform qualification
claims from this milestone. No upstream compiler feature request is needed for
this frozen scope. Further source forms depend on L408-owned qualified output.
