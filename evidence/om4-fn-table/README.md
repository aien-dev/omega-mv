# om4 function table: 512 -> 4096 (PROPOSED prototype)

Host: Linux AArch64 (DGX Spark), 2026-10-31, worktree on `origin/main` `57a4135d` plus this change.
om4 before: `d0da40e45f1fcc45d9a39c9c52c9d745b545e076970729e51c12d76ef519efab` (14,224 bytes).
om4 after: `bfc1cfd197348c398be19425b21704ad5ea79d28e33c0e1eff676142f47812b7` (14,224 bytes).

- `gate4.log`, `om4.sha256`: `sh seed/rung4/gate4.sh` with the new om4 (GATE4 PASS, 220 cases,
  including section 7b and its capacity control). The committed `evidence/SEED-0/rung4/` files are
  left as they were.
- `probe.sh`, `probe.out`: `sh evidence/om4-fn-table/probe.sh`. Old om4 (`git show 57a4135d:seed/rung4/om4.bin`), new om4
  and `seed/out/omc` on `omc.om` plus k one-line functions and on function-mode chains and a cycle;
  every accepted output of the new om4 is compared byte for byte with omc's.

Not in this directory (run, output not kept): `sh seed/omega-from-seed.sh` RC=0 with
`seed/out/omc` `be8c5130…448a`, the same as before the change; `seed/omc/gate5.sh` GATE5 PASS;
gates 6, 7, 8, 17 and gate-ir RC=0; `sh seed/host/check.sh` RC=0; `sh seed/challenge/run.sh`
CHALLENGE PASS (it builds the pinned `369dbc96`, so it does not exercise this om4).
