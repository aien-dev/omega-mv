# Packed-list hostile ingress on main's compiler (#671)

Date: 2026-10-30 (UTC). Host: DGX Spark, Linux 7.0.0-1019-nvidia aarch64, CPUs 15-19
under `nice -n 19 ionice -c3`. Base: aien-dev/omega-mv main
`62d1d0d668f897386a0e311374f8dbcc5c32dde8` plus the files added in this commit.
Compiler: `seed/out/omc` sha256
`62775d2a391cfef7cde64e387655caa9809a267093112910ec76bfa568f3712b` (unchanged;
`seed/omega-from-seed.sh` inside `check.sh` rebuilt it to the same digest).

Ported from commit `9a2b6a5` on `fix/security673-qualified` (packet corpus modes 0-8
and the execprog consumer), extended with the boundary cases below and wired into
`seed/host/check.sh` through the OM-2 conductor instead of the unwired `gate669.om`.

## Commands

```sh
make build/omegese/omegese build/omega_emit/omega_emit build/checker2/checker2
TMPDIR=/dev/shm taskset -c 15-19 nice -n 19 ionice -c3 sh seed/host/check.sh   # exit 0
# controls below, with GATE built by: seed/out/omc < seed/qualification/om2/gate.om
GATE seed/out/omc $(sha256sum seed/out/omc | cut -d' ' -f1) evidence/SECURITY-673/packet-main1/negative-controls.cases SCRATCH  # exit 1
GATE seed/out/omc $(sha256sum seed/out/omc | cut -d' ' -f1) evidence/SECURITY-673/packet-main1/reclen-equivalent.cases SCRATCH  # exit 0
```

## Results

| File | Expected | Observed |
| --- | --- | --- |
| `check-packets.log` | 60 rows PASS: 16 fixture identities, 4 valid packets exit 0 (echo prints `packed list`), 12 malformed packets exit 103, prefixes 0..26 of the echo packet exit 103, prefix 27 exit 0 | `OM2GATE PASS cases=60 pass=60 fail=0 invalid=0 not_run=0` |
| `check-packets-mutant.log` | each mutant compiler executes the packet only its check refuses | `mutant-size`, `mutant-count`, `mutant-exact`: `FAIL run exit=0 expected=103`; `OM2GATE FAIL cases=3 pass=0 fail=3` |
| `check-tail.log` | whole host qualification passes | `PASS native-host qualification` |
| `negative-controls.log` | the conductor rejects a wrong fixture, a wrong accept, a wrong refusal and a one-byte-short prefix | 4 of 4 FAIL, exit 1 |
| `reclen-equivalent.log` | see below | 21 of 21 PASS, exit 0 (mutant survives) |

No malformed packet was accepted by main's compiler, so this campaign found no
defect to fix on main.

## Record-length check: not detectable by exit status

`mut-reclen-*.txt` removes `cmp x12, x5; b.hi` (declared record length above the
remaining bytes). The mutant still traps 3 on every over-declared record tried:
three corpus packets (modes 3, 7 and 15) and 18 truncated prefixes of the echo
packet. The
decoder then copies past the slice end, and either the zero-byte check stops at the
first zero it reads or the exact-consumption check traps after the remaining count
wraps. The check bounds how far the decoder reads; exit status and output cannot
show its removal, and the OM-2 conductor counts a signal as INVALID, never a kill.
This mutant is therefore not counted as detected.

`omega-seed.log` holds the summary lines of `bash tests/gates/run.sh seed`
(OMEGA-SEED, exit 0, no SKIP, GATE5 PASS), which includes this campaign.

## Not run

- x86-64 decoder `xhpack`: not exercised.
- Other #671 ingress paths (renames, argread paths, manifests, receipts, RPC, model
  metadata, trace and device parsers): NOT_RUN here.
