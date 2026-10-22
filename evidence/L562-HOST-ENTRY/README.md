# L562: exact Make host gate entry preconditions

Parent-authored follow-on to L553 `3f9eee7178e0ba05ad847f81f4dd4d9e9f519c40`. This branch carries L553's four host fixes for568/344 plus a test-driver-only entry correction. It is not native-AIENOS/hardware/current-model qualification or a new language/bootstrap implementation.

## Actual failure and root cause

Parent reproduced the exact `quiet-build make OUT_DIR=PRIVATE test-osh-host` target with SIGINT default and SIGQUIT ignored at entry. No GNU timeout was inserted around the tested process, because timeout can reset signal dispositions. Actual Make exits2:1,559 checks pass and5 fail. The same five failures preserved by L553 are present: the default child-signal fixture sees inherited ignored QUIT, and four child FD-list fixtures see the caller's inherited build/quiet lock descriptors.

The execution library deliberately preserves an inherited SIGINT/QUIT ignore, and the suite already has explicit tests requiring that POSIX behavior. Resetting production behavior would be wrong. The initial test fixture also did not declare which caller descriptors belong in its child environment. Globally closing build fd8 would break the orphan-compiler publication exclusion previously qualified by L546, so no build helper or production adapter was changed.

## Correction and limits

Only the normal test-driver entry initializes its own signal/descriptor fixture, after the `--helper` dispatch:

- Establish default INT/QUIT before ordinary tests deliberately override them.
- Mark inherited nonstdio descriptors close-on-exec, without closing them. They remain open in the test driver, retaining the caller's lock lifetime; helper execs no longer see unrelated caller descriptors.
- Fail visibly on signal/fd setup failure. No setup refusal produces PASS.
- Bypass initialization entirely in `--helper` mode, preserving explicit ignored-signal and inherited-FD tests.

The existing test/assertion prefix is byte-identical to the exact L553 base. No assertion, case, skip, timeout, compiler warning, production source, Makefile or build-lock helper changed. The Linux/proc test harness already requires this host environment; no new runtime dependency is introduced.

## Actual verification

Under the same deliberately inherited ignored QUIT and actual Make/quiet locks, the **same public Make target now exits0**. Plain1,564/1,564 and ASan/UBSan1,564/1,564, both `OSH_HOST_PASS`, final `test-osh-host: PASS`.

Two meaningful compiled controls:

- Remove only default-QUIT setup: both plain/sanitizer binaries compile successfully; Make exits2 with exactly the original child-signal assertion, no unrelated failures.
- Remove only CLOEXEC marking: both binaries compile successfully; Make exits2 with exactly the four original FD-list assertions.

Fault controls are killed by the exact existing behavioral assertions, not compile errors, crashes or deadlines. `tests/osh/host/test_entry_controls.py` verifies original assertion-byte identity, successful ELF builds and exact failure lists. Full raw command output and original failing/new passing `host.out` files are losslessly gzip-compressed; hashes bind uncompressed bytes as well as packet files.

Actual parent command driver is retained as `reproduce.py`; it sets dispositions in the actual Make child's preexec, then invokes quiet-build/make without an alternate direct-binary workaround. The prior direct clean-stdio PASS remains historical L553 evidence; it is not substituted for this exact target result.

## Delivery

Independent review of this parent entry correction and the four inherited L553 source fixes is pending. L563 owns the separate prompt-read SIGINT lost-wakeup work and must not edit this fixture or the generic build helper. L396 owns the OSH extraction to omega-host: route/carry this patch and L553's four source fixes into that canonical module, rather than re-adding retired OSH to Omega after extraction. L531 receipt/allocator correction remains a required carry for old-base descendants.

All original L553 failure receipts remain immutable. This correction addresses only the exact host test gate's entry contract; it does not solve the separate interactive prompt race, qualify the aienos differential column, close568/344, or grant merge/whole-repository/hardware acceptance.
