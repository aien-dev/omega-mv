# Parent physical Mac replay — L615 bounded milestone

Source: f43e6755d95a9783040b5ec2147ae519e9b01a11.
Adapter source SHA256: b4c02c7dc2c14a420bf305cd3dbf2a4880da8c4e40c7ac8387d4a609935d56fb.
Unmodified final export SHA256SUMS checked before execution; source-commit.txt
matches source above. Original author Spark66-check logs remain historical Spark
evidence, not parent Mac runs.

Executed on physical ARM64 Mac, macOS26.6.2 build25G83, Darwin25.6.0.
Command from exported final/ directory: `sh mac-replay.sh`.
Actual gate exit0,12 behavior cases verified from individual exit files:
10 positive native programs, closed-stdout hello returning74, and a separately
source-compiled faulty-address hello that signs/loads/returns0/writes16incorrect
bytes. The latter is a caught behavioral mutant, not a loader failure.
All programs are actual ARM64 instructions, not emulated, and Apple ad-hoc
signing/strict verification/loader dependencies remain explicitly disclosed.
No compiler, source adapter or program source was changed for this replay.

Full raw receipt archive: parent-native-receipt.tar.gz
SHA256: 6529ba42eb39c71a395d0b50d261cf88be56b66f65611985578021312331398c.
118 current-run files include all signed executables, codesign logs, per-case
stdout/stderr/status, otool outputs, host/tool hashes, export input manifest,
replay command/source metadata and parent summary. Extraction preserves paths.
`parent-native.log` and `parent-summary.json` are also directly readable here.
Initial orchestration ran the preflight from the wrong directory and could not
find SHA256SUMS; no product test ran. The corrected export-root invocation is
this receipt's single actual native gate. It passed without product correction.

This clears the actual-native-runtime gate for this frozen bounded target on
THIS host only. Independent exact-source review is pending; do not close L615
or issue634 from this receipt. It does not qualify full macOS compiler/package,
installer, SDK/public ABI, runtime attachment, additional OS versions or general
language forms. No merge, deployment, third-party compiler or production runtime
fallback was used. See CONTRACT.md for exact accepted/refused forms.
