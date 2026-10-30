# Seeded security qualification

This directory extends the existing native host compiler profile. It is not an alternative compiler,
authority service, store, or release path. Existing L408 compiler integration and Renda review remain.

## Branch gate (#669)

Linux AArch64 only. Requires the existing seed reconstruction tools and GNU objdump 2.42 at
`/usr/bin/objdump`. From a fresh checkout at the reviewed source commit:

```sh
sh seed/omega-from-seed.sh
seed/out/omc < seed/qualification/security673/gate669.om > build/gate669
readelf -h build/gate669
chmod +x build/gate669
build/gate669 > build/gate669.log 2> build/gate669.err
```

Run each command only if the previous command exits zero. In particular, a failed compilation
must never be made executable or run. Existing bootstrap shell is a retained seed prerequisite;
all new first-party qualification logic here is `.om`. `readelf` is an external format inspection,
not a correctness proof. The gate refuses an existing `build/security673-native` directory with91
instead of overwriting evidence. Preserve and rename that directory before a fresh run.

The gate rebuilds the actual compiler twice, compares both binaries, builds its helpers, and saves
every input, output and child stderr under that new directory. Standard output records exact child
commands, redirects, observed exits and expected exits. Gate success is0. Compiler refusal is24
with empty output; malformed oracle inputs are62; independently decoded target mismatch is61.
Unexpected exit, signal, timeout, malformed executable, or I/O failure fails the gate. Each directly
spawned child has a30-second deadline. This is not a sandbox, process-tree budget, or full resource
limit. These trusted synthetic tests do not spawn grandchildren. Run no untrusted workload through
this coordinator as if it provided isolation.

`build/gate669 0`, `build/gate669 1`, and `build/gate669 2`, each with fresh scratch, are intentional
negative controls for unexpected exit7, signal9, and a45-second child exceeding the30-second
deadline. Each must fail with99; the latter two log the raw low wait-status byte9. They never
print the success line. An unexpected gate crash is INVALID, never a passing negative control.

The actual compiler encoder is exposed by an exact-once source transform, not copied into a
test implementation. The branch-limit mutant is compiled from a distinct transformed source.
Its incorrect output is decoded and rejected without execution. The current backward B.LO
caller initializes a local array with a short fixed loop; long backward source reachability is
not claimed from direct encoder probes. The imm26 capacity argument is inspection: all positions
are aligned within an8MiB output pool, below its signed128MiB reach.

GNU objdump is outside the seed compiler lineage. The `.om` checker consumes its selected text
format and rejects the enumerated malformed/missing/duplicate/wrong-target controls. It is not
a full disassembly grammar or authentication of attacker-supplied text. The checker and runner
share the seed compiler lineage. Binary equality and SHA256 identities are reproducibility and
identity evidence, not diverse compilation or proof of general semantic correctness.
