# Native chdir capability for .om automation

Date: 2026-10-23. Branch: `fix/security673-qualified`. Platform: Linux AArch64.

## Change

`chdir A off n` is now a native host word (code 136, Linux syscall 49). It uses the existing
private path copy, moves the copied path pointer from x1 to x0, and returns the kernel result.
`unlink` already existed (code 111). Both are expression-valued: `rc = chdir d 0 len d` and
`rc = unlink d 0 len d`.

This removes the last missing capability needed by a `.om` replacement for the shell host gate:
compile a test, change into a scratch directory, run it, and remove its file.

## Qualification

| Command | Result |
| --- | --- |
| `sh seed/host/gate.sh` | exit 0, including `PASS native chdir, create below it and unlink` |
| `gate669` | exit 0, `GATE669 PASS` |
| `gate4` | exit 0, 217 cases, `GATE4 PASS` |

`seed/host/tests/chdir.om` creates a directory, changes into it, creates and removes a file, and
returns 0 only if every step succeeds. The shell gate checks the file is gone.

## Residual

The host gate itself is still shell. It is the next automation cutover; this word exists so the
replacement can run each test in a scratch directory without a shell `cd`. No other legacy script is
claimed as migrated.
