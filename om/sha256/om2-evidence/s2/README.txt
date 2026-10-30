S2 reduced-unit check (L838, 2026-10-25). Evidence class HOST (GB10 host CPU, AArch64 Linux), no GPU.
TEMPORARY, like s1/: not the delivered unit; removed when the delivered unit compiles.

Compiler: aien-dev/om2/compiler-s2 @ 7fb62f3a (PR #753), omc sha256
  c23b33760f255bb4083a02da238ac802796bf94be23d9f4c4d2fc9bdb6e0b533
  (CompilerLane's ~/workspace/om2-compiler/seed/out/omc; equals the announced pin; not re-born here)
Unit: sha256-s2.om, sha256 5e39e0eb38306f528b70f26e8b87e5dcde9ce091c70de45b6bdc6bda97dfa329
  Built by gen.py from s1/sha256-s1.om: the four Sigma helper functions removed, and fn sched and
  fn rounds replaced by the delivered unit's bodies verbatim (one schedule step under
  `for i ulong 48 unroll`, the 16 loads under `for i ulong 16 unroll`, one round body under
  `for i ulong 64 unroll`). Changes to sched only: `b view uchar 64` for `fresh m view uchar *`,
  offset 0. Still reduced: no layout/fresh (S3), program mode instead of views/out/export (S4).
Output: sha-s2.bin, sha256 0b0c074d29986079b286d0b2d0bbf528de1de0b9eae14e6d828a97faaddf3e45;
  a second compile is byte-identical.

Commands and results
  omc < sha256-s2.om > sha-s2.bin                    exit 0
  python3 harness.py ./sha-s2.bin                    vectors pass=158 fail=0 (vectors.out)
  unroll removed (sed 's/ unroll$//') -> rolled.bin  vectors pass=158 fail=0 (section 4: removing
                                                     unroll leaves behaviour unchanged)
  mutant rotr a 13 -> rotr a 12                      vectors pass=0 fail=158

Code (GNU objdump 2.42 -D -b binary -m aarch64; whole program incl. main, finish, hexout, kinit, iv)
                     bytes  insns  ROR imm  RORV  LDR imm  LDR reg  STR imm  STR reg  BL  B.HS/CS
  S1 (sha-s1.bin)     4904   1213      10     0        0       27        0       93  17       31
  S2 (sha-s2.bin)    17648   4399     576     0      336       65      144       11  13       80
  S2 unroll removed   3988    984      10     0       16       11       80       13  13       31
  ROR 576 = 64 rounds x 6 + 48 schedule steps x 4: every rotr of the unrolled bodies is one ROR
  immediate. LDR imm 336 = the round loads get kt i / get w i (128) + the schedule's get w loads
  (192) + 16 elsewhere: constant indices into sized arrays, no run-time check.
  Not folded: the 16 big-endian loads read `get b q` / `get b add q 1..3` through the name q
  (q = mul i 4 is folded, but a name holding a constant is not treated as a constant index), so
  those 64 byte loads stay register-indexed with a run-time check (LDR reg 65, B.HS 80). In the
  delivered unit those reads are from an unsized view and are checked anyway (section 4).

Change to the delivered unit from this (same commit): the schedule step and both Sigma sums were
too long for 16 registers under S2 (error 12, rounds line `h = add h xor xor rotr e 6 rotr e 11
rotr e 25`); each Sigma is now two lines through t, and the schedule step two lines through x. The
delivered unit's sched and rounds are exactly what this test unit compiled.
Delivered unit under S2: "omc: line 114: error 16" (fresh, S3), as under S1.
