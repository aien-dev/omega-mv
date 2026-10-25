Full unit under S4 (L838, 2026-10-25). Evidence class HOST (GB10 host CPU, AArch64 Linux), no GPU.
This is the author's run; it does not qualify the unit (L836 does).

Compiler: aien-dev/om2/compiler-s4 @ f6a6492fd (draft PR #764), omc sha256
  499236398aabc652e2dae27fe4bdc4c5db3d3726314356e0dbef23e3345803ec, born here from that commit by
  nice -n 19 sh seed/omega-from-seed.sh (rc 0, birth-f6a6492f.log); equals CompilerS4's binary
  /tmp/om2c/s4/omc-s4-f6a6492f, which was used for the runs below.
Unit: om/sha256/sha256-om2.om (344 lines), sha256
  dbf002dd6aa36585632b9d01d98ac3f0bd2fa0f0475082940de53638847994bb
  omc < sha256-om2.om: exit 0, 21340 bytes, sha256 27775fb2106dc0695d32417b1545af0027c136fe05bcdc4abd12cb473460570b,
  byte-identical on a second compile.
Bridge: aien-dev/omega-bridge feat/slice-abi @ 3bb8b8a (AbiLane: 1392549 plus tests/docs; src/native.om
  and tools/native unchanged; outputs here are byte-identical to 1392549's).
  OMEGA_NATIVE_OMC=<seed omc 734f0a39> OMEGA_NATIVE_OMC2=<S4 omc> tools/native OUT \
    om/sha256/sha256-om2.om om/blake3/b3unit.om                                          rc 0
  ident.txt: sha256_om2 abi=2 profile=OM-2 src=dbf002dd… omc=49923639…; b3unit abi=1 profile=OM-1.
  omega_api.h.generated: sha256_ctx {uint32_t state[8]; uint64_t count; uint8_t buf[64];} with
  static asserts (104; 0, 32, 40), SHA256_DIGEST_SIZE/BLOCK_SIZE, static inline sha256_init/update/
  final/hash -> omega2_sha256_* with abort() on a nonzero status. Field `buf` (src/sha256.h says
  `buffer`; no caller touches fields, AUDIT-L838.txt item 5).
  sha256: omega_native.c 21b58846…, omega_api.h 79c7de9c…, libomega_native.so 0f8d029c…

Harness t.c (oracle side, outside the closure): cc 13.3.0 -std=gnu11 -O2 -Wall -Wextra -Werror,
  omega_native.c + t.c + the historical C (src/sha256.c @ a647b6a9, sha a8fb8861…, renamed orc_*).
  python3 harness.py ./t vec        vectors pass=158 fail=0 (each vector also through random update
                                    splits 0..4096 bytes; exit 4 if they differ)
  ./t receipts                      all PASS (receipts.out):
    lengths 0..9000, hash and split update == C
    2000 random lengths < 20000, hash and split update == C
    E3 (2) hash(d,32,d) iterated 100x == C; hash(d,64,d) (digest over the first 32 bytes) == C
    E3 (1) byte copy of a live context after k = 0..299 updates of 7 bytes, both continued, == C
    section 7: init/update/final after a final == fresh context
    section 7: hash(n) == init/update(0,NULL)/update(n)/update(0)/final, n = 0..299 (empty updates)
    omega2_ident_sha256_om2() names unit sha256_om2, profile OM-2
  ./t big                           PASS update of 2^32+4094 bytes (after 3) == C;
                                    PASS hash of 2^32+4097 bytes == C   (50.9 s wall, both arms)
    These go through blocks/run: 128 runs of 2^25 bytes plus the rest. The top level (2^44 bytes)
    is exercised only by the scaled desk check (deskcheck/RESULT.txt).

Code (objdump 2.42 on the unit's code only): 21340 bytes, 5335 insns, ROR immediate 576 (64 x 6 +
  48 x 4), RORV 0, LDR immediate 376, LDR register 87, SVC 0 (section 8: no system calls).

Compile-time refusals of the full unit (sed on a copy):
  schedule loads 15 words (w[15] unwritten)   omc: line 127: error 27  (fresh, obligation=init)
  field count at 31 (overlaps state)          omc: line 25: error 29   (the field line)
  put c count 1 ... (index on scalar field)   omc: line 287: error 5   (E8)
  blocks without part (whole d)               compiles (part is not required by the types; kept for
                                              the 1e6-pass loop limit)
Change made for S4 (see AUDIT-L838.txt item 7): absorb's tail copy exceeded 16 registers (error 12 at
  line 268 @ 4c8aa0ff7, reported by CompilerS4); the copy into the buffer is now fn tobuf, used by
  absorb and by sha256_update's short path; absorb reads n = len d instead of a parameter.
NOT_RUN here: runtime mutants of the full unit, trace-equivalence scan, paired timing; L836 owns them.
