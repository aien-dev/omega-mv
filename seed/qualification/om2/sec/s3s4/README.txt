seed/qualification/om2/sec/s3s4: OM-2 contract sections 3 and 4 (#748, ledger L836, lane QualPhaseB.FixRotUnroll)
Written from the contract alone (spec/omegese-om2.md, frozen sha 245bb74e...; errata E1-E7), OMEGESE.md 9 and
the baseline sources at main acb3f15. No candidate source or branch was read.

FILES
  candidate.cases     205 cases for a candidate OM-2 compiler; each block cites its contract lines / errata
  pinned.cases        145 cases that run now under 734f0a39 (OM-2 refusals exact + OM-1 twins/controls)
  mutant-kills.cases  kill specifications (a)-(d); QualPhaseB inserts the subst/cc lines per mutant
  lib-pu.om           decimal printer (OM-1 words); program fixtures are compiled as FIXTURE+lib-pu.om
  exp/                expectations taken from contract text: diag-site*.err (exact omc-site records, E7),
                      unroll-k65-diag.err (exact human line + omc-diag record), *.tok (words a diagnostic
                      line must contain, gate tok: matcher)
  <name>.om           OM-2 fixture (first non-note line om2 / om2 diag)
  <name>-rolled.om, <name>-plain.om   the same OM-2 unit with unroll / inline removed (section 4 lines 91, 97)
  <name>-om1.om       OM-1 twin for the pinned compiler (om2 line made a note; rotations as shifts; ce-view-om1
                      uses sized views); the twins were derived by these literal edits and reviewed
  Oracles: ../../oracle/s3s4 (python3 3.12.3; PROVENANCE.txt there).

FIXTURES BY PROPERTY (contract line)
  rot-values        s3 l.76-83 values, 4 widths, n = 0 1 mid w-1, constant and run-time n, run-time sweep 0..w-1
  rot-trap A-P      s3 l.79-81 trap 7 (exit 107): const n = w, run-time n = w, run-time n > w, const n > w; P control
  rot-signed-*      s3 l.78 signed x error 7;  rot-mixed-*  l.76-77, E7 n of x's type, error 7;  rot-result-type l.77
  unroll-sum        s4 l.87-92 K = 1, 4, 64 (uint, uchar, ulong), both forms against one oracle
  unroll-nest       nested 8 x 8 unroll, folded constant indices in range (l.105)
  unroll-nest-64x2  E7: 64-copy limit per loop, 64 x 2 nested (128 copies) accepted and runs
  unroll-write-i(-nested)  l.89 error 7;  unroll-break / unroll-continue  l.90 error 9 (rolled controls run)
  unroll-break-nested / unroll-continue-nested  E7: exit of a rolled loop nested in an unrolled body allowed
  unroll-k0 / unroll-k65(-diag) / unroll-kvar  l.87, E7: K = 0 or 65 error 8, non-number K error 16
  unroll-growth(-diag)  l.109-111 65536-word bound, error 14 obligation=growth
  inline-order A-I  l.93-97, l.102-104: argument order, trap order and codes, early ret, single evaluation
  inline-depth4/5/6 l.111, E7: 4 levels run, 5 and 6 levels error 14;  inline-new / inline-rec-* l.94 error 9
  fold-guard        l.102-104 guarded div/rem by i compile and behave as before (also in an inline body)
  fold-trap A-I     l.102-104 trapping constants stay run-time traps 2/1/5/7 (unrolled and inlined bodies)
  ce-sized(-oob, -oob-fold)  l.105-106 constant index into a SIZED array: in range runs, outside error 21
  ce-view A-F       l.106-108, s6 l.170, E2: every unsized-view index keeps trap 3 (unrolled, inlined, plain)
  slots-*           l.99-101 function mode slots; error 9 at the signature
  regs(-diag)       l.111-112 error 12 obligation=regs
  diag-site*        l.114-116, E7 omc-site records (iter=- / 0-based / dotted path); om2 diag artifact == om2 (same)

PINNED-COMPILER EXPECTATIONS (734f0a39)
  Not every OM-2 fixture is refused at its om2 line: omc's names pass reads every fn signature before any
  code (OMEGESE 9.2), so a fixture with an OM-2 signature is refused at the FIRST such fn line:
  `view T *` and `out` give error 16, `to T inline` gives error 5. Otherwise the om2 / om2 diag line is
  refused with error 16. pinned_rule.py encodes this; pinned.cases uses err:LINE:CODE per fixture.
  Result: OM2GATE PASS cases=145 pass=145 (evidence/phaseB/s3s4-pinned.log).
  candidate.cases dry run under 734f0a39: no INVALID and no malformed line (63 OM-1 cases pass, OM-2 cases
  fail as expected, 3 NOT_RUN); this only checks syntax, not a candidate.

OBSERVATIONS UNDER 734f0a39 (used for controls, recorded here)
  O1 OM-1 shifts accept an amount of another type (shr x n with x uint, n ulong compiles). E7 confirms the
     stricter same-type rule applies to OM-2 rotr/rotl only; the mixed cases expect error 7.
  O2 OM-1 accepts a write to the counter of a rolled for, and the for's increment still applies (1, 3).
  O3 OM-1 refuses an op whose operands are all bare numbers (div 1 0) with error 11; fixtures write div int 1 0.
  O4 The unroll-growth body is 30 words per copy under OM-1 (compiled size of 2 copies minus 1 copy:
     600 - 480 bytes), so 64 x 64 copies is about 122880 words > 65536, about 1.9 x the bound.

ASSUMPTIONS AND AMBIGUITIES (contract line, evidence, proposed fix)
  A1 Error lines: a refusal is reported at the line holding the offending statement or signature (OM-1
     convention): write to i, break/continue, error 21 at the get/put, error 9 at the fn signature. The
     contract never states the line of a section 4 refusal. Where the line is less certain (new / recursion
     in an inline function, growth, depth, regs) the case uses refuse (code only) or tok: (words only).
     E7 K errors are expected at the for line (the line of the K token, E4 span).
  A2-A6 RESOLVED by erratum E7 (2026-10-25) and converted to exact cases: K = 0 / 65 error 8 and a
     non-number K error 16 (unroll-k0, unroll-k65, unroll-kvar); break/continue of a nested rolled loop
     allowed (unroll-break-nested exit 4, unroll-continue-nested exit 8); inline depth counted from a
     non-inlined call site as 1 (inline-depth5 error 14); 64-copy limit per loop (unroll-nest-64x2 runs);
     omc-site iter = 0-based dotted path of enclosing unrolled counters in emission order
     (diag-site-unroll: iter=0 1 2; diag-site-nest: -, 0.0, 0.1, 0 for the rolled loop, 1.0, 1.1, 1).
     The unroll-k65-diag record is exact: col 13, span 289-291 (the token 65), found=3635, expect=number
     (E6 Q2), obligation=- (error 8 is not a growth obligation), sev=error.
  A6b "after the artifact on stderr" is read as: on stderr after compilation; stdout is only the artifact.
     Exact site records are expected only for function-mode units whose only trap sites are unsigned divs
     by a run-time divisor (no stack trap in function mode, s6 l.202). Each record is one LF-terminated line.
  A7 s6 l.172-176 define part of a view or edit; ce-view passes part of a LOCAL sized array to a view
     parameter, assumed allowed (OMEGESE 9.4: a local array may be passed as view). If a candidate refuses
     it, ce-view A/C/D/E/F fail at compile time; B (E2 sized argument) still checks trap 3.
  A8 E4 fixes col/span/found/expect for parse errors but not which token a growth/regs error points at, so
     growth/regs records are checked word by word: code=, sev=error (s1 l.43-46), obligation=, profile=OM-2.
  A9 slots-out6 writes only o[0] of an out parameter; the contract does not say whether an out must be fully
     written (s6 l.196-198). slots-* are compile-only: slot ORDER (pointer then length) needs the bridge.
  A10 Rotation of bool/f32/f64 is not covered (l.76-78 lists only unsigned and signed integers).
  A11 l.91-92 "compiles both forms of every unrolled fixture and compares their runs" cannot apply to a
     fixture refused only in its unrolled form (ce-sized-oob: error 21 unrolled, trap 3 rolled). Proposed:
     restrict the both-forms rule to accepted units.
  A12 l.98 "calls to non-inline functions stay calls" is not observable through run/emit: NOT_RUN.

NOT_RUN (candidate.cases)
  inline-calls-stay-calls (A12), slots-order (A9), program-site-table (program-mode site tables depend on
  OM-1 trap stubs and checks, stack and write slices, that neither the contract nor E7 enumerates).
  The `rotr`/`rotl` names in OM-1 units (s0 l.15-16) belong to another lane.

MUTANT KILL SPECS (mutant-kills.cases; predicted stdout in oracle/s3s4/exp/mut, byte in exp/mutants.txt)
  (a) unroll K-1 / K+1 copies: kill-unroll-sum, exit 0, stdout differs at byte 1 (K-1) / byte 5 (K+1),
      line 1 (the K = 1 loop); every line differs. A K+1 copy writes a[64] of a 65-element array: no refusal.
  (b) rotate: n+1 amount differs at byte 0 (constant path) / 29 (run-time path); w-n at byte 4 / 33;
      ROR for ROL at byte 18 / 47 (kill-rot-values, exit 0). Range check dropped: kill-rot-trap-A..O,
      exit 0 with a printed value where the oracle is empty (byte 0).
  (c) unsized-view check eliminated: kill-ce-view-A/C (unrolled, inlined) exit 0, differs at byte 14
      (prints 11111 from neighbour b[4]); D (plain constant get v 6) byte 0 (prints 1000000); F (part b 3 4)
      byte 26 (prints 11111000 from b[7]). B is not a clean witness (reads outside a 4-element array).
      A mutant that refuses the view index with error 21 fails candidate run cases ce-view-A/C/D at compile.
  (d) trapping constant folded: kill-fold-trap-A..I, exit 0, first diff bytes A 4, B 22, C 8, D 40, E 11,
      F 0, G 4, H 4, I 3. A mutant that refuses instead fails candidate run cases fold-trap-* / fold-guard
      with "compile exit=<code> expected=0".
