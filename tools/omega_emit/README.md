# omega_emit: op list in, checked OSC IR out

`omega_emit` turns an op list (a JSON description of a program, written by a model or by the
Omegese front end) into the same binary program format (OSC IR) that the OSC compiler `oscc`
writes. It checks every op as it reads it and refuses the first wrong one with the place and the
reason. `omega_emit lift` goes the other way: IR bytes back to an op list.

- Shape of the op list: [SCHEMA.md](SCHEMA.md).
- Since L093 (2026-10-19) the op list also carries OSC-2 structs (struct table, `own:`, `mut:` and
  `shared:` struct references, `fload`/`fstore`), contract text and arenas, so a program with a
  struct can reach the same IR bytes on both roads. The struct gate checks every positive struct
  program in `tests/compiler` and the three in [structs/](structs/): IR from `oscc` must equal
  `emit(lift(IR))` byte for byte, and checker2 must accept both (results:
  `evidence/EMIT-STRUCTS/round-trip.md`).
- Units without structs keep IR version byte 1 and the same bytes as before.

Tests (host only; nothing here runs on the GPU):

```
make test-omega-emit            # every op, every refusal with its reason, negative controls (mutants)
make test-omega-emit-structs    # the struct round trip and checker2 gates, writes the evidence table
```
