#!/bin/sh
# Host orchestration only: compile and execute first-party Omega sources.
set -eu
LC_ALL=C; export LC_ALL
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo=$(CDPATH= cd -- "$here/../../../.." && pwd)
compiler=${L717_OMC:-/home/drakestapleton/handoffs/universal-adapter-L707/shared/omc}
expected=191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9
check_compiler() {
    read -r actual rest <<EOF
$(sha256sum "$compiler")
EOF
    [ "$actual" = "$expected" ] || { printf 'FAIL pinned compiler hash: %s\n' "$actual" >&2; exit 1; }
}
check_compiler
[ "$#" = 1 ] || { printf 'usage: run.sh fresh-output-directory\n' >&2; exit 64; }
out=$1
case $out in /*) ;; *) out=$repo/$out;; esac
[ ! -e "$out" ] || { printf 'FAIL output already exists: %s\n' "$out" >&2; exit 73; }
mkdir -p "$out"
cat "$here/buffers.om" "$here/tests/test_globals.om" "$here/tests/test_buffers.om" > "$out/candidate.om"
check_compiler
if "$compiler" < "$out/candidate.om" > "$out/candidate.elf" 2> "$out/candidate.compile.log"; then candidate_compile_rc=0; else candidate_compile_rc=$?; fi
[ "$candidate_compile_rc" = 0 ] && [ -s "$out/candidate.elf" ] || { printf 'FAIL candidate compile rc=%s\n' "$candidate_compile_rc"; exit 1; }
readelf -h "$out/candidate.elf" > "$out/candidate.elf-header.txt"
grep -q 'AArch64' "$out/candidate.elf-header.txt"
grep -q 'EXEC' "$out/candidate.elf-header.txt"
readelf -l "$out/candidate.elf" > "$out/candidate.program-headers.txt"
if grep -q 'INTERP' "$out/candidate.program-headers.txt"; then printf 'FAIL candidate is dynamically interpreted\n' >&2; exit 1; fi
chmod 700 "$out/candidate.elf"
if "$out/candidate.elf"; then candidate_run_rc=0; else candidate_run_rc=$?; fi
[ "$candidate_run_rc" = 0 ] || { printf 'FAIL candidate behavioral suite rc=%s\n' "$candidate_run_rc"; exit 1; }
printf 'candidate compile_rc=%s run_rc=%s elf_bytes=%s\n' "$candidate_compile_rc" "$candidate_run_rc" "$(wc -c < "$out/candidate.elf" | tr -d ' ')" > "$out/RESULT.txt"
for mutant in "$repo"/evidence/UNIVERSAL-ADAPTER/L717/mutants/*.om; do
    name=$(basename "$mutant" .om)
    cat "$mutant" "$here/tests/test_globals.om" "$here/tests/test_buffers.om" > "$out/$name.om"
    check_compiler
    if "$compiler" < "$out/$name.om" > "$out/$name.elf" 2> "$out/$name.compile.log"; then compile_rc=0; else compile_rc=$?; fi
    [ "$compile_rc" = 0 ] && [ -s "$out/$name.elf" ] || { printf 'FAIL mutant compile %s rc=%s\n' "$name" "$compile_rc"; exit 1; }
    readelf -h "$out/$name.elf" > "$out/$name.elf-header.txt"
    grep -q 'AArch64' "$out/$name.elf-header.txt"
    grep -q 'EXEC' "$out/$name.elf-header.txt"
    chmod 700 "$out/$name.elf"
    if "$out/$name.elf"; then run_rc=0; else run_rc=$?; fi
    [ "$run_rc" -ne 0 ] || { printf 'FAIL mutant escaped: %s\n' "$name"; exit 1; }
    printf '%s compile_rc=%s run_rc=%s caught=yes\n' "$name" "$compile_rc" "$run_rc" >> "$out/RESULT.txt"
done
sha256sum "$here/CONTRACT.md" "$here/buffers.om" "$here/run.sh" "$here/tests/test_globals.om" "$here/tests/test_buffers.om" "$repo"/evidence/UNIVERSAL-ADAPTER/L717/mutants/*.om "$out/candidate.om" "$out/candidate.elf" "$out"/mutant-*.om "$out"/mutant-*.elf "$compiler" > "$out/SHA256SUMS"
cat "$out/RESULT.txt"
printf 'PASS L717 Omega buffer suite and compiled mutants\n'
