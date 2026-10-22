# Seed lineage replay

Scope: rebuild the root seed and the pinned compiler chain on AArch64 Linux. This is not a whole-system migration or a proof of freedom from bugs. No C compiler, assembler or linker was invoked by these reconstruction commands. Existing host shell, Linux, file/text/hash utilities and Git remain part of the environment.

## Inputs

- Source commit: `29b12760f4c962606411d6bf25032aedef063301` in `aien-dev/omega-mv`.
- First recorded seed commit: `cd51427c3c4928ba3aba208add6d9aa9ad0ee798`.
- Execution date: 2026-10-22.
- Architecture and OS: AArch64 Linux.
- Root source: `seed/rung0/seed0.hex`.
- Compiler source: `seed/omc/omc.om`.
- Expected root SHA-256: `aca00cf3677236809f0f223437398c6bb429e4c65f13d58762bcf96d13799474`.
- Expected final compiler SHA-256: `191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9`.

## Root check

```sh
sh verify-root.sh /path/to/omega-at-the-pinned-commit
```

`verify-root.sh` reconstructs the seed with the original loader, executes it on its source, compares both outputs with the committed seed and checks the expected size and fingerprint. Output: `root-replay.log`, exit 0.

## Complete compiler chain

A fresh source export was made with:

```sh
git archive 29b12760f4c962606411d6bf25032aedef063301 seed | tar -x -C /isolated/replay-directory
cd /isolated/replay-directory
timeout 180 sh seed/omega-from-seed.sh
sha256sum seed/out/omc
```

Output: `full-replay.log`, command exit 0 and bootstrap's own `RC=0`. The resulting compiler hash matched the pinned expected hash. The bootstrap checks the root, each intermediate against recorded binaries, the three-stage compiler fixed point, forward and mutual calls, and two refusal cases. It does not run every compiler qualification suite; the older suite records remain at the pinned source commit.

The `/isolated/replay-directory` spelling above is a portable placeholder for the actual scratch directory, not a literal required folder. The retained logs omit account-specific paths. No binaries are included in this evidence directory.

The root loader's header says shell and printf only, but the script also invokes `tr`. The public walkthrough names that dependency rather than repeating the narrower header claim. Later scripts also use `cat`, `sed`, `stat`, `cmp`, `chmod` and SHA-256 tooling. The result establishes no C compiler in the reconstructed compiler ancestry, not absence of all C-built software on the host.
