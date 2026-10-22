# L519 #488 source-bound build-invalidation oracle

HOST TEST-CONTRACT + REVIEW PARTIAL. Existing production fixes are retained on
their existing branches; no Makefile production stamp implementation copied.
No driver code compiled, no real Physics checkout modified, no chip run.

New reusable `tests/physics_build_identity_test.sh MAKEFILE PRIVATE_OUTDIR
[identity|dirty]` creates two private synthetic Git repositories with source
mtimes forced to 2000-01-01, invokes the actual supplied Makefile's two driver
object recipes, and uses a compiler spy to count their execution. It exercises
real make dependencies rather than parsing the intended rule. The spy emits
explicit non-object markers and delegates compiler discovery to cc. No archive
or library is linked from its markers. The quiet checker is built with the real
compiler first. No lock check override or production gate weakening is used.
Private fixtures/logs remain under build/L519/physics-488/.

## Actual commands and results

All commands prefixed with quiet-build. Makefiles extracted byte-for-byte with
`git show REF:Makefile`, then supplied explicitly to the script:

| Exact source | Mode | Exit / result |
|---|---|---|
| baseline85d8343fa04647b198e3ddda00ad8e448837f9ca | identity | exit1 RED: directory switch leaves compile count2, expected4 |
| PR5468a4f15a6abedd3c21c17bf2f7f033d94fbab57ea | identity | exit0 GREEN: directory/revision switches rebuild both, unchanged/resolved alias does not |
| same PR546 | dirty | exit1 at dirty older-timestamp source: count6, expected8 |
| PR495a1896dcf68a899fe58ffbc32559fd4b3fa5bd4b3 | dirty | exit0: all9 assertions pass, final compile count10 |

Logs: red.log, green.log, pr546-dirty.log, pr495-dirty.log. Source Makefile SHA256
is printed in every log. The first spy version mistakenly counted compiler
introspection as a compile; initial-spy logs retain that invalid attempt. It
was fixed to record only -c recipe invocations. Neither production fix nor the
expected compile counts were weakened.

The identity mode covers initial build, unchanged tree, different resolved path
with older sources, new HEAD with unchanged source bytes, symlink alias of the
same resolved path, switching back, and nonexistent source refusal. The optional
dirty mode additionally changes tracked content without advancing HEAD, resets
its mtime backwards and verifies both invalidation and no redundant recompilation.

## Review finding and reuse recommendation

PR546 meets its declared path+HEAD boundary. Its dirty-content result is an
additional uncovered boundary, not a claim that its advertised path fix fails.
PR495 already fingerprints `git diff HEAD` and passes this extra boundary.
Desk should review/reuse the existing stronger PR495 implementation for this
case rather than open a competing stamp implementation. PR495 also proposes
header-consumer dependencies, but this oracle checks only the two direct driver
recipes. Header-consumer discovery, full clean build, concurrent stamp writes,
real object contents, and silicon acceptance remain NOT_RUN here.

Canonical motor inspected at1ad2dc1af70f01680fd93cb41a7a94fd0951723f uses
CUDAPP_DRIVER_DIR instead of the legacy PHYSICS_DIR driver rule. Do not apply
this old-rule result to canonical CUDApp wiring without a separate exact-source
contract. No canonical owner file was edited or process changed.

Independent review of L519's new test is NOT_RUN. This is L519's independent
execution of existing PR Makefiles, not independent review of its own code.
