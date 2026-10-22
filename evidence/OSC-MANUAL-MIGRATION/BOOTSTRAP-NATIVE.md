# Bootstrap caller migration — native comparison and hashing

The real `seed/omega-from-seed.sh` now builds the qualified native ELF checker, byte comparator and SHA256 command immediately after the three compiler generations exist. It checks their executable shapes, compares stage1/stage2/stage3 byte-for-byte, then writes the compiler fingerprint using the native SHA command. The three `sha256sum | cut` pipelines have been removed from this bootstrap caller.

This supersedes earlier milestones' statements that this caller was untouched. Existing historical evidence is retained. No compiler source, seed machine code, or other owner's worktree was modified.

## Gate and scope

`tests/seed-migration/bootstrap-native.py` exports the committed seed tree to a private directory for each test, overlays the script under examination, places refusing `sha256sum` and `cut` commands at the front of PATH, and records every attempted invocation. Five cases:

1. Full birth succeeds with neither command called; Python independently verifies the resulting compiler hash and exact fingerprint file against the established pin.
2. Appending one byte to stage3 is rejected specifically by native byte comparison.
3. A missing SHA source refuses compilation rather than falling back to a host command.
4. Invalid comparator source refuses with compiler error 16.
5. A compilable SHA stub producing no output is rejected for digest length.

Negative cases require their named diagnostic as well as nonzero exit and absence of the success sentinel; an unrelated failure cannot satisfy them. `--baseline` selects the prior bootstrap script at `837a3d6c7f5450ecf2086af1bdfbff61ee7d933d`. The baseline fails all five cases with blocked host-tool invocations; the new script passes all five. Existing 26-case compiler regression and the eleven-fingerprint integration gate also pass. Clean committed-source birth evidence is retained separately.

## Remaining dependencies

Early seed checks still use host `cmp` because the Omega compiler has not yet been born. Shell, loader printf, chmod, mkdir, cp, wc, cat, stat, sed, grep and path utilities remain host orchestration. `native-closure.sh` deliberately retains its independent pre-execution host check of the compiler pin. Thus this is removal of two host-tool dependencies from one actual bootstrap caller, not removal of all legacy tools or a pure-Omega boot environment.

The native stage comparator and hasher establish self-consistency and reproducibility; a compiler building its own verifier is not independent correctness proof or DDC. The outside test oracle verifies the preserved compiler identity. Native helper limits remain the contracts documented in ELFCHECK.md, BYTEEQUAL.md and SHA256.md.

The inherited optional om4 comparison with `|| true` remains outside this change and is not counted as a passing gate. Full repository suite and independent review of this bootstrap delta are not claimed. No live GPU, kernel module or service changed.
