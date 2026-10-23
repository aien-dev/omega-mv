# L717 committed-source replay

- Source commit: `98844b3648823aa2de13c48af9eeae5f5515f80c`
- Branch: `ua/buffers-L717`
- Export: `git archive HEAD omega/native/universal/buffers evidence/UNIVERSAL-ADAPTER/L717 | tar -x -C build/L717/immutable-source`
- Export stream SHA256: `f638935b57dc4f96674c7e5e9dbf1f0db2df5849b0f3105d7ea862abdaa49252`
- Replay command: `build/L717/immutable-source/omega/native/universal/buffers/run.sh /home/drakestapleton/workspace/hermes-desk/ua-buffers-L717/build/L717/immutable-replay`
- Compiler: `/home/drakestapleton/handoffs/universal-adapter-L707/shared/omc`, SHA256 `191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9`; runner rechecked the compiler pin before each candidate/mutant compile.

Observed replay: candidate compile RC 0, nonempty 15,288-byte AArch64 ELF `EXEC`, no `INTERP`, execution RC 0. Compiled behavior mutants each produced nonempty AArch64 `EXEC` and were caught by nonzero program status: no-cancel RC 162, no-generation RC 42, no-rights RC 161. Candidate and all three mutant ELF bytes compared equal (`cmp` RC 0) with the author-run binaries committed in `author-run/`.

This is native executable behavior on the Spark Linux AArch64 host, not GPU/driver/kernel evidence. No latency or throughput measurements were taken. Parent source review, full application integration, hardware/GPU work, and merge approval remain open.
