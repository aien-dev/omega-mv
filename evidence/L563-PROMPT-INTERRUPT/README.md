# L563: interactive prompt SIGINT lost wakeups

Final host source: `268d909141be0968bacbda3cb63147658a0b55f9`.
Initial fix: `7569e8d2664ced00ec792ae25a0483e0cfe4f2e7`, superseded at the
terminal-readiness boundary. L553 baseline: `3f9eee7178e0ba05ad847f81f4dd4d9e9f519c40`.
Original L562 fixture correction `7177ad74cfb5215fa478c270f198af4e07cb33c5`
carried unchanged as `1151a8a9`; no manual fixture or build-helper edits.

Existing Linux host control only. The OSC lexer/parser/expander, compiler
sources, native/interpreter slices, root Makefile and build-lock helper are
unchanged. No new front end, language, compiler binary, model, inference engine,
GPU, driver, service or hardware qualification. Embedded unit-source bytes match
L553's previously built artifact; environment.txt pins both hashes. The existing
OSC library is linked by the unchanged build rules. Tests use actual existing
core backends; C is the replaceable host/testing role, not an Omega runtime claim.

## Frozen criterion and implementation

A delivered prompt interrupt must discard the pending list, set status130 and
print a fresh prompt before any new input or second signal rescues it. The next
command must retain its exact bytes/stdout/status. Unrelated EINTR must retry;
caller-blocked SIGINT remains blocked and pending. Caller signal actions, masks
and input open-file flags must survive return and repeated interactive embedding.
EOF is distinct from an input error. Fault, crash and generic deadline failures
are never mutant kills.

The old flag was examined only after read returned EINTR, then cleared at every
new list. A signal delivered before read could strand the shell and concatenate
interrupted text with the next command. The driver now blocks SIGINT across its
flag check and uses pselect with the original caller mask. It consumes an actual
interrupt instead of clearing a flag at each list. A signal pending during the
read is handled when the mask is restored.

A second RED found that terminal input can be flushed after pselect reports
readiness. Keeping SIGINT masked around an ordinary blocking read still loses
progress there. Each ready read now temporarily enables O_NONBLOCK, restores the
original flags before executing any command, and retries EAGAIN via the atomic
wait. The embedder must serialize access to that open-file description, as it
already serializes process-wide prompt signal actions. Interactive fd numbers
at/above FD_SETSIZE explicitly refuse with status2; no out-of-bounds fd_set use.
No new test wrapper/hook is present in the shipped binary.

## Actual proofs

All builds/tests use quiet-build, private output directories and normal actual
quiet/build lock lifetime. No performance measurements or timing claims.

- Final frozen harness against old3f9eee71: build0, run1. Legacy-only24cases,
  16fail: eight actual race wrong states across both backends at windows1/2/3/8;
  four newly introduced syscall fault-refusal expectations and four fd-boundary
  expectations separately classified. The no-pselect old source has no seam7;
  only that old-baseline leg explicitly excludes it and new13-15 controls.
- Final frozen harness against intermediate7569e8d2: build0, run1,32cases4fail:
  two EAGAIN wrong refusals and two real PTY post-readiness lost wakeups.
- Final source:32/32 plain and32/32 ASan/UBSan, all cases enabled. Prompt reset,
  continuation, partial byte and newline, atomic wait, input error, mask setup
  refusal, unrelated EINTR, caller-blocked/pending SIGINT, EBADF, high fd, EOF,
  EAGAIN, actual terminal flush and original nonblocking flags are covered.
  Every case also runs a second interactive invocation in the same child and
  checks the original input flags and signal mask. Backend calls are checked.
- Deterministic barriers link-wrap libc read and its fortified __read_chk form,
  fflush, pselect and sigprocmask only in test binaries. Signals are actual
  raise/kill. Parent withholds the next input until a fresh prompt appears.
  On RED it records /proc child syscall63 (read) or72 (pselect), then deliberately
  supplies rescue input and checks the precise wrong stdout/consumed byte.
  No poll deadline alone counts as a kill. Terminal window14 holds the child
  after real pselect readiness, uses actual tcflush and SIGINT, then releases it.
- Independent hook-free PTY oracle: actual /bin/bash5.2.21, final osh native and
  final osh interpreter each pass three real terminal Ctrl-C cycles, partial
  line and continuation discard, status130, next command stdout and terminal EOF.
  Actual Bash environment/version and complete terminal transcripts are pinned.
- Five successfully compiled behavioral mutants: discard handled flag8 wrong
  states; unmasked wait2 lost-progress states; commit interrupted newline2 wrong
  stdout states; hide select error4 wrong refusals; blocking read after readiness2
  lost-progress/consumed-first-byte states. Every expected window/backend is
  matched exactly by the checker; compiler/signal/watchdog errors reject verdicts.

## Regression and delivery limits

Exact final public Make gate is run under deliberately inherited ignored QUIT,
with real quiet fd9 and build fd8 retained. This uses the original published L562
fixture correction, not an alternate clean-stdio invocation. Host1564plain and
1564ASan, caller-action254plain and254ASan,104e2e cases/320checks,20trace scripts,
original differential66plain/64ASan +2original RLIMIT_AS NOT_RUN/21controls are
required and recorded in commands.json. Actual Bash exit-boundary36 comparisons
and original two caller-action compiled mutants95/2 are separate preserved gates.
Original L553 broad Make FAIL5 and alternate invocation results remain immutable
in their original packets; no historical result is relabeled.

Independent L563 review NOT_RUN. L568's independent inherited L553/L562 review is
separate and retains its own scope/verdict. Inherited L562 public manifest check
actually FAILS with two missing tracked build-path blobs; the parent owner must
repair its delivery packet separately. This receipt does not modify or waive
that failure. Source/gate success alone is not whole-branch acceptance.

L396 owns OSH extraction06299ff8 to omega-host. Existing canonical module clone
~/handoffs/teaching-private-L506/omega-host is clean e9fa9554, live HEAD verified,
with omega.lock5dd4ea38; historical c7596804 is not assumed current. The local
workspace/omega-host tree is L396's Omega modules/host branch, not that module
clone. Neither tree is changed. Route L553 PR572 carries/four fixes, L562 fixture
and these two L563 source commits to L396/desk together. Never re-add retired
OSH to canonical Omega. L531 carry, module integration and independent reviews
remain open. No issue, parent or other-owner ledger closure; no remote issue/PR
writes or merges. No new agents, existing session changes or479 validator work.
Native AIENOS/QEMU/hardware/current35B/shared train/full integration NOT_RUN;
energy UNMEASURED. All claims here are host functional observations.

Raw logs are losslessly compressed. lossless-raw-pins.json binds raw bytes and
compressed artifacts; source-pins.json/binary-pins.json/environment.txt bind inputs,
outputs and tools. Earlier development failures are retained and classified in
commands.json rather than silently promoted to acceptance evidence.
