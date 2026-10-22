# Bounded macOS ARM64 target (L615)

Under construction. Native macOS execution: NOT_RUN. Issue #634 stays OPEN.
Production namespace belongs to L615, compiler source belongs to L408.

The seed compiler produces Linux AArch64 ELF with Linux syscalls, fixed stack
and absolute text addresses. The initial real seed-compiled integer fixture
fails the independent Mach-O boundary assertion (evidence/initial-red.log).

Selected mechanism: a handwritten Omega format/target adapter with full input
validation. First scope: no-memory leaf integer/control-flow seed programs and
one exact source-built hello program, preserving all other valid instructions.
No general ELF translator, source compiler replacement or SDK qualification.
Unknown headers/instructions/branches/addresses/program forms are refused.

Apple XNU rejects static ARM64 MH_EXECUTE on production kernels. Output must use
dyld, PIE, four-gigabyte PAGEZERO, and explicit native entry. Apple signing and
inspection tools are separately disclosed host dependencies. Code generation
and format construction remain seeded Omega; no foreign tool builds the target.

References inspected: Apple xnu bsd/kern/mach_loader.c, bsd/dev/arm/systemcalls.c,
EXTERNAL_HEADERS/mach-o/loader.h; dyld common/MachOFile.cpp. Exact contracts,
source pins and replay commands will accompany the implemented milestone.
