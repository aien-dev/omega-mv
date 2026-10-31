# Host fixture gate as `.om`

## Change

`seed/host/gate.sh` is deleted. `seed/host/gate.om` is compiled by
`seed/out/omc` and performs the fixture gate itself: it creates and enters
`build/seed/host/run`, compiles each fixture with the seed compiler, runs it,
enforces a native 10-second deadline with `waitpid`/`killpid`, and rejects every
leftover artifact. `seed/host/check.sh` only builds and launches the gate.

The gate needs the native `chdir` word from `a897ecc`; no shell command performs
a test operation.

## Identity

```text
39d4be6db9c1cbc99158251a8ecbd4eb90220fd84d21cb3d7f0f4185660e776d  seed/host/gate.om
66b4cf34e5cd2c8ff003bd142e7309f52178b66c2380f78ac313b6eb8b67471b  seed/out/omc
```

The compiler is the self-reproducing fixpoint reported by
`seed/omega-from-seed.sh` (`PASS omc fixpoint 66b4cf34...`).

## Verification

Command: `sh seed/host/check.sh`

Result: exit 0. The gate printed all ten fixture lines, including the new
`PASS native chdir, create below it and unlink`, followed by:

```text
PASS 18 bounds, packet, mutability, type and function-mode negative controls
PASS 41 total host negative controls, including cwd, readlnk and sockopt
PASS 70 original fixtures retain identical machine code, refusal status and diagnostics
PASS native-host qualification; no C/Rust compiler, assembler or linker used
```

Raw output: `host-gate.log`.

## Limits

`check.sh` remains shell because it is the bootstrap/quotation boundary: it
rebuilds `seed/out/omc` and launches the gate. The gate uses `seed/out/omc` as
the compiler under test and `/proc/self/exe` for the `location` fixture, so that
fixture is emitted as `build/seed/host/run/location` rather than the shared
`fixture` name.
