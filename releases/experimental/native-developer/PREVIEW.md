# Public preview entry and replay

Experimental Linux ARM64 only. This branch preview is publicly fetchable.
Canonical website, release and default-branch publication remain pending.

```sh
curl -fsSL https://raw.githubusercontent.com/aien-dev/omega-mv/fec281df6829b9b4efa069c02037c69e4e4ff679/releases/experimental/native-developer/install.sh | sh
```

The default prefix is `$HOME/.local`. Use the printed full command paths if
its bin directory is absent from PATH. The shell profile remains unchanged.
`omega`, `om` and `omg` resolve to one installed implementation. Existing
commands cause a refusal. Uninstall with `$HOME/.local/bin/omega uninstall`.

For parent independent replay, obtain `tests/public-preview.sh` from the final
evidence commit, read it, create a fresh output directory outside source
worktrees, and run `bash -x public-preview.sh /absolute/output-directory`.
On the shared Spark prefix that command with `quiet-build`, constrain execution
to one allowed CPU and set MAKEFLAGS=-j1. The harness creates isolated HOME and
TMPDIR, uses the literal public entry, compiles and executes a real native
project through all three aliases, checks three collisions and hash refusal,
and uninstalls its successful installation. It keeps logs/project executables
in the output directory for inspection. It never edits the invoking profile.
The hash-refusal case deliberately fetches public SOURCE.json as wrong payload
bytes, using the unchanged reviewed mirror override. All other payload fetches
use the bootstrap's default immutable asset URL.

## Exact pins

- Asset commit: `fd4c12224773253c063f732c7d9e64a5395104e1`
- Bootstrap commit: `fec281df6829b9b4efa069c02037c69e4e4ff679`
- Payload SHA256: `eaf37ff32d17166de403372069b52aeb98b9523ce154b8f065ae9e4c161c8480`
- Preview script SHA256: `e4d6707b4023f8db9ffbd15fe603e8c67523603ba7d1c13640d6f75256cc1a36`

Payload URL:
https://raw.githubusercontent.com/aien-dev/omega-mv/fd4c12224773253c063f732c7d9e64a5395104e1/releases/experimental/native-developer/omega-developer-linux-arm64-source.tar.gz

The archive retains its original reviewed bytes and AGPLv3 notices. The script
retains its original reviewed implementation with a single URL replacement;
`evidence/url-only.diff` records it. No private source history is needed.

## Evidence boundaries

Source: original independent review completed without findings; only the public
URL changed. Public archive closure reverified against all 372 blobs/modes and
the declared complete git tree. Corrupt archive and wrong-tree controls refused.

Spark public replay: PASS, curl/sh both zero; 3 alias project paths, 3 collision
refusals (73), checksum refusal (65), argument refusal (64), uninstall complete,
empty temporary directory and profile checksum unchanged. Native binary hashes
and AArch64 ELF headers were retained in the durable replay output. The original
full native/alias/path/HTTP suites are inherited, not rerun here.

Pi5 public preview replay: NOT_RUN by L614; parent independent gate pending.
Original Pi5 local HTTP 5/5 evidence is inherited and distinct.

Deployment: public immutable branch preview only. Stable release, main merge,
canonical-site deployment and final issue #633/L590 closure remain pending.
No extra reviewer agents were created; parent reviews this distribution change.
