#!/bin/sh
# #488 host-only dependency oracle over an exact Makefile.
# A compiler spy records recipe executions; it does not build driver code.
# All fixture repos/objects stay in the caller's private output directory.
set -eu
makefile=${1:?usage: physics_build_identity_test.sh MAKEFILE PRIVATE_OUTDIR}
out=${2:?private output directory required}
mode=${3:-identity}
case "$mode" in identity|dirty) ;; *) echo 'mode must be identity or dirty' >&2; exit 2;; esac
makefile=$(realpath "$makefile")
mkdir -p "$out"
root=$(mktemp -d "$(realpath "$out")/run.XXXXXX")
printf 'FIXTURE_ROOT=%s\nMAKEFILE_SHA256=' "$root"
sha256sum "$makefile" | cut -d' ' -f1
phy_a=$root/physics-a; phy_b=$root/physics-b; objects=$root/objects
mkdir -p "$objects"
for path in "$phy_a" "$phy_b"; do
    mkdir -p "$path/m16" "$path/nvrm"
    git -C "$path" -c core.hooksPath=/dev/null init -q
    printf '/* synthetic driver recipe dependency, never compiled */\n' > "$path/m16/m16_native.c"
    printf '/* synthetic driver recipe dependency, never compiled */\n' > "$path/nvrm/nvrm.c"
    git -C "$path" add m16/m16_native.c nvrm/nvrm.c
    git -C "$path" -c user.name=Fixture -c user.email=fixture@invalid -c commit.gpgsign=false \
        -c core.hooksPath=/dev/null commit -qm fixture
    touch -t 200001010000 "$path/m16/m16_native.c" "$path/nvrm/nvrm.c"
done
cat > "$root/record-compile" <<'SPY'
#!/bin/sh
set -eu
compile=0
for argument in "$@"; do [ "$argument" != '-c' ] || compile=1; done
# Makefile compiler discovery (for example -dumpmachine) is not a compile.
[ "$compile" = 1 ] || exec cc "$@"
source_file=; target_file=
while [ "$#" -gt 0 ]; do
    case "$1" in
        -c) shift; source_file=$1;;
        -o) shift; target_file=$1;;
    esac
    shift
done
[ -n "$source_file" ] && [ -n "$target_file" ]
printf '%s\n' "$(realpath "$source_file")" >> "$L519_COMPILE_LOG"
printf 'SYNTHETIC recipe marker, not an object file\n%s\n' "$(realpath "$source_file")" > "$target_file"
SPY
chmod +x "$root/record-compile"
L519_COMPILE_LOG=$root/compile.log; export L519_COMPILE_LOG
: > "$L519_COMPILE_LOG"
# The real quiet checker must exist before CC is replaced with the recipe spy.
make --no-print-directory -f "$makefile" OUT_DIR="$objects" quietlock > "$root/quietlock.log" 2>&1
run_build() {
    make --no-print-directory -f "$makefile" OUT_DIR="$objects" PHYSICS_DIR="$1" \
        CC="$root/record-compile" "$objects/m16_native.o" "$objects/nvrm.o" >> "$root/make.log" 2>&1
}
assert_count() {
    actual=$(wc -l < "$L519_COMPILE_LOG" | tr -d ' ')
    if [ "$actual" != "$1" ]; then
        printf 'FAIL %s: expected=%s actual=%s\n' "$2" "$1" "$actual"; exit 1
    fi
    printf 'PASS %s: compile_recipes=%s\n' "$2" "$actual"
}
run_build "$phy_a"; assert_count 2 initial_build
run_build "$phy_a"; assert_count 2 unchanged_no_rebuild
run_build "$phy_b"; assert_count 4 changed_directory_older_sources
# Same directory and same source bytes, different Git revision must invalidate.
git -C "$phy_b" -c user.name=Fixture -c user.email=fixture@invalid -c commit.gpgsign=false \
    -c core.hooksPath=/dev/null commit --allow-empty -qm new-revision
run_build "$phy_b"; assert_count 6 changed_revision_older_sources
ln -s "$phy_b" "$root/alias"
run_build "$root/alias"; assert_count 6 same_resolved_directory_no_rebuild
expected_count=8
if [ "$mode" = dirty ]; then
    # Extra content boundary beyond PR546's declared path+HEAD guarantee.
    printf '/* uncommitted older-timestamp edit */\n' >> "$phy_b/m16/m16_native.c"
    touch -t 200001010000 "$phy_b/m16/m16_native.c"
    run_build "$phy_b"; assert_count 8 dirty_content_older_timestamp
    run_build "$phy_b"; assert_count 8 unchanged_dirty_no_rebuild
    expected_count=10
fi
run_build "$phy_a"; assert_count "$expected_count" switch_back_rebuilds
# Missing source must fail instead of claiming a successful build.
if run_build "$root/nonexistent"; then echo 'FAIL missing Physics path accepted'; exit 1; fi
assert_count "$expected_count" missing_path_no_compilation
printf 'PHYSICS_BUILD_IDENTITY_PASS (recipe invalidation only; driver/chip NOT_RUN)\n'
