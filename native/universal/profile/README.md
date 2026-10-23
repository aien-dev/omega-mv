# Read-only universal application preflight (L715)

Tracks [architecture298](https://github.com/aien-dev/aien-architecture-mv/issues/298) and [Omega642](https://github.com/aien-dev/omega-mv/issues/642), under [parent295](https://github.com/aien-dev/aien-architecture-mv/issues/295). This is an opt-in native Omega **plan-only** module, not a new public command, installer, resolver authority or launch path. Neither issue is closed by it.

`core.om` validates the fixed versioned UAP1 declaration. `cli.om` emits a deterministic JSON explanation only after validation. `unit.om` contains native direct behavior and input-preservation tests. `fixture.om` emits the valid bounded test record. [CONTRACT.md](CONTRACT.md) freezes bytes, refusal order, target labels and authority boundaries. The serialized JSON plan data in `cli.om` is fixed explanatory data; hashes declared by a caller never authenticate themselves.

## Build and exercise using the existing installer

Linux/AArch64, isolated user-owned directory. Set `module` to this directory in a frozen source checkout. The archive below is an existing public **experimental** distribution, not a new stable release. Verify its hash before extraction. Internet is used only for this explicit acquisition, not by the application.

```sh
module="$PWD/native/universal/profile"
work=$(mktemp -d "$HOME/omega-uap1.XXXXXXXX")
chmod 700 "$work"
cd "$work"
curl -fL https://raw.githubusercontent.com/aien-dev/omega-mv/fd4c12224773253c063f732c7d9e64a5395104e1/releases/experimental/native-developer/omega-developer-linux-arm64-source.tar.gz -o public.tar.gz
printf '%s  %s\n' eaf37ff32d17166de403372069b52aeb98b9523ce154b8f065ae9e4c161c8480 public.tar.gz | sha256sum -c -
tar -xzf public.tar.gz
mkdir -m 700 home tmp project
export HOME="$work/home" TMPDIR="$work/tmp"
# On the shared Spark, prefix this installer with quiet-build.
omega/tools/install.sh --offline --prefix "$work/prefix"
cd project
cat "$module/core.om" "$module/unit.om" > unit.om
cat "$module/core.om" "$module/fixture.om" > fixture.om
cat "$module/core.om" "$module/cli.om" > plan.om
"$work/prefix/bin/omega" check plan.om
"$work/prefix/bin/om" build unit.om -o unit
./unit
"$work/prefix/bin/om" build fixture.om -o fixture
./fixture > valid.uap
"$work/prefix/bin/omg" run plan.om < valid.uap
# Actual existing native computation, including refusal and recovery:
printf 'eval 1/3+1/6\neval 1/0\ndiff x^2\n' | "$work/prefix/bin/omega" math
# Transport/contract refusal, expected64 and empty stdout:
"$work/prefix/bin/omega" run plan.om < /dev/null
```

The first script run intentionally ends with a nonzero refusal. `omega`, `om` and `omg` remain identical installed paths. Source checking and computation use the source-born Omega compiler; shell, GNU file/hash utilities and Linux services are explicitly retained. The profile command consumes standard input, not unsupported program arguments. Execution is not a sandbox.

The installed package's public `check/build/run` still performs its native ELF preflight. This module does not authorize replacing that validation with a successful profile return code. Run plans are untrusted declarations until actual files, target and evidence authority are revalidated. Dynamic runtime compatibility, native-Aienux, GPU, arbitrary CPU and FFI profiles refuse or remain unimplemented.

## Rollback and cleanup

No hooks, PATH edits, services or real account enrollment are made. Stop invoking `plan.om` to opt out. From the same private run:

```sh
"$work/prefix/bin/omg" uninstall
# Keep project source, outputs and logs for audit. The installer removes only
# its checked owned payload; do not recursively delete a user-supplied prefix.
```

Modified installation files intentionally make uninstall refuse instead of deleting unknown data. Restore the exact installed owned bytes before retrying; never erase an installation to conceal a refusal.

## Verification scope

Initial behavior RED: valid compiled stub failed599 of1204 direct assertions (exit1); implementation then returned0/1204, exit0. Both builds were nonempty static AArch64 ELFs. Three compiled mutant variants fail the direct gate: ignored compiler pin (64 assertion failures), erased missing-runtime category (2), and caller-input corruption (301). The label `allow-missing-runtime` in the retained raw filename identifies a category-erasure mutant: its actual change returns73 instead of69, not success. This distinction is intentional and its exact source is retained.

Initial setup errors are retained separately: an invalid bool cast (compiler9) and an unsized array parameter (compiler16). They are not counted as behavioral RED or killed mutants. The pinned compiler requires fixed array parameter extents; the final implementation uses module-owned fixed buffers and fixed text data.

Actual installed `omega check`, `om build`, `omg run` returned0; the installed-built plan was byte-identical to the direct pinned-compiler build. Closed stdin and `/dev/full` each returned77. Empty input returned64 with no plan. Original math stream produced `OK 1/2`, `REFUSED 2`, `OK 2*x`; one-shot refusal returned65. Existing `discover` returned UNSUPPORTED69, not an invented registration result.

See `evidence/UNIVERSAL-ADAPTER/L715/` for source-bound raw logs and later replay/review dispositions. Do not interpret a local test or an explanation's target field as hardware qualification. No performance claim is made for this module.

## Remaining integration gates

- Final committed-source replay and fresh independent source review.
- The live main at base `a8ffd2b912c84197d08f963b05f9c7d8b30a3f8a` has no seeded package or `native/` tree; the existing public distribution is on its release owner's branch. Normal judge enrollment of this new native path must use that canonical package/artifact dependency, not an invented compiler or skipped mapping. Renda owns PR/judge integration; L590/L696 own distribution. No gate definitions or trust anchors are weakened here.
- Installed qualified-adapter registry, transactional lifecycle, receipt authentication, launch binding/cache invalidation, native component attachment and command-surface integration remain open under the linked programme issues.
- The first application still uses the original published stream driver. L716's hosted service facade is a separately qualified adapter with a different source identity; it cannot reuse this original driver's hash silently.
