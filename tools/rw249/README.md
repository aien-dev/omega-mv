# rw249: checked elimination ranked by cost (research, #249 E06)

Research tools for aien-dev/aien-architecture-mv#249. They try to remove steps from small omega
programs (chains of add, sub, mul, and, or with constants) and accept a removal only when an
independent checker proves it. Nothing here changes omega's runtime or its synthesis code.

- `rwcheck`: the checker. Judges rewrite contracts (`contracts/*.rw`) against a fixed list of
  algebra laws (`contracts/axioms.rwa`) and judges every rewrite the search proposes.
  Verdicts: `proven_in_scope`, `probe_agreement`, `rejected`.
- `rwsearch`: the search, with three approaches under one budget: M (hand-written rules),
  G (only moves that cut emitted instructions), B (bounded exploration ranked by the measured
  cost model, where an unknown cost makes a candidate ineligible, never free).
- `rwprobe`: shows where omega's six-input probe signature groups programs that are not equal.

Build and test: `make OUT_DIR=<dir> PHYSICS_DIR=<physics> rw249 test-rw249`.
Runs on any AArch64 Linux host (it runs the generated machine code on the CPU). No GPU.
Design: `docs/research/249-checked-elimination.md`. Evidence: `evidence/REWRITE-249/`.

`rwsearch account` writes one OEV1 file per accepted rewrite that removed steps. It replays the
recorded arm to recover each surviving step's stable id and refuses if the replay differs from the
recorded output (amendment A1 in `evidence/REWRITE-249/AMENDMENTS.md`).
