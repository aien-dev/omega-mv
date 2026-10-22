# L562 append-only publication correction

Independent review15a33b9c accepted the four L553 source deltas and parent test fixture implementation, but found two checksum-declared control outputs missing from Git. Their nested `build/` names were ignored even though private working-tree checksums passed.

This commit explicitly tracks those two exact preexisting gzip payloads. Their declared hashes already match; no receipt is synthesized, original manifest rewritten, failure removed, source/assertion changed or ignore rule disabled. Both old working-tree copies and the original raw files remain intact.

`git-closure.json` records actual failure against7177ad74 and complete staged Git-object payload/hash resolution. Final published Git-head closure is also checked after commit and remote ref readback. Git-object verification cannot be fooled by ignored private files. Existing source/test/helper/Makefile blobs remain exactly those independently accepted by15a33b9c; no new execution qualification or Bash-equivalence claim follows.

The separately reported preexisting nonnumeric-plus-extra-arguments exit defect is real, not fixed by evidence publication, and requires a distinct scoped corrective worker. Canonical omega-host carry, integration/native/hardware gates and issue closure remain open.
