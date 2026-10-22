# L553 #344 command-string exit boundary

Source3695dd594868d853d4ed6521ae8f6d30f2d4f09c. Existing host driver only;
no lexer/parser/expander unit or language change. Existing PR5015c769ac5 fixes
bash-reference environment separately, and remains its owner's proposal.
Module routing remains L396/omega-host; no sibling edits or new authority.

Independent oracle: actual /bin/bash5.2.21 under explicit PATH,LC_ALL=C,TZ=UTC
and no inherited startup files. Both stdout bytes and exit status checked.
Criterion before tests: exit-too-many stops entire -c string1; stdin/script
abort current list but continue next line. Valid7 and invalid-numeric2 exits
stop all forms, ordinary continuation remains exact. Diagnostic categories
checked without pretending bash/osh diagnostic prefixes are identical.

Commands from own tree, all build/test operations under quiet-build:

- `node tests/osh/e2e/exit_boundary_controls.mjs build/L553/baseline/osh/osh build/L553/exit-red`: exit1,32pass4fail; both Omega modes wrongly print next and return0 for command strings. Actual18bash invocations per leg and36Omega comparisons in raw case JSON.
- `make -s -j2 OUT_DIR=build/L553/exit-fixed osh`: exit0.
- Same script with exit-fixed binary / fresh exit-green: exit0,36/36PASS.
- `node tests/osh/e2e/exit_boundary_mutants.mjs`: exit0. Both controls compile. Continuing command strings fails exactly4known assertions; stopping all input modes fails exactly8script/stdin assertions. No build failure/signal/deadline/other assertion counted.
- `make -s -j2 OUT_DIR=/tmp/L553-osh-regression.DAxToC test-osh-e2e osh-trace test-osh-host`: actual exit2. E2E104cases/320checksPASS, trace20scriptsPASS. Original host suite1559pass/5fail; raw unchanged assertions see inherited fd8buildlock/fd9quietlock in four descriptor checks and inheritedignoredSIGQUIT in one signal check. The failed public Make-target result is preserved, never relabelled green. No host-suite source is changed by M3; test source and binaries pinned.
- Build existing host sanitizer binary (same private OUT_DIR) exit0. Run exact plain and sanitizer binaries once each with clean stdio inheritance and explicit default signals:
  `quiet-build env --default-signal=INT,QUIT node -e 'const r=require("node:child_process").spawnSync(process.argv[1],[],{stdio:"inherit"}); if(r.error||r.signal)process.exit(99);process.exit(r.status);' /tmp/L553-osh-regression.DAxToC/osh-host/test_osh_host`
  and corresponding `_asan`: each exit0,1564/1564PASS. Wrapper's quiet lock remains held in parent; child descriptor preconditions are clean. No assertion, sanitizer, gate or lock helper weakened. This is a documented alternate invocation, not evidence the original Make target passed.

Prompt handler restoration and prompt SIGINT-read race remain separate open
#344 items at this milestone. Independent review, native AIENOS/QEMU,
hardware/current35B, full integration NOT_RUN. No timing/energy claim.
Inherited L531 carry and parent reviews remain dependencies; no issue closure.
