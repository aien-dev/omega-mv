# Independent L601 host exit review

Read L601-review.md and L601-review.json for the exact combined source recommendation and limits. This packet adds review evidence only. Original L577/L592 evidence remains immutable.

Commands actually executed on the review host:

```
node evidence/L577-EXIT-NUMERIC-PRIORITY/verify_packet.mjs HEAD
node evidence/L592-EXIT-ARGUMENT-COMPLETION/verify_packet.mjs HEAD
quiet-build node evidence/L601-EXIT-REVIEW/review.mjs
quiet-build node evidence/L601-EXIT-REVIEW/direct.mjs
quiet-build node evidence/L601-EXIT-REVIEW/author_audit.mjs
node evidence/L601-EXIT-REVIEW/exact_sets.mjs
quiet-build node evidence/L601-EXIT-REVIEW/finding_repro.mjs
quiet-build node evidence/L601-EXIT-REVIEW/minimize.mjs
node evidence/L601-EXIT-REVIEW/collect.mjs
```

The execution drivers require fresh private outputs at targets/L601. The independent review.mjs run correctly exits1:1212PASS/12FAIL per build, reflecting the published preexisting lifecycle finding. It was not retried to turn the result green. Source, binary and tool pins bind the tested candidate and direct-probe builds. The full raw records include exact commands/status/stdout/stderr, generated workload scripts, source delta, recomputed author assertions, selected actual replay controls, and historical setup failures. Each top-level execution log is separate. No compiler error, crash, sanitizer failure or watchdog termination is a behavioral kill. Original broad Make results are inspected pinned author qualification, not an independent rerun. Timing and energy are unmeasured.

raw.tar.gz excludes ELF binaries and private journal/key fixture state. Its members are individually SHA256/length bound in raw-members.json; verify_packet.mjs reads all payloads and raw members from staged or committed Git objects. To check publication use `node evidence/L601-EXIT-REVIEW/verify_packet.mjs --staged`, then `node evidence/L601-EXIT-REVIEW/verify_packet.mjs HEAD` after commit. Candidate source pins are checked at exact be37dcca. Retained own outputs move intact from this worktree's targets/L601 to /home/drakestapleton/workspace/targets/L601 after completion; recorded paths retain their actual execution spelling.

Parent L487/desk L488 owns disposition and L570 canonical carry. This review grants no merge approval or whole issue closure. Known help behavior, unsupported grammar and native/hardware/current35B limits remain explicit in the report.
