# Inspected references and mechanism comparison

Repo baseline57f5f425 and the current remote main observed/fetched as
`a8ffd2b912c84197d08f963b05f9c7d8b30a3f8a` were searched for Mach-O/Darwin/macOS
and additive target namespaces. Main has src/aarch64_target.h but no existing
seed-native Mach-O target module. The relevant seed implementation is
seed/omc/omc.om: elfhdr/eof_prog, s_text, vs_call, save/restore and finish.
That source is inspected read-only. No parent dirty source or binary is consumed.

Historical issue629 is the OSC/LLVM x86 target proposal, not a seed-native Mac
module. `~/handoffs/2026-10-20-aienos-run-macos.md` concerns a Rust/VZ/QEMU guest
launcher and native Mac hardware receipts for a different project. It provides
no reusable seeded Mach-O construction. L556 RISC-V is parked and not owned here.

Mechanisms considered:

|Mechanism|Decision/evidence|
|---|---|
|ELF header swap|Refused: compiler has fixed Linux stack, absolute text address, Linux syscall numbers/registers and error contract. RED uses real compiler output.|
|Fully static Mach-O/UNIXTHREAD|Refused: production XNU mach_loader.c explicitly rejects static non-x86 MH_EXECUTE; ARM64 hard pagezero is4GB.|
|Apple/LLVM link or C/Rust launcher|Refused as production construction/fallback; not seeded Omega. LLVM only decodes/verifies test artifacts.|
|Compiler/backend fork|Outside L408 ownership; no compiler edits needed for this bounded milestone.|
|Strict source-built adapter|Selected: exact full ELF header, frozen leaf instructions, no memory/absolute references, one completely specified hello, unknown inputs refused before any output. Dynamic Mach-O supports ASLR via relative branches and ADR.|

Apple primary sources were fetched from the following URLs. SHA256 identifies
the exact inspected content (upstream main URLs can move). Copies are retained
outside the repository at ~/handoffs/omega-developer-gaps/L615/.

- [XNU loader](https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/bsd/kern/mach_loader.c), SHA25691d94460c47479afcbf76f2c36265fde3a0bdfc39f37cf77f528cbec4cd327e3.
  Static ARM64 rejection and4GB pagezero checked in source.
- [XNU ARM syscall dispatch/returns](https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/bsd/dev/arm/systemcalls.c), SHA2567eb44232e6da37c8e6f5d2317ed5cf1d55d02032410b0ddf7007e4ca7b495df4.
  X16 dispatch, carry errors and X0/X1 returns checked in source.
- [XNU syscall numbers](https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/bsd/kern/syscalls.master), SHA2567e866891439ad5b3464777d2cacc3b82fc2e7f439fb5a8686c3e6f160d5aeab5.
  exit1/read3/write4.
- [XNU ARM SVC handler](https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/osfmk/arm64/sleh.c), SHA25693d0e76b996a091584f4b021658a7b128147e05fa1c7ccc1a929bbee6db67f75.
- [Mach-O format](https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/EXTERNAL_HEADERS/mach-o/loader.h), SHA2568780a1e622375833b563f488bc820422c2fb5b8727bb8df8ceaa73ab6f0f88e3.
- [dyld Mach-O validation](https://raw.githubusercontent.com/apple-oss-distributions/dyld/main/common/MachOFile.cpp), SHA25624852b5c5371ea46bd823cd246a747506da4d207e7ebe5f26b239ce046bfc980.
  Text/linkedit layout and dynamic load command structure inspected.
- [Apple code signing procedures](https://developer.apple.com/library/archive/documentation/Security/Conceptual/CodeSigningGuide/Procedures/Procedures.html).

Inference from those sources: the declared Mach-O layout and direct Darwin ABI
are a plausible native candidate. Only parent hardware replay can demonstrate
that the actual Mac loader/signing version accepts it. No native PASS is inferred.
