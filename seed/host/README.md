# Native host operations

Drake authorized extending Omega before rewriting benchmarks-mv on 2026-10-23,
with no outside implementation help. These operations are implemented in
`seed/omc/omc.om`. That Omega compiler emits the machine instructions itself.
The seed chain builds it and it rebuilds itself to identical bytes. No C/Rust
implementation, translator, external model, foreign compiler, assembler or
linker implements or builds these additions.

The existing shell seed loader remains the first bootstrap step, as described
in `seed/README.md`. Shell scripts coordinate builds and tests; they do not
implement the host operations. Programs call the Linux ARM64 kernel directly,
as the original `read`, `write` and `exit` already do. This is Linux-hosted
operation, not a native AienOS qualification or a GPU qualification.

The language contract is [OMEGESE section 10](../../docs/one_layer/OMEGESE.md).

## Qualification

From the repository root on Linux ARM64:

```sh
make -j2 build/omegese/omegese build/omega_emit/omega_emit build/checker2/checker2
quiet-build sh seed/host/check.sh
```

On a machine without the Spark build coordinator, run the script directly.
Missing IR oracles return `NOT_RUN` with status 3 before rebuilding; they cannot
produce host qualification. The script rebuilds the seed chain, proves compiler
self-reproduction, runs the independent C-reference IR gate, exercises native
Omega fixtures, rejects malformed inputs, and compares the reviewed original
fixture baseline against the original native parent.

Verified on the Spark on 2026-10-23:

- Native file create/read/write/seek/sync/chmod/close/unlink and errno checks.
- Monotonic nanoseconds, realtime nanoseconds and a measured native sleep.
- Argument copying, including empty arguments, spaces, UTF-8 bytes, and refusal
  of short output buffers without modifying them.
- Socket exchange between native Omega processes, including local sockets and
  TCP loopback on a kernel-selected ephemeral port.
- Poll timeouts, fork, accept, exec with exact arguments, exec failure, pipes,
  descriptor duplication, process termination and child reaping.
- Directory creation/enumeration/removal, file metadata and no-replace rename.
- Current-directory and symbolic-link reads with explicit count conventions,
  nonzero slice offsets, truncation checks, overlapping buffers and errno checks.
- Socket options verified by the changed bind result on an ephemeral UDP
  loopback port, including readonly option bytes and invalid options.
- Forty-one negative controls for bounds, malformed packets, readonly arrays,
  wrong types, invalid paths and use outside program mode.
- The baseline in `original-fixtures.tsv` pins paths and observed parent statuses.
  Its 62 cases retain identical machine code, refusal status and diagnostics.
  This is the actual selection at `ec07d25d`; older prose incorrectly said 70.

Fixtures run in a unique temporary directory under `build/seed/host/`. The
network test connects only to its own local server. Process tests reap their
children. No service is installed or changed. `timeout` only bounds test
processes; timed operations inside the tests use the emitted native code.

Offline gate integrity controls (temporary repositories, no services or GPU):

```sh
nice -n 19 python3 seed/host/test_gate_integrity.py
```

They exercise recursive and long secret searches, tracked generated paths,
search errors, missing/empty/duplicate corpus obligations, common compiler
refusals, and missing IR oracles. The secret gate checks all tracked paths
(including build output), plus untracked paths outside root `build/` and `.git/`;
its manifest forbids cache reuse across commits.

## Source authority and limits

Instruction forms come from the existing hand-audited instruction table in
`seed/rung2/table.s2`, which identifies Arm manual sections. Linux interfaces
were checked against the installed ARM64 headers: `asm/unistd.h`,
`asm-generic/unistd.h`, `asm/fcntl.h`, `asm-generic/fcntl.h`,
`asm-generic/stat.h`, `asm-generic/poll.h` and `linux/time.h`. The architecture
header overrides generic constants: ARM64 `O_DIRECTORY` is 040000 octal.
These are interface references, not imported implementation code.

The additional keywords 132 through 134 are `cwd`, `readlnk` and `sockopt`.
Installed ARM64 `asm/unistd.h` includes the generic definitions for `getcwd`
(17), `readlinkat` (78) and `setsockopt` (208). `linux/fcntl.h` supplies
`AT_FDCWD`; ARM64 `asm/socket.h` includes the generic `SOL_SOCKET` and
`SO_REUSEADDR` definitions. Adding these reserved words changes the compiler
digest. The reviewed baseline has 62 original fixtures after the old content
filters, not the 70 previously stated here. The new fixed manifest prevents
missing files, changed skip boundaries or common compiler refusals from silently
shrinking that obligation. New reserved words cannot be used as ordinary identifiers.

This extension supplies host facilities for the benchmark migration. It does
not implement a JSON library, floating-point types, the full benchmark suite,
or the sibling runtime's in-process interfaces. Existing numeric, array,
register and loop capacities remain in force. The language has no exposed
pointers or arbitrary-syscall escape hatch.
