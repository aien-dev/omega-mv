# L546 milestone 1: preserve lock file bytes

L537 parent review8c6d5d0 reproduced truncation through symbolic and hard links.
This follow-on changes only tools/with_build_lock.sh and adds two host test files.
The Makefile and all original acceptance/history tests remain byte-identical.

The helper checks symbolic links, regular file type and link count before opening,
opens in append mode without truncation, then checks descriptor link count and
name/descriptor identity. It retains fd8 and the existing flock/exec lifecycle.
Valid empty or nonempty regular locks keep their inode and bytes. No lock is
unlinked. Canonical output directories still define the exclusion scope.

This uses existing Linux shell/stat/flock primitives; no compiler bootstrap or
new runtime dependency. Inspection found append-mode flock in chip_run.sh and
host quiet-build, and stat already used across host tools. The output namespace
is trusted and caller-owned. This correction refuses pre-existing unsafe leaves;
it does not attest security against hostile concurrent namespace replacement.
Linux /proc/self/fd is required for descriptor checks.

Actual commands, from the assigned worktree:

```
git show d84142f7:tools/with_build_lock.sh > build/L546/immutable-old-helper.sh
chmod 444 build/L546/immutable-old-helper.sh
quiet-build node tests/build_lock_file_safety_test.mjs build/L546/red build/L546/immutable-old-helper.sh linked
# exit1 before source fix; both exact safety assertions FAIL, victim hash becomes
# empty hash, command executes. No signal, timeout or unrelated failure.
quiet-build node tests/build_lock_file_safety_test.mjs build/L546/green
# exit0, 18/18 direct/public-make rows PASS
quiet-build node tests/build_lock_file_safety_controls.mjs build/L546/safety-controls
# exit0: production PASS; old unsafe open, unchecked append and regular-file
# truncation each exit1 at precisely the expected safety rows.
quiet-build node tests/physics_build_concurrency_controls.mjs build/L546/acceptance
# exit0, all original8 exact rows PASS, including three original behavioral mutants.
```

Safety refusal requires exit2, the specific refusal diagnostic, preserved victim
SHA256, stable leaf inode and absence of the command marker. Watchdogs/signals are
failures. Positive command markers prove the fixture command really is reachable.
Cases: new, empty regular, nonempty regular, output alias, symlink, dangling link,
hard link, FIFO and directory, through direct helper and actual Makefile dispatch.
The public probe includes the real Makefile and adds only a marker target.

Immutable old helper retained here with source provenance. New unsafe-open mutant
replaces only the lock-open/refusal block with the original redirection and keeps
flock/exec intact. Unchecked append proves nontruncation alone cannot satisfy the
command boundary. Regular truncation proves ordinary lock bytes are also covered.
No timeout or crash is accepted as refusal or as a killed mutant.

New and original raw logs/results are in raw/. pins.json binds the tested source.
Original nine-case historical packet and L537/L532 evidence remain untouched.
Startup current84 issue/63 PR inventory, live ledger, exact refs, branches and
read-only recent app-server SQLite thread metadata were reconciled in private
build/L546. L537/L532/decoder/bridge/CUDApp owners were not restarted or changed.

Independent corrective-head review NOT_RUN, pending parent L487/desk L488.
Hardware/current35B/full production driver/shared-train gates NOT_RUN, retained
by canonical owners including L386. This is host file-safety evidence only.
