# Native device and mapping prerequisite

Functional source: `d80b428c6ed9e3864fd7fc02cd7392bd4ad67f56`.
Parent native host profile: `f0499f415571bdd7496b6de391096915e78f7556`.
Compiler SHA256: `1d1aad082539399920bc6c90ff41f0a0c3a1ca3225e518fd0d259e0aeb71732d`.

The seed chain reconstructed this compiler with identical first, second and
third stages. A fresh export of the exact source passed the existing host
qualification, including all 70 original machine-code/refusal comparisons.
Its nine existing positive host cases and 41 refusal cases still pass.

New native checks exercise real pipe ioctl output, guard bytes, nested calls,
five bad device ranges, shared file writes, private file copies, read-only file
maps, kernel errors, all 128 mapping slots, exhaustion, 1,024 slot-reuse cycles
and 14 invalid memory operations in isolated child processes. A native Omega
compiler-test driver verifies 16 exact compile refusals and zero emitted bytes.
No GPU device is opened by these tests.

The unchanged experiment tests also pass under this compiler: policy 106,
collector 125, report 299, independent oracle 12,801, and native process/pipe
integration 337. Their source is CUDApp b5eaf26f2bc590fe6b0d54a793b43827079ed624.

Eight actual compiler variants compiled the unchanged tests and were rejected:
remove generation matching, remove read-only enforcement, remove the final
mapped-range check, suppress generation advance, remove ioctl-size enforcement,
replace ioctl with getpid, replace munmap with close, reduce slot capacity to127.
`mutants.tsv` records actual nonzero test exits. The generation-advance variant
fails at its first invalid zero-generation handle. The other generation variant
exercises reuse of a previously valid handle. Source and executable hashes are
in `receipt.json`; raw local variant source and logs are retained for review.

Reproduction from this source checkout:

```sh
quiet-build sh seed/host/check.sh
```

The fresh-export check included `seed`, `tests/one_layer/omegese` and
`tools/exp445/arms/examples` and a local Git index, which the inherited fixture
enumerator requires. `host-check.log` confirms 70 actual original comparisons;
an empty fixture collection would not qualify this receipt.

The operations and test programs are handwritten Omega. Existing shell bootstrap
and orchestration, Node source-edit/hash orchestration for compiled mutants,
strace and readelf are disclosed development tools. They are not dependencies of
the emitted programs. The compiler ELF has two load segments, no interpreter and
no dynamic section. All native test gates ran through quiet-build.

This is host qualification of a proposed compiler extension. Independent review,
the existing compiler branch-range correction integration, native GPU launcher,
complete model/state attachment and full hardware comparison remain open.
No GPU/model timing, speedup, whole-experiment completion or issue closure is claimed.
