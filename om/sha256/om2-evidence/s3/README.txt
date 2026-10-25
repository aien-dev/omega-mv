S3 reduced-unit check (L838, 2026-10-25). Evidence class HOST (GB10 host CPU, AArch64 Linux), no GPU.
TEMPORARY, like s1/ and s2/: not the delivered unit; removed when the delivered unit compiles.

Compiler: aien-dev/om2/compiler-s3 @ ad220c05 (PR #754), omc sha256
  98cd1621ca4f69c5589641b3437e7e512838596fdfbf2f7775a48de19c284b16
  (CompilerLane's ~/workspace/om2-compiler/seed/out/omc; equals the announced pin; not re-born here)
Unit: sha256-s3.om (351 lines, built by gen.py from the delivered unit), sha256
  6d18716348c2b2181aa4e41cccd96c1172a6bea5fda0e667e40dcb417d2f9d02
  Byte for byte from the delivered unit (checked by diff): layout shactx (state uint 8, count scalar
  per E5, buf uchar 64), kinit, iv, rounds, finish. sched keeps `w edit uint 64 fresh` and its body;
  sched/compress take `m view uchar 128 o ulong` instead of `m view uchar * o ulong` (S4).
  sha256_init (no export) and sha256_final (o edit, not out) bodies verbatim; sha256_update takes
  (d view uchar 128, n) instead of len d. absorb is reduced (completes one pending block from at most
  64 new bytes; no blocks/run/part). Program mode: stdin in chunks 1, 64, 3, 55, 63, 56, 17 bytes with
  an empty update after each, digest as hex.
  Receipts in main, exit 3 on mismatch:
    E3 copy: a byte copy k of the live context c after the third chunk, k continued with every later
      chunk; final(k) must equal final(c).
    section 7 reuse: r = init, update(50 zero bytes) twice, final, then init again and fed every
      chunk alongside c; final(r) must equal final(c).
Output: s3.bin, sha256 8cf843bb78017d5678c7484c5db718fd68eebd68a57fb580ab8a421729daa6a0, ELF64
  AArch64; a second compile is byte-identical.

Results
  omc < sha256-s3.om > s3.bin                              exit 0
  python3 harness.py ./s3.bin                              vectors pass=158 fail=0 (vectors.out):
    every vector's digest correct AND both receipts held (exit 0) on all 158 messages
  Mutants (sed on a copy):
    copy loop 104 -> 32 bytes (copy drops count, buf)       pass=2 fail=156, all 156 exit 3
      (the 2 passing rows, checked: fips-empty and seq-0, both the empty message: count 0, nothing buffered)
    sha256_init without `put c count 0`                     pass=0 fail=158, all exit 3 (reuse receipt)
    sched loads 15 words instead of 16 (w[15] unwritten)    compile error 27, line 109
      omc-diag 1 code=27 sev=error line=109 col=17 ... obligation=init  (`y = get w add i 14`, i=1)
    field count at 31 (overlaps state)                      compile error 29, line 14 (the field line)
    field buf at 41 (past SIZE 104)                         compile error 29, line 15
    rotr e 11 -> rotr e 12                                  pass=0 fail=158 (digest only)
Code (objdump 2.42): s3.bin 22468 bytes, 5604 insns, ROR imm 576, LDR imm 360, LDR reg 92
  (s2 test unit: 17648 / 4399 / 576 / 336 / 65). Programs differ in main and absorb, so this is not
  a like-for-like size comparison; ROR count is unchanged.
Delivered unit under S3: "omc: line 114: error 16", now at col 44 (found `*`, the unsized view, S4);
  `fresh` on the same line is accepted.
