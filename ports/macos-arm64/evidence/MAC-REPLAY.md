# Final L615 native Mac replay request

macOS status: NOT_RUN. Parent L613 owns this run. Issue634 remains OPEN.
Use this final export, not the historical first-candidate export.

On the actual native ARM64 Mac, copy this whole directory and open Terminal in
that copied directory. The gate validates every source/artifact hash, creates a
fresh receipt folder, copies each unsigned artifact to a signed sibling in that
folder, uses Apple's codesign and otool, and executes native integer/hello cases.
It does not install software or change system settings. Expected success is
12 native behavior cases including closed stdout and a correctly loadable,
source-compiled faulty address control that writes different bytes. Any signing,
loader, timeout or behavior failure stops the gate and preserves evidence.

Literal commands:

```sh
shasum -a 256 -c SHA256SUMS
sh mac-replay.sh
```

Return the full native-receipt.XXXXXX directory, last-receipt-path.txt and terminal
exit. It includes host/version/tool hashes, strict signing evidence, Apple format
inspection, signed binaries/hashes, stdout/stderr and exact exit for each case.
The Linux adapter and original ELF inputs are provenance, not Mac executables.
Only control-hello-address is executed by the gate. The other exported controls
remain inspection/refusal oracles, not arbitrary native syscall experiments.

Compile/reproduce on Spark from the owned repo:

```sh
ports/macos-arm64/build.sh
quiet-build node ports/macos-arm64/tests/run.mjs
ports/macos-arm64/tests/export-replay.sh
```

The shared compiler SHA256 is checked before every invocation. Full Spark gate
is66 checks,10 Linux native source controls,5 source-compiled adapter controls,
172 exact-hello byte refusals. Native Mac and independent review remain NOT_RUN.
