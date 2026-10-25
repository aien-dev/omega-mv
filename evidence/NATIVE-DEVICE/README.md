# Native device primitives

The source and executable digests in `receipt.json` bind this qualification.
`seed/host/check.sh` reconstructs the compiler through the existing seed chain,
checks its self-rebuild, runs the native host programs, rejects malformed inputs
and compares70 original fixtures. The expanded device/mapping driver rejects36
compiler inputs. New native tests cover30 forked runtime refusals, exact syscall
errors, pointer-field isolation, register preservation, aligned word access,
shared writes, address collisions and reservation retirement after failed binds.

Sixteen actual compiled compiler variants are rejected. Their deltas are retained
under `controls/`; `controls.json` identifies each unchanged native test. Apply
one delta to a private copy of the pinned compiler source, build that source with
`seed/rung4/om4.bin`, then compile/run the named `seed/host/tests/<test>.om` with
the variant. For `memcheck`, compile the test with the original compiler and pass
the variant executable as argv1. Use a separate scratch directory for every run.
The record distinguishes exit/signal outcomes. The read-only and inaccessible
mapping mutants can fault in a forked child; the parent test rejects that signal.

The existing native issue669 gate also passes on this compiler. Its coordinator
used a private scratch path; source, boundary generators, actual execution,
independent decoding and refusal checks are otherwise unchanged. The final
gate line and child outcomes are retained in `gate669.log`.

These compiler checks open no GPU device. Physical correctness of the consuming
Omega NVRM implementation is separately recorded in CUDApp PR67. This is still
proposed work and requires independent final-source review. It does not establish
SM kernel correctness, full-model inference or experiment performance.
