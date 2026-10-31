# effects: capability manifest derived from Omega source

A seeded-Omega program reaches the kernel only through the closed keyword set in
[OMEGESE section 10](../../docs/one_layer/OMEGESE.md) and
[MAPPING.md](../host/MAPPING.md): there is no raw syscall and no address-to-pointer
conversion. The host words that appear in a unit are therefore a complete, finite list
of the OS effects it can have. This tool turns that list into a manifest, checks it
against the compiled binary, and emits a seccomp filter that enforces it.

```
seed/out/omc < seed/effects/effects.om > effects && chmod +x effects
effects src [allow=C,..] < unit.om      host words, classes, Linux syscalls; policy check
effects elf < binary                    syscalls the omc ELF can issue (fail closed)
effects verify SRC BIN [allow=C,..]     ELF syscalls must lie within the source manifest
effects bpf [launch] < unit.om          seccomp cBPF allow-list for bwrap --seccomp
effects table                           word, class and syscalls, one per line
```

Classes: `io` (read, write), `fd`, `path`, `net`, `proc`, `ident`, `time`, `env`,
`random`, `device`. `exit`/traps (syscall 93) are always allowed.

Exit status: 0 pass, 1 refused or undeclared syscall, 2 usage, 3 invalid or
unresolvable input, 4 read error.

`elf` treats every SVC in an executable load segment as reachable. Each SVC must be
immediately preceded by `MOVZ X8,#n`, no direct branch may land on it, and the text
may contain no `BR`/`BLR`. Anything else is `ELF UNRESOLVED` (exit 3). W+X segments
are refused. These rules hold for every program the pinned compiler emits today. A
binary from another producer will usually fail closed.

## Qualification

`sh seed/effects/check.sh` (pinned omc; strace, bwrap, od and dd as oracles):

- closure: every keyword with a host code in `seed/omc/genkw.awk` is in the table;
  `omc.om` has exactly 5 SVC emit sites (a new site means the table needs review)
- probe parity: one compiled probe per word; the ELF syscalls equal the table row
- self manifest, fixtures, policy refusals with word, class and line
- every program-mode unit in `seed/host/tests` and `seed/rung4/tests`, and omc itself:
  binary syscalls lie within the source manifest
- strace: syscalls observed at run time lie within the ELF set
- seccomp: each fixture runs under its own filter; a binary run under a narrower
  filter is killed (exit 159, SIGSYS)
- fail-closed controls: SVC without `MOVZ X8`, `MOVZ X9`, branch into an SVC, `BR`,
  W+X segment, non-ELF input
- tool mutants (dropped row, wrong syscall number, weakened MOVZ check) are killed

## Limits

- Classes and the filter work at syscall granularity. `fopen` is `path` whether it
  reads or writes; argument-level policy (literal open flags, socket families) is
  future work.
- `bpf launch` adds `execve` (221) so that bwrap can start the program. The program
  can therefore call execve itself under that filter. An in-program install would close
  this gap but needs a `prctl`/`seccomp` host word, which is a compiler change.
- The filter restricts syscalls only. The check runs bwrap with `--dev-bind / /`, so
  filesystem and namespace isolation are separate.
- The table is pinned to the compiler in `om/units.sh`. A compiler that changes how
  host words lower is caught by closure and probe parity, not silently accepted.
