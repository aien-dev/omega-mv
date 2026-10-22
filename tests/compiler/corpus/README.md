# Compiler corpus: frozen OSC programs from modules that left omega

The OSC compiler, checker2 and the Omega emitter are tested over every real OSC program we have
(`tests/compiler/progs`, `tests/compiler/slices` and the folders here). When a module moves to its own
repo (ADR 0039, L396), its OSC programs are kept here as fixed test input, byte for byte as they were
at the move, so the compiler tests read exactly the same programs as before. They are test input only:
the live sources are in the module's repo and may change there without changing these tests.

| Folder | From | Now in | Frozen at |
|---|---|---|---|
| `osh/` | `src/osh/*.osc` | aien-dev/omega-host | omega a8ffd2b9 |
| `physics0/` | `tests/physics0/osc/*.osc` | aien-dev/omega-programs | omega a8ffd2b9 |
