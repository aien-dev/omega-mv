# Strict pinned-executable launch profile for Omega #670

Date: 2026-10-23. Parent tracker: #673. Branch: `fix/security673-qualified`.
Base: `9a2b6a5` plus the `execpure` change in this commit. Platform: Linux AArch64, native execution.

## Threat and protected assets

A launcher admits a native executable after a caller supplies only a path and argument vector. The
untrusted program may try to read or write inherited descriptors, open files outside its pinned
image, create sockets, fork, execute a different image, allocate unbounded memory, or run without a
time limit. The protected assets are the caller's descriptors and filesystem view, other processes,
and host availability.

This is a strict, fixed profile for one pinned executable. It is not a general sandbox for arbitrary
workloads and it does not add a second authority service.

## Enforcing code

The compiler adds one host word, `execpure` (code 135), lowered by `hpack` in `seed/omc/omc.om`.
After the existing packet validation and descriptor closure, `hpure` performs these operations at
the kernel boundary, in order:

1. `openat(AT_FDCWD, path, O_RDONLY|O_CLOEXEC|O_PATH)` pins the image as descriptor 3.
2. `landlock_create_ruleset` handles execute, read, write, directory, remove, make and refer rights.
3. `landlock_add_rule` allows only `EXECUTE|READ_FILE` beneath the pinned descriptor.
4. `prctl(PR_SET_NO_NEW_PRIVS, 1)` then `landlock_restrict_self`.
5. `prlimit64` sets CPU 1 s (hard 2 s), file size 1 MiB, stack 8 MiB, core 0, processes 0,
   descriptors 4 and address space 128 MiB.
6. `seccomp(SECCOMP_SET_MODE_FILTER)` installs a 27-instruction filter allowing only:
   `read` on descriptor 0, `write` on descriptors 1 and 2, `execveat` on descriptor 3 with
   `AT_EMPTY_PATH`, and `exit`/`exit_group`.
7. `execveat(3, "", argv, empty env, AT_EMPTY_PATH)` replaces the process.

Every failure before `execveat` returns the kernel's negative error; there is no fallback to a
less-restricted launch. The caller cannot select a weaker profile through this word.

## Tested properties

`pure-child.om` is the untrusted consumer. It asserts, in order:

| Attempt | Expected |
| --- | --- |
| read public stdin | works |
| read descriptor 3 (the pinned image) | EPERM |
| write descriptor 0 | EPERM |
| read descriptor 1 | EPERM |
| `open` private data | EPERM |
| `open` judge data for write/truncate | EPERM |
| create a socket | EPERM |
| fork | EPERM |
| execute a different image | denied |
| write public stdout | works |

The child exits 0 only if every assertion matches. `pure-raw.om` emits four fixed native images
that attempt `execveat` on an unassigned inode, raw `openat`, raw `getpid` and a 1 GiB `mmap`.
Each expects the exact kernel refusal and exits 0.

## Results

| Case | Observed |
| --- | --- |
| Compiler rebuild `om4 -> stage1 -> stage2` | stage1 == stage2 |
| `pure-launch` with `pure-child` | exit 0; stdout `public-output`; protected files unchanged |
| raw-0 absolute `execveat` | exit 0; kernel `EACCES` |
| raw-1 raw `openat` | exit 0; kernel `EPERM` |
| raw-2 raw `getpid` | exit 0; kernel `EPERM` |
| raw-3 1 GiB `mmap` | exit 0; kernel `EPERM` |
| Gate branch/packet/source | exit 0 |
| Native host suite | exit 0 |

Raw traces in `launch.strace` and `raw-0..3.strace` show the Landlock ruleset, rlimits, seccomp
filter, pinned `execveat`, and each refusal. `protected-before.sha256` and
`protected-after.sha256` match.

## Residual limits

- Fixed profile only. There is no per-request grant identity, generation, revocation fence,
  durable budget or caller-selected policy. Those remain #670/#649/#651 work.
- The pinned image is trusted to be the intended executable. The profile does not establish source
  correctness, a diverse compiler or absence of kernel/Landlock/seccomp defects.
- `RLIMIT_CPU` bounds CPU seconds, not wall time, output volume, descendant count or total system
  memory. `RLIMIT_NPROC=0` is the current process-control boundary; no process tree or cgroup is
  created.
- Landlock ABI support and the exact handled-access set are host assumptions. Unsupported kernels
  fail closed at the kernel call; no emulation was used.
- Only synthetic files and native fixtures were used. No device, GPU, network service or private
  data was touched. This does not close #670 or #673.

## Additive review correction

The descriptor3 used to pin the executable is CLOEXEC and closes on successful exec. Descriptors
0/1/2 remain caller-assigned capabilities. Seccomp blocks clone/fork; RLIMIT_NPROC is only an
additional limit and does not by itself isolate processes. A selected inode is not immutable content
identity. Setup failure may leave restrictions and transient descriptors installed in the child;
the supported caller exits without fallback. Pipe output and blocked-I/O wall deadlines require the
trusted supervisor. The later `pure1`/`pure4` evidence adds compiled missing-filter and missing-inode
controls, resource-boundary checks and missing-facility refusals. The original observations above
remain historical; they do not establish generation-bound grants or durable evaluation custody.
