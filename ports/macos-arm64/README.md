# Bounded macOS ARM64 target (L615)

The handwritten Omega adapter builds selected seed-compiled integer/control-flow
programs and one exact hello into native ARM64 Mach-O candidates. Native macOS
execution and independent review are **NOT_RUN**. Issue634 stays **OPEN**.
This milestone does not qualify a Mac SDK, installer or complete developer package.

On the assigned Linux ARM64 Spark worktree:

```sh
ports/macos-arm64/build.sh
quiet-build node ports/macos-arm64/tests/run.mjs
ports/macos-arm64/target.sh INPUT.elf OUTPUT.macho
```

The build script verifies the immutable shared seed compiler's hash immediately
before consuming it. The target adapter verifies the entire input before writing.
The file wrapper preserves existing output on refusal. Read [CONTRACT.md](CONTRACT.md)
for exact instruction/header/ABI/resource/refusal closure and host dependencies.

Pure leaf integer/control flow is supported within the frozen instruction subset.
The hello family is precisely [tests/hello.om](tests/hello.om), including its
stdout/error behavior; changed literals or syscall shapes are refused. Stdin,
arrays, calls and other program forms are explicitly refused. All production
computation and file construction use seeded Omega. Shell/Node orchestrate tests
and files; LLVM is an external format/instruction oracle. Apple dyld/libSystem
and host signing tools are required and are not claimed to be independent.

[evidence/initial-red.log](evidence/initial-red.log) preserves the original failing
boundary assertion against real seed output. Qualification artifacts are in
`build/qualification/`, with full source/input/output hashes, native Linux exits,
refusals and compiled negative controls. [tests/mac-replay.sh](tests/mac-replay.sh)
is the parent-owned native Mac gate. Export instructions and raw receipts are
recorded in [evidence/RESULTS.md](evidence/RESULTS.md).
