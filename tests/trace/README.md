# L489 host regression tests

All inputs are explicitly SYNTHETIC and contain no model traces. Run `tests/trace/run.sh`, which enters quiet-build and builds only the existing SearchTrace host verifier plus a C test sandbox. `make test-trace` is the optional convenience entry. Collection is never enabled by building or running a query.

The test oracle files ending `.c` are TEST HARNESS ONLY, never linked into Omega. network-deny installs a kernel seccomp policy denying socket operations before Node, including child processes and worker threads; its positive control must observe EPERM. Native ARGUS/DUAL fixtures are independently produced using the existing encoders, pinned by argus.lock and the Omega source tree. No native engine implementation is added.

The viewer tests execute the actual generated client script against an in-memory DOM fixture. They cover selection, time bounds, comparison labels and reset. They are not a real browser qualification. Browser layout, CPU/memory footprint, query scaling timings, instrumentation overhead, GB10/energy, native callback attachment and actual training remain NOT_RUN.

`mutants.mjs` copies only owned trace files to private temporary directories, changes one behavior, runs the behavioral tests and requires an assertion failure. Timeouts and import/syntax errors do not count as killed mutants. `artifacts.mjs` creates deterministic local review artifacts and refuses to replace frozen differing bytes. Run only when preparing a new explicitly named artifact revision.
