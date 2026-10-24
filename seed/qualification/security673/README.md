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

## Branch-form semantics (#669)

`branch-forms.om` adds fifteen small programs covering every source conditional form: `eq`, `ne`,
`lt`, `le`, `gt` and `ge`; comparison with zero; `while` and `for` back
edges; the unconditional B over an `else`; a branch inside a called function; and the shift-width
and divide-by-zero trap sites. Independent disassembly shows comparison with zero uses CMP/B.NE;
the divide-by-zero fixture exercises CBZ. The coordinator compiles and executes each and compares the exit
with the fixed table in `formwant`. These cases check target selection, while `boundaries`,
`positive`, `negative`, `array-fill` and `mutant` continue to check the imm19 range and refusal.

All imm19 writes still flow through `disp19`/`patch19`. The compiler does not expand or relax a
branch: a target outside the signed 19-bit word displacement, or an unaligned target, is refused
with error 24 before an artifact is accepted. Unconditional B uses `patch26` and the inspected
8 MiB output-pool capacity. Evidence is `evidence/SECURITY-673/branchforms1`.

## Strict launch profile (#670)

`execpure` (native host word 135) pins one executable as descriptor 3 and then applies a fixed
kernel profile before `execveat(AT_EMPTY_PATH)`: Landlock execute/read only beneath that inode,
`PR_SET_NO_NEW_PRIVS`, CPU/file/stack/core/process/descriptor/address-space limits, and a seccomp
filter that allows only stdin read, stdout/stderr write, the pinned exec and exit. Any setup
failure returns the kernel error; there is no weaker fallback.

`pure-child.om` is the hostile consumer; `pure-launch.om` is the caller; `pure-raw.om` emits four
fixed native images for absolute exec, raw open, raw getpid and a 1 GiB mmap. Build the launcher and
child with the rebuilt compiler, create `build/security673-pure/private.data` and `judge.data`, then
run `pure-launch build/security673-pure/pure-child` with `public-input` on stdin. Every assertion
must pass and the protected files must be unchanged. Evidence and traces are
`evidence/SECURITY-673/pure2`. This is a fixed strict profile, not a general sandbox; grant
generations, revocation and durable budgets remain open under #670/#649.

Both words preserve the caller-assigned descriptors 0, 1 and 2. `execpure` temporarily opens the
selected executable as descriptor 3; CLOEXEC closes it when execution succeeds. The three standard
descriptors remain real capabilities and must be assigned by the trusted caller. This does not
establish grant identity, generation, revocation or authority-aware reuse.

Invoke `execpure` only in a fresh supervised child. A setup failure can leave that process partly
restricted; it must report failure and exit, without retrying through `execprog`. All three standard
descriptors must be present. The fixed profile supports static Linux AArch64 executables on a kernel
with close_range, Landlock ABI3 rights and seccomp filter support. Missing facilities cause refusal.
The pinned inode is not an immutable content identity. The trusted supervisor must own and protect
the selected artifact and supply bounded input, wall deadlines and pipe-output accounting. CPU and
regular-file size limits do not supply those additional guarantees. Fork/clone denial comes from
seccomp; RLIMIT_NPROC alone is not a sufficient process-isolation boundary.

With fresh `build/security673-native` and `build/security673-pure` directories, `build/gate669 4`
rebuilds and tests the strict profile. It checks actual child file/network/process denials, raw native
syscalls, two compiled bypass controls, the planned SIGXCPU termination and exactly one MiB regular
output. Filter removal must expose raw getpid (fixture exit71), and inode-fence removal must execute
the harmless unassigned static fixture (exit77). Unexpected signals and deadlines remain INVALID.
Fault-injected missing kernel facilities are separately traced in `pure1`; independent review and
fresh integrated gate receipts supplement the earlier `pure2` campaign.

## Bounded source ingress (#671)

With fresh scratch, `build/gate669 3` runs eighteen source cases through the real rebuilt compiler.
The generator is `hostile-corpus.om`; the existing coordinator is reused. The campaign covers
decimal length, nested values, nested blocks, large literals, function count, invalid control byte,
oversized pool, unterminated quote, unknown name, long whitespace and input capacity. Eight
accepted fixed-compiler programs execute with expected exits; ten refused inputs leave empty
artifacts. Both parities of the nested Boolean expression are exercised.

A separately compiled numeric-limit mutant changes the existing20-digit rule to21. It accepts
the21-zero source, violating the published profile; the fixed compiler refuses6. That mutant's
program is format-inspected and retained, not executed. The independent report records the
distinction. The gate logs elapsed nanoseconds for each child. External `/usr/bin/time -v` can
record observed campaign wall time and maximum resident memory; those observations do not
establish a general complexity bound or enforce a kernel memory limit.

This is a deterministic bounded source campaign. Other ingress paths, broad grammar/mutation
fuzzing, authenticated canonicalization and kernel resource limits remain under their existing
owners. Current evidence and independent review are `evidence/SECURITY-673/hostile1` and
`independent671`. Earlier failed development fixtures are preserved and excluded from PASS.

The same gate also runs nine packed-list cases through a compiled consumer. `packet-corpus.om`
builds valid, empty-count, over-count, truncated-record, trailing-byte, empty-path, embedded-NUL
and oversized-record packets; `pkt-consumer.om` reads one packet and calls `execprog`. Accepted
modes execute `/bin/true`; malformed modes trap 3 with exit 103 before any kernel exec. Evidence is
`evidence/SECURITY-673/packet1`; the wider ingress map is `evidence/SECURITY-673/ingress-inventory.md`.

The strict CPU control requires a signaled wait, including Linux's optional core flag. A normal
exit152 is refused by mode5 with99. A separately compiled mode6 mutator control removes only that
check and wrongly accepts the same fixture with0. Earlier supervisor0/1/2 controls had an
unconditional failure after execution; the corrected runner returns0 if an invalid result is ever
accepted, making expected99 meaningful. Original observations remain historical.

On the inspected host, kernel crash collection is piped to Apport; CORE0 does not disable a piped
collector. Private/holdout input confinement remains unqualified until the runtime owner verifies
crash handling in a disposable execution environment. No live crash policy was changed and these
fixtures contain only synthetic data. See `pure4/core-pattern.txt` and independent review.
