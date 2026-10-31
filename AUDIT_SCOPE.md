# Audit scope

Written for an outside, independent security audit. Revision described:
`bf54b19fa590e8ae925c0e80815516dbf41e7326` on `main`, 2026-10-28. Everything below is taken from
the repository's own files at that revision; items marked NOT_RUN were not executed while writing
this document.

## What this repository is

Omega is a systems language and compute platform being implemented in handwritten, seeded `.om`.
The seeded compiler `seed/out/omc` is rebuilt from a 408-byte hand-written loader
(`seed0 -> hex1 -> asm2 -> om4 -> omc`); its qualified profile is AArch64 Linux, static ELF. An x86-64 Linux backend (`target x86linux`,
#923) is merged as DRAFT: emulated execution only, native x86 qualification NOT_RUN
(`docs/X86-LINUX.md`).
The default branch still holds substantial C (about 600 tracked files under `src/`: the legacy
OSC compiler, the reaction runtime, GB10 numerics), which the README calls unfinished migration.
Status: research grade, staged Alpha qualification in progress. `OMEGA_SYSTEMS_CORE_CODE_AUDIT.md`
is an earlier internal code audit marked PROPOSED and partly superseded; it is history, not a
current security review.

## What to review, in priority order

1. The seed chain: `seed/rung0/seed0.hex`, `seed/rung0/load0.sh` (the one outside step, POSIX
   `printf`), rungs 1 to 4, `seed/omc/omc.om`, `seed/omega-from-seed.sh` (rebuilds omc twice to
   identical bytes), and the committed `seed/out/omc` (sha256
   `62775d2a391cfef7cde64e387655caa9809a267093112910ec76bfa568f3712b`).
2. Native host words (ids 100..165) and device and mapping words (ids 166..177) in
   `docs/one_layer/OMEGESE.md` and `seed/host/`: what effects compiled programs can perform.
3. Seeded libraries that parse input: `seed/json`, `seed/net` (`http.om`), `seed/wasm`,
   `seed/spirv`, `seed/sm121`.
4. C code on `main`: `src/compiler/`, `src/runtime/` (reaction runtime, capability and receipt
   paths, `rx_argus.c` hooks), `src/fabric/`, numerics. Memory safety and parser handling.
5. `om/` units (`om/sha256`, `om/blake3`) and the generated `src/omega_om_units.*` from
   omega-bridge at `om/omega-bridge.lock`.
6. `tests/gates/run.sh` and the `.gate` manifests; `.aien/always`, `.aien/unmapped-ok`,
   `.aien/verify.pub`.
7. `.github/workflows/aien-verification.yml` (pull request and push, `contents: read`, advisory),
   `evidence-immutable.yml` (pull request, fails on any modification of existing evidence),
   `om-units-arm64.yml` (hosted ARM64 rerun of the om-units gate, advisory). This branch adds
   `sbom.yml` (SPDX SBOM artifact, read-only token). `irspirv-intel-cpu.yml` (advisory replay of
   the irspirv SPIR-V suite on Intel's CPU OpenCL runtime) landed on `main` after the described
   revision; this branch pins its actions by commit sha like the others.
8. `tools/` test scripts and C helpers.

AGENTS.md: a head that changes `.aien/`, `tests/gates/`, `.github/workflows/`, `.forgejo/` or
`tools/aien-test/` is `BLOCKED_OPERATOR` and needs an operator run on the Spark. `evidence/` is
immutable: never modify, rename or delete an existing file there.

## Trust boundaries

- Untrusted: `.om` source given to `omc`; input to seeded parsers and network code; pull request
  heads (run by the Spark judge inside its sandbox).
- Trusted: the seed hex and loader; the committed `omc` digest; `.aien/verify.pub` on `main`;
  sibling checkouts at the commits in `physics.lock`, `argus.lock`, `aienos.lock` and
  `om/omega-bridge.lock`; the DGX Spark that signs bundles.
- GitHub checks are advisory; the DGX Spark is the test judge (`docs/PIPELINE.md`).

## Threat model summary

- Assets: compiler integrity (a trusted seed path with no C), correctness of emitted machine
  code, receipt integrity, the build host.
- Attackers considered: a malicious pull request (including one that rewrites evidence or
  gates); hostile `.om` input; hostile data to seeded parsers; a tampered committed binary.
- Defended: byte-identical self-rebuild and recorded digest; evidence additions-only check in CI
  and as a Spark gate; BLOCKED_OPERATOR rule for trust paths; zero-disk-secrets gate; signed
  bundles checked against `.aien/verify.pub` from `main`.
- Not defended / explicitly out of scope: the fixpoint is a same-lineage rebuild check, not
  diverse double compilation (README); host and emulator passes are not hardware qualification;
  the reaction runtime ladder R1 to R16 is not requalified on the current candidate.

## Build from source at a pin

AArch64 Linux with `gcc`, `make` and `sh`. Some gates need sibling checkouts (`../physics` at
`physics.lock` `048e5f5e881b092733baaf65d0ace46abda7f908`, `../aienos`, `../aien-architecture`,
`../omega-bridge` at `431fff0a95f84c0a8bf9d3383f94d7e1f02d913e`); a missing sibling exits 3.
`crumb.lock`: `d0f0b95d344b4c6628c3d868346932fd247ce168`.

```sh
git clone https://github.com/aien-dev/omega-mv.git omega
cd omega
git checkout bf54b19fa590e8ae925c0e80815516dbf41e7326
sh seed/omega-from-seed.sh
sha256sum seed/out/omc
```

Expected digest: the value in `seed/out/omc.sha256`.

## Run every test

```sh
tests/gates/run.sh seed
tests/gates/run.sh evidence-immutable
tests/gates/run.sh zero-disk-secrets
tests/gates/run.sh crumb-verify
tests/gates/run.sh om-units
make PHYSICS_DIR=/nonexistent PHYSICS_LOCK_CHECK=0 test-compiler-quick
make PHYSICS_DIR=/nonexistent PHYSICS_LOCK_CHECK=0 test-numeric-cpu
make PHYSICS_DIR=/nonexistent PHYSICS_LOCK_CHECK=0 test-realize
```

- Every gate name in `tests/gates/*.gate` is a `run.sh` argument; the Spark runs the touched
  gates plus `.aien/always` (`CRUMB-VERIFY`, `OMEGA-EVIDENCE-IMMUTABLE`, `OMEGA-ZERO-DISK-SECRETS`).
- `evidence-immutable` prints `evidence/: additions only`; `zero-disk-secrets` prints
  `Zero plaintext .env files verified.`
- Some older milestone gate scripts rewrite their receipts in place; run on a throwaway checkout.
- Gates that touch the GB10 (`*-silicon`, `gpu-*`, chip campaigns) need the DGX Spark in an
  authorized window: NOT_RUN. All commands above: NOT_RUN while writing this document.

## Known limitations and NOT_RUN

- C remains on the default branch (`docs/migration/C-TO-OM-INVENTORY.md`).
- R15 and R16 recorded FAIL at requalification; the R16 receipt `22d7a79a` is invalid as a
  current claim (README).
- `aien-verification.yml` sets `REPO_SLUG: aien-dev/omega` while this repository is
  `aien-dev/omega-mv`; whether GitHub redirects this is unknown from the repository.
- Hardware results are tied to the exact commit in each receipt.

## Dependencies and provenance

- No outside step on the seed path except POSIX `printf` (`seed/rung0/load0.sh`).
- Legacy build: gcc and make on AArch64. Sibling repositories pinned by lock files above.
- Committed binaries: `seed/out/omc`; binary blobs and a static library under `evidence/` are
  recorded artifacts, not build inputs.
- Licence: Apache-2.0 (`LICENSE`, `NOTICE`).

## Evidence

- `evidence/`: about 255 content-addressed receipt directories (`<name>.<sha256>.json`), additions
  only, enforced by `evidence-immutable.yml` and the `evidence-immutable` gate.
- Signed Spark bundles: `aien-dev/aien-receipts` at `omega/<sha>/bundle.json` (`docs/PIPELINE.md`).
  Receipts are append-only and never edited.
