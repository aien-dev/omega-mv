#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Drake Stapleton (AIEN / aien-dev)
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# e1_manifest.sh -- E1 candidate-to-main artifact equivalence manifest.
#
#   tools/e1_manifest.sh --candidate SHA --main SHA [--physics-dir DIR] [--out FILE]
#
# The E1 chip campaign ran on one commit (the candidate); the squash merge is
# another (main). This tool does not claim the chip ran on main. It records,
# for every E1 artifact, the SHA-256 of the file's content at both commits
# (ABSENT when a commit lacks it), and whether they are equal. With
# --physics-dir it also rebuilds the four E1 chip binaries from the main tree
# with exactly the gcc lines of the gate scripts (named per entry) and records
# their SHA-256, so a reviewer can compare them with the binary digests the
# chip receipts carry. Read-only on git; writes only the manifest.
#
# Artifact classes:
#   chip   compiled into a chip binary or run as a chip gate script; must be equal
#   host   host-side build system or host tests; may differ only if the host
#          tier was rerun on main (tools/e1_combine.sh enforces this)
#
# Shell + git + coreutils + jq + gcc. No Python.
# Shared canonical consumer adapter. The stage guard owns authentication; this
# adapter only composes a disposable mutation tree, never a second authority.
# Extension proposal (source pin 6999fa9e): seeded .om has bounded byte I/O
# and process words but no regex rewrite primitive used by mutation_runner.
# Add `textedit input(view uchar N), size(ulong), pattern(view uchar P),
# replacement(view uchar R), output(edit uchar N) -> long`, with an explicit
# bounded POSIX-BRE subset and exact-one-match policy. Return output length;
# refuse malformed pattern (-22), zero/multiple matches (-61), overflow (-28),
# and unsupported backreferences (-95). No implicit shell or regex host call.
# Gate against retained sed/perl read-only oracles at recorded versions, with
# zero/two-match, bad-pattern, overflow and scanner read-error controls.
# Then implement a seeded .om conductor using fork/exec packed argv, directory
# traversal and SHA-256 units, preserving runner exit and baseline semantics.
# Until that lands, bash/git/coreutils/jq/gcc and runner sed/perl/awk remain
# explicit host dependencies, not an Omega implementation.
# Snapshot only authenticated canonical inputs for the chip receipt collectors.
# This mode never builds a binary, takes a GPU lock or opens a device.
if [ "${1:-}" = --canonical-evidence ]; then
    set -eu
    root=$(cd -P "$(dirname "$0")/.." && pwd)
    stage=${CUDAPP_STAGE:-${OUT_DIR:-build}/cudapp}
    case $stage in /*) ;; *) stage=$root/$stage;; esac
    metadata=$(sh "$root/tools/cudapp-stage.sh" --verify "$root/cudapp.lock" "$stage")
    [ -n "$metadata" ]
    source=$(cd -P "$stage/source" && pwd)
    target=${2:?evidence directory required}
    shift 2
    [ "$#" -gt 0 ]
    mkdir -p "$target"
    for input in "$@"; do
        case $input in src/*.[ch]) ;; *) echo "invalid canonical source: $input" >&2; exit 2;; esac
        case $input in */../*|*/./*) exit 2;; esac
        [ -s "$source/$input" ] && [ -f "$source/$input" ] || {
            echo "missing or empty canonical source: $input" >&2; exit 2;
        }
        cp -- "$source/$input" "$target/"
    done
    [ -s "$stage/source.tar" ]
    cp -- "$stage/source.tar" "$target/cudapp-source.tar"
    cp -- "$stage/pin" "$target/cudapp.pin"
    for oracle in bash git sha256sum gcc make; do
        "$oracle" --version > "$target/$oracle.version" 2>&1
        [ -s "$target/$oracle.version" ]
    done
    printf '%s\n' "$metadata" > "$target/CUDAPP-SOURCE.json"
    exit 0
fi
if [ "${1:-}" = --canonical-overlay ]; then
    set -eu
    root=$(cd -P "$(dirname "$0")/.." && pwd)
    target=${2:?missing disposable output directory}
    stage=${CUDAPP_STAGE:-${OUT_DIR:-build}/cudapp}
    case $stage in /*) ;; *) stage=$root/$stage;; esac
    metadata=$(sh "$root/tools/cudapp-stage.sh" --verify "$root/cudapp.lock" "$stage")
    source=$(cd -P "$stage/source" && pwd)
    [ -d "$source/src" ] && [ -s "$source/src/cudapp_om_units.c" ] || exit 2
    for required in omega_gpu_engine.c omega_gpu_engine.h omega_numeric.c omega_numeric.h \
        omega_numeric_reduce.c omega_blackwell_codegen.c omega_blackwell_codegen.h \
        omega_blackwell_encoder.c omega_blackwell_encoder.h omega_blackwell_matmul.c \
        omega_blackwell_matmul.h omega_blackwell_qmd.c omega_blackwell_qmd.h \
        omega_blackwell_engine.c omega_blackwell_engine.h omega_blackwell_submit.c \
        omega_blackwell_submit.h omega_blackwell_realize.c cudapp_om_units.c; do
        [ -s "$source/src/$required" ] || {
            echo "canonical source closure missing required member: $required" >&2; exit 2;
        }
    done
    mkdir -p "$target"
    cp -R "$source/src" "$target/"
    # Retained Omega CPU/adapters keep ownership; retired GPU paths no longer
    # exist here and therefore remain the authenticated canonical versions.
    cp -R "$root/src/." "$target/src/"
    # Canonical GPU translation units use the public SHA facade, not Omega's
    # generated CPU declaration header (whose native ABI guard is independent).
    # The unchanged Omega binding remains the linked SHA implementation.
    cp -- "$source/src/sha256.h" "$target/src/sha256.h"
    # Same rule for tests: the GPU twin tests were retired to CUDApp, so the
    # canonical tests come first and Omega's retained tests overlay them.
    cp -R "$source/tests" "$target/"
    cp -R "$root/tests/." "$target/tests/"
    # Capture each oracle's version as a checked command, not a command
    # substitution whose failure jq could otherwise mask.
    oracle_version() {
        local name=$1; shift
        "$@" > "$target/$name.version" 2>&1
        [ -s "$target/$name.version" ]
    }
    oracle_version bash bash --version
    oracle_version git git --version
    oracle_version coreutils sha256sum --version
    oracle_version jq jq --version
    oracle_version gcc gcc --version
    oracle_version sed sed --version
    oracle_version perl perl -v
    oracle_version awk awk -W version
    printf '%s\n' "$metadata" | jq -e '
        . + {host_dependencies: {
          bash: $bash, git: $git, coreutils: $coreutils, jq: $jq, gcc: $gcc,
          sed: $sed, perl: $perl, awk: $awk},
          omega_replacement: "bounded seeded .om conductor; proposal in tools/e1_manifest.sh"}
    ' --rawfile bash "$target/bash.version" --rawfile git "$target/git.version" \
      --rawfile coreutils "$target/coreutils.version" --rawfile jq "$target/jq.version" \
      --rawfile gcc "$target/gcc.version" --rawfile sed "$target/sed.version" \
      --rawfile perl "$target/perl.version" --rawfile awk "$target/awk.version" > "$target/cudapp-source.json"
    printf '%s\n' "$target"
    exit 0
fi
set -eu
set -o pipefail
HERE=$(cd -P "$(dirname "$0")/.." && pwd)
CAND= MAIN= PHYS= OUT=
while [ $# -gt 0 ]; do
    case $1 in
        --candidate) CAND=$2; shift 2;;
        --main) MAIN=$2; shift 2;;
        --physics-dir) PHYS=$2; shift 2;;
        --out) OUT=$2; shift 2;;
        *) echo "usage: $0 --candidate SHA --main SHA [--physics-dir DIR] [--out FILE]" >&2; exit 2;;
    esac
done
[[ $CAND =~ ^[0-9a-f]{40}$ && $MAIN =~ ^[0-9a-f]{40}$ ]] || { echo "candidates must be full 40-hex SHAs" >&2; exit 2; }
cd "$HERE" || exit 2
git cat-file -e "$CAND^{commit}" 2>/dev/null || { echo "unknown commit $CAND" >&2; exit 2; }
git cat-file -e "$MAIN^{commit}" 2>/dev/null || { echo "unknown commit $MAIN" >&2; exit 2; }
STAGE=${CUDAPP_STAGE:-${OUT_DIR:-build}/cudapp}
CANONICAL=$(sh tools/cudapp-stage.sh --verify cudapp.lock "$STAGE") || exit 2
SOURCE=$(cd -P "$STAGE/source" && pwd)
for revision in "$CAND" "$MAIN"; do
    git show "$revision:cudapp.lock" | cmp - "$STAGE/pin" ||
        { echo "comparison requires the exact staged CUDApp pin at both revisions" >&2; exit 2; }
done

# Chip-class patterns: sources of the four chip binaries (Gate 5, reduce, ldst,
# transc), the chip gate scripts and manifests, the physics pin. Host-class:
# the Makefile and host-only tests the host tier runs.
CHIP_RE='^(src/omega_numeric.*|src/omega_blackwell_(codegen|encoder|matmul|qmd)\.[ch]|src/forge_realization\.[ch]|src/aegis_verification\.[ch]|src/sha256\.[ch]|src/omega_program_fp32\.[ch]|src/omega_unwritten_trap.*|tests/test_omega_numeric.*|tests/test_omega_reduce\.c|tests/test_omega_ldst_gb10\.c|tests/test_omega_divsqrt_gb10\.c|tests/numeric_oracle\.h|tests/tensor_reduce_multi_tests\.inc|tests/numeric_native/.*|tests/run_numeric_gates\.sh|tests/run_reduce_chip\.sh|tests/chip_run_transc_receipt_allowlist\.txt|tools/chip_run\.sh|tools/run_numeric_ldst_chip\.sh|tools/run_numeric_transc_gate\.sh|tools/run_divsqrt_gate\.sh|tools/run_unwritten_trap\.sh|tools/ldst_nvdisasm_check\.sh|tools/divsqrt_nvdisasm_check\.sh|tools/manifests/.*|physics\.lock)$'
HOST_RE='^(Makefile|mk/numeric-.*\.mk|mk/program-fp32\.mk|tests/test_omega_transc\.c|tests/test_numeric_lifecycle\.c|tests/test_omega_unwritten_trap\.c|tests/test_chip_run.*\.sh|tools/numeric_check_sweep\.sh|tools/divsqrt_check_sweep\.sh)$'

sha_at() {
    if git cat-file -e "$1:$2" 2>/dev/null; then
        git show "$1:$2" | sha256sum | cut -c1-64
    else printf '%s\n' ABSENT; fi
}

files=$( (git ls-tree -r --name-only "$CAND"; git ls-tree -r --name-only "$MAIN") | sort -u | grep -E "$CHIP_RE|$HOST_RE")
[ -n "$files" ] || { echo "empty artifact read set" >&2; exit 2; }
entries= n_eq=0 n_ne=0 chip_ne=0
for f in $files; do
    a=$(sha_at "$CAND" "$f"); b=$(sha_at "$MAIN" "$f")
    if echo "$f" | grep -Eq "$CHIP_RE"; then cls=chip; else cls=host; fi
    if [ "$a" = "$b" ]; then eq=true; n_eq=$((n_eq+1))
    else
        eq=false; n_ne=$((n_ne+1))
        if [ "$cls" = chip ]; then chip_ne=$((chip_ne+1)); fi
    fi
    entries+="${entries:+,}{\"path\":\"$f\",\"class\":\"$cls\",\"sha256_candidate\":\"$a\",\"sha256_main\":\"$b\",\"equal\":$eq}"
done

# Rebuild the four chip binaries from the main tree (gcc lines copied from the
# gate scripts; the source list of each is what the chip ran).
bins=
if [ -n "$PHYS" ]; then
    [ -d "$PHYS/nvrm" ] || { echo "no physics tree at $PHYS" >&2; exit 2; }
    [ "$(git rev-parse HEAD)" = "$MAIN" ] || { echo "checkout must be at --main $MAIN to rebuild" >&2; exit 2; }
    [ -z "$(git status --porcelain --untracked-files=no)" ] || { echo "tracked files are modified; refusing to rebuild" >&2; exit 2; }
    NV=$SOURCE/third_party/nvidia-open-580.173.02
    INC=(-Isrc -I"$SOURCE/nvrm" -I"$SOURCE/m16" -I"$NV/src/common/sdk/nvidia/inc" -I"$NV/kernel-open/common/inc" -I"$NV/kernel-open/nvidia-uvm" -I"$NV/src/nvidia/arch/nvalloc/unix/include")
    FORGE="src/forge_realization.c src/aegis_verification.c src/omega_om_units.c $PHYS/forge/forge_descriptor.c $PHYS/forge/forge_realize.c $PHYS/sha256_clean.c $SOURCE/nvrm/nvrm.c $SOURCE/m16/m16_native.c"
    BLK="src/omega_blackwell_codegen.c src/omega_blackwell_encoder.c src/omega_blackwell_matmul.c src/omega_blackwell_qmd.c"
    TMPB=$(mktemp -d "${TMPDIR:-/tmp}/e1manifest.XXXXXX") || exit 2
    trap 'rm -rf "$TMPB"' EXIT
    build() { # NAME SOURCE_SCRIPT FLAGS... -- SOURCES...
        local name=$1 script=$2; shift 2; local flags=() sources=() s
        while [ "$1" != "--" ]; do flags+=("$1"); shift; done; shift
        for s in "$@"; do
            case $s in
                src/omega_blackwell_*.c|src/omega_numeric.c|src/omega_numeric_reduce.c|src/omega_core.c|src/omega_vector.c|src/omega_canonical.c)
                    [ -s "$SOURCE/$s" ] || return 2
                    sources+=("$SOURCE/$s");;
                *) sources+=("$s");;
            esac
        done
        sources+=("$SOURCE/src/cudapp_om_units.c")
        if gcc -std=gnu11 -O2 -Wall -Wextra -Werror -ffp-contract=off -I"$SOURCE/src" "${flags[@]}" "${INC[@]}" -o "$TMPB/$name" "${sources[@]}" > "$TMPB/$name.log" 2>&1; then
            s=$(sha256sum "$TMPB/$name" | cut -c1-64)
        else cat "$TMPB/$name.log" >&2; return 2; fi
        bins+="${bins:+,}{\"gate\":\"$name\",\"build_line_of\":\"$script\",\"sha256_main\":\"$s\"}"
    }
    # shellcheck disable=SC2086
    build GATE5 tests/run_numeric_gates.sh -I"$PHYS/forge" -- tests/test_omega_numeric.c src/omega_numeric.c src/omega_numeric_gb10.c src/omega_numeric_divsqrt_gb10.c src/omega_numeric_provenance.c $BLK $FORGE
    # shellcheck disable=SC2086
    build REDUCE tests/run_reduce_chip.sh -I"$PHYS/forge" -- tests/test_omega_reduce.c src/omega_numeric_reduce.c src/omega_numeric_reduce_gb10.c src/omega_numeric.c src/omega_numeric_gb10.c src/omega_numeric_divsqrt_gb10.c src/omega_numeric_provenance.c $BLK $FORGE
    # shellcheck disable=SC2086
    build LDST tools/run_numeric_ldst_chip.sh -pthread -- tests/test_omega_ldst_gb10.c src/omega_numeric_ldst_gb10.c src/omega_numeric_divsqrt_gb10.c src/omega_numeric.c src/omega_numeric_provenance.c src/omega_blackwell_encoder.c src/omega_blackwell_codegen.c src/omega_blackwell_matmul.c src/omega_blackwell_qmd.c src/omega_om_units.c "$SOURCE/nvrm/nvrm.c" "$SOURCE/m16/m16_native.c"
    # shellcheck disable=SC2086
    build TRANSC tools/chip_run.sh+tools/manifests/numeric_transc.chiprun -fno-fast-math -pthread -- tests/test_omega_numeric_transc_gb10.c src/omega_numeric_divsqrt_gb10.c src/omega_numeric_transc.c src/omega_numeric.c src/omega_numeric_provenance.c src/omega_blackwell_encoder.c src/omega_blackwell_codegen.c src/omega_blackwell_matmul.c src/omega_blackwell_qmd.c src/omega_om_units.c "$SOURCE/nvrm/nvrm.c" "$SOURCE/m16/m16_native.c"
fi

body="{\"schema\":\"AIEN_E1_EQUIVALENCE_MANIFEST_V1\",\"candidate_git_commit\":\"$CAND\",\"main_git_commit\":\"$MAIN\""
body+=",\"whole_tree_identical\":$([ "$(git rev-parse "$CAND^{tree}")" = "$(git rev-parse "$MAIN^{tree}")" ] && echo true || echo false)"
body+=",\"files_equal\":$n_eq,\"files_differ\":$n_ne,\"chip_files_differ\":$chip_ne,\"files\":[$entries]"
body+=",\"canonical_source\":$CANONICAL"
[ -z "$bins" ] || body+=",\"rebuilt_chip_binaries\":[$bins]"
body+=",\"timestamp_utc\":\"$(date -u +%Y-%m-%dT%H:%M:%SZ)\"}"
if [ -n "$OUT" ]; then printf '%s\n' "$body" | jq . > "$OUT" && echo "manifest: $OUT (equal=$n_eq differ=$n_ne chip_differ=$chip_ne)"
else printf '%s\n' "$body" | jq .; fi
