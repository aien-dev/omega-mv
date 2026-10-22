# Experimental native developer source preview

This complete source package supports experimental Linux ARM64 development.
It contains the reviewed Omega command, compatible `om` and `omg` aliases,
and the qualified seed bootstrap. Public preview users need no private staging
access. Native source remains owned by its original module; this directory is
an additive distribution and is outside Omega's production build.

This is an immutable branch preview. Canonical website publication, release
publication, default-branch integration and parent independent Pi5 public replay
remain pending. Issue #633 and end-to-end publication item L590 remain open.
No general platform, GPU, stable release or native runtime-attachment claim.

`SOURCE.json` records exact provenance, source tree, compiler hash and limits.
`SHA256SUMS` pins the complete archive. Its AGPLv3 license and imported-source
license are included unchanged. The archive holds 372 regular text source,
test, documentation and historical evidence files, with 39 directories.
No compiled binaries, weights, private repository history or staging branches
are included. The hexadecimal seed is committed textual bootstrap source.
Historical build traces contain local source paths; they are test evidence.

The existing installer, dispatcher and download/packaging boundary use shell
and OS utilities. Native checking, compiling and computation use seeded Omega.
Python and Node oracle/test files and outward C/Python examples are disclosed
source material, not installation or native runtime dependencies.

## Reproduce and verify

Original archive recipe, for the source owner with the pinned commit:

```sh
git archive --format=tar --prefix=omega/ 70459b34b8c4840c48c2b15f2edc4b2a7c39ea75 | gzip -n > omega-developer-linux-arm64-source.tar.gz
sha256sum -c SHA256SUMS
```

Public consumers can use this complete archive without that private checkout:

```sh
sha256sum -c SHA256SUMS
node tests/verify-source.mjs # optional verification oracle, not an install dependency
mkdir source-preview
tar -xzf omega-developer-linux-arm64-source.tar.gz -C source-preview
(cd source-preview/omega && sh tools/verify-sources.sh)
```

`evidence/members.sha256` records every regular member's original git mode,
SHA256 and relative name. The pre-publication audit reproduced the compressed
archive byte for byte, checked every blob against the declared commit tree,
and scanned all member names and bytes for credential/key/binary/weight and
conversation artifacts. No findings. Pattern scanning has finite coverage;
it is not a universal secret-detection guarantee. Original logs and design
notes were retained as part of the specifically authorized complete payload.

To build/install offline on Linux ARM64, create an isolated HOME and TMPDIR,
then run `source-preview/omega/tools/install.sh --offline --prefix /absolute/existing-parent/preview`.
The installer performs the qualified source seed chain and compiler fixpoint.
It requires standard host utilities including POSIX shell, coreutils, binutils
`readelf`, tar and gzip; the online boundary also requires curl and sha256sum.
Use `quiet-build` on the shared Spark and bound CPU concurrency to one.
Installation leaves PATH and shell startup files unchanged. Use full command
paths, and remove the installation with its `bin/omega uninstall` command.

## Reviewed evidence and remaining acceptance

The original implementation passed its full native suite, 7 alias checks,
61 path safety cases, and 5 real local HTTP download checks on each physical
Spark and Pi5. The independent source review is copied under `evidence/`.
These are inherited results, not reruns on this public preview.

The reviewed bootstrap is published with only its download URL changed to the
immutable public asset commit. Public payload and manifest readback matched
the staged bytes. The literal public curl route passed on Spark with isolated
HOME/TMPDIR, all three aliases checking/building/running a native project,
three command-collision refusals, wrong-hash refusal and uninstall. Complete
commands, raw output and exit status are under `evidence/`. See `PREVIEW.md`
for the immutable entry and parent replay instructions. Parent independent
review/fetch and Pi5 public replay remain pending before product publication.
