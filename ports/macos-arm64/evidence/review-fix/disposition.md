# L619 review disposition and L620 correction

Original independent review is preserved verbatim as independent-review-f43e6755.md,
SHA256 8df2113552666e3ec034100264d2cd7b8fdb166e0a1f5747070033e6c7cf1fe1.
Source reviewed: f43e6755d95a9783040b5ec2147ae519e9b01a11. No Critical or
Important findings; one Minor host-wrapper destination-type finding.
The review file exists and its hash was verified. A failed attempt to edit a
different L619-prefixed filename in the reviewer run did not modify this report.

The finding was independently reproduced before correction: four real target/
build directory and symlink-to-directory cases incorrectly returned0 instead
of73 and nested artifacts. Two normal-file checks passed. Original RED exit1
and full raw assertion log are retained. Neither a failed compiler nor an absent
fixture was counted as reproduction.

Fix: both wrappers explicitly refuse directory destinations with status73 and
use GNU mv -T -- to prevent directory-container semantics after preflight.
GNU Linux host-glue dependency is disclosed in CONTRACT.md. No Omega compiler,
adapter instructions or native runtime computation was changed.

Validation: all6 focused cases PASS; added to the combined qualification runner,
which now passes67checks (the original66 plus the six-case wrapper gate), with
10Linux native fixtures,5compiled controls and172hello-byte mutations intact.
Raw GREEN logs/exits retained. native-artifact-identity-result.json independently
matches all15unsigned Mach-O files, adapter.om and seed-built adapter binary to
the original parent physical-Mac replay inputs (17exact identities). Thus the
12-case physical-Mac receipt under evidence/parent-native remains applicable to
these unchanged target bytes; it was not fabricated as a rerun of changed code.
The combined Spark runner honestly continues to report macOS NOT_RUN locally.

The original review's finding is fixed and executed. A scoped independent review
of this host-wrapper/test delta remains required before L620/L615 acceptance.
Issue634 stays OPEN for full native platform/installer/ABI gates regardless.
No merge, release, deployment, shared compiler or model/service change occurred.
