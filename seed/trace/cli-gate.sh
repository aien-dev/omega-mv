#!/bin/sh
# seed/trace/cli-gate.sh: omega-mv #610 offline trace CLI.
# Run from the repository root: sh seed/trace/cli-gate.sh
# Checks seed/out/omc against seed/out/omc.sha256, compiles tracegen and tracecli,
# regenerates fixtures and pins their SHA-256, runs cli.cases against cli-golden,
# refuses a mode-0000 segment, and requires four source mutants to fail their own
# checks. The read-only mutant (ropen flags 32768 -> 32770) is caught by a literal
# scan of the exact return line in tracecli.om. An O_RDWR open of a mode-0400 file
# is not a reliable refusal for the owner, and verify uses segment.om's own open,
# so a functional open check would not catch that mutant.
set -eu
cd "$(dirname "$0")/../.."

bad() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }

sha256sum -c seed/out/omc.sha256 >/dev/null || bad "omc sha256"
[ "$(id -u)" != 0 ] || bad "mode-0000 check is meaningless as root"

ROPEN='ret fopen tpath 0 get tlen 0 32768 0'
ROPEN_MUT='ret fopen tpath 0 get tlen 0 32770 0'
scan_ropen() {
  c=$(grep -c -x -F "$ROPEN" "$1" || true)
  [ "$c" = 1 ]
}
scan_ropen seed/trace/tracecli.om || bad "ropen flags constant is not 32768 exactly once"
if grep -q -x -F "$ROPEN_MUT" seed/trace/tracecli.om; then
  bad "ropen flags 32770 present in the good source"
fi
[ "$(grep -c -x 'ret 3' seed/trace/tracecli.om || true)" = 1 ] || bad "partial site not unique"
[ "$(grep -c -x 'ret same' seed/trace/tracecli.om || true)" = 1 ] || bad "same site not unique"
[ "$(grep -c -x -F 'if and ne get qtype 0 65536 ne t get qtype 0' seed/trace/tracecli.om || true)" = 1 ] || bad "qtype site not unique"

if [ -n "${OUT_DIR:-}" ]; then
  B="$(pwd)/$OUT_DIR/trace-cli"
else
  B="$(pwd)/build/seed/trace-cli"
fi
mkdir -p "$B"
compile() {
  cat seed/trace/segment.om seed/trace/tsha.om "$1" | seed/out/omc > "$2"
  [ "$(od -An -tx1 -N4 "$2" | tr -d ' ')" = 7f454c46 ] || bad "compile did not emit ELF: $1"
  chmod 700 "$2"
}
compile seed/trace/tracegen.om "$B/tracegen"
compile seed/trace/tracecli.om "$B/tracecli"

ROOT=$(pwd)
D=$(mktemp -d "${TMPDIR:-/tmp}/trace-cli.XXXXXX")
cleanup() {
  chmod -R u+rwx "$D" 2>/dev/null || true
  rm -rf "$D"
}
trap cleanup EXIT

( cd "$D" && "$B/tracegen" ) || bad "tracegen failed"
if ! ( cd "$D" && sha256sum -c "$ROOT/seed/trace/fixtures.sha256" > "$D/sha.out" ); then
  cat "$D/sha.out" >&2
  bad "fixture sha"
fi
[ "$(readlink "$D/fx/link.seg")" = a.seg ] || bad "link target"
for f in a b c crc torn magic; do
  m=$(stat -c %a "$D/fx/$f.seg")
  [ "$m" = 400 ] || bad "fixture $f mode $m want 400"
done

n=0
while IFS= read -r line || [ -n "$line" ]; do
  case "$line" in ''|\#*) continue ;; esac
  set -f
  # shellcheck disable=SC2086
  set -- $line
  set +f
  name=$1
  want=$2
  shift 2
  out="$D/case.out"
  err="$D/case.err"
  set +e
  ( cd "$D" && "$B/tracecli" "$@" ) > "$out" 2>"$err"
  rc=$?
  set -e
  [ ! -s "$err" ] || { cat "$err" >&2; bad "case $name wrote stderr"; }
  cmp -s "$out" "seed/trace/cli-golden/$name.txt" || bad "case $name stdout"
  [ "$rc" = "$want" ] || bad "case $name exit $rc want $want"
  n=$((n + 1))
done < seed/trace/cli.cases
[ "$n" = 28 ] || bad "ran $n cases, need 28"

( cd "$D" && "$B/tracecli" export fx/a.seg ) > "$D/export.out"
got=$(sha256sum "$D/fx/a.seg" | cut -d' ' -f1)
grep -q -F "# source sha256=$got " "$D/export.out" || bad "export hash != sha256sum"

cp "$D/fx/a.seg" "$D/fx/zero.seg"
chmod 000 "$D/fx/zero.seg"
for cmd in verify show events stats export; do
  set +e
  ( cd "$D" && "$B/tracecli" "$cmd" fx/zero.seg ) > "$D/zero.out" 2>"$D/zero.err"
  rc=$?
  set -e
  [ ! -s "$D/zero.err" ] || bad "mode-0000 $cmd wrote stderr"
  [ "$rc" = 4 ] || bad "mode-0000 $cmd exit $rc want 4"
  cmp -s "$D/zero.out" seed/trace/cli-golden/zero-read.txt || bad "mode-0000 $cmd stdout"
done
chmod 644 "$D/fx/zero.seg"
rm -f "$D/fx/zero.seg"
printf 'mode-0000 refused by verify show events stats export\n'

# $1 dest  $2 exact old line  $3 exact new line. The old line must occur once
# and be gone afterwards. The new line may already occur; its count must rise by 1.
mut_swap() {
  cp seed/trace/tracecli.om "$1"
  o=$(grep -c -x -F "$2" "$1" || true)
  before=$(grep -c -x -F "$3" "$1" || true)
  [ "$o" = 1 ] || bad "mutant site count $o for: $2"
  awk -v old="$2" -v new="$3" '($0 == old) { print new; next } { print }' "$1" > "$1.tmp"
  mv "$1.tmp" "$1"
  o=$(grep -c -x -F "$2" "$1" || true)
  nn=$(grep -c -x -F "$3" "$1" || true)
  [ "$o" = 0 ] || bad "mutant site survived: $2"
  [ "$nn" = "$((before + 1))" ] || bad "mutant replacement count $nn want $((before + 1)) for: $3"
}

mut_swap "$D/mut-partial.om" 'ret 3' 'ret 0'
compile "$D/mut-partial.om" "$B/mut-partial"
set +e
( cd "$D" && "$B/mut-partial" verify fx/crc.seg ) > "$D/mut.out" 2>"$D/mut.err"
rc=$?
set -e
[ ! -s "$D/mut.err" ] || bad "partial mutant wrote stderr"
cmp -s "$D/mut.out" seed/trace/cli-golden/verify-crc.txt || bad "partial mutant stdout"
[ "$rc" = 0 ] || bad "partial mutant exit $rc want 0"
printf 'caught mutant partial: verify crc exit 0 (need 3)\n'

mut_swap "$D/mut-same.om" 'ret same' 'ret true'
compile "$D/mut-same.om" "$B/mut-same"
set +e
( cd "$D" && "$B/mut-same" compare fx/a.seg fx/b.seg ) > "$D/mut.out" 2>"$D/mut.err"
rc=$?
set -e
[ ! -s "$D/mut.err" ] || bad "same mutant wrote stderr"
grep -q -F 'compare differ at=0' "$D/mut.out" || bad "same mutant did not differ at 0"
if grep -q -F 'compare differ at=17' "$D/mut.out"; then
  bad "same mutant still differs at 17"
fi
[ "$rc" = 1 ] || bad "same mutant exit $rc want 1"
printf 'caught mutant same: compare a b differ at=0 (need 17)\n'

mut_swap "$D/mut-qtype.om" 'if and ne get qtype 0 65536 ne t get qtype 0' 'if and ne get qtype 0 65536 eq t get qtype 0'
compile "$D/mut-qtype.om" "$B/mut-qtype"
set +e
( cd "$D" && "$B/mut-qtype" events fx/a.seg --type 7 ) > "$D/mut.out" 2>"$D/mut.err"
rc=$?
set -e
[ ! -s "$D/mut.err" ] || bad "qtype mutant wrote stderr"
grep -q -F 'matched=40 of=40' "$D/mut.out" || bad "qtype mutant did not match all"
if grep -q -F 'matched=0 of=40' "$D/mut.out"; then
  bad "qtype mutant still matched none"
fi
[ "$rc" = 0 ] || bad "qtype mutant exit $rc want 0"
printf 'caught mutant qtype: events --type 7 matched=40 (need 0)\n'

mut_swap "$D/mut-ropen.om" "$ROPEN" "$ROPEN_MUT"
compile "$D/mut-ropen.om" "$B/mut-ropen"
if scan_ropen "$D/mut-ropen.om"; then
  bad "ropen mutant was not caught by the flag scan"
fi
printf 'caught mutant ropen: flag scan rejected 32770\n'

printf 'PASS tracecli %s cases, mode-0000 refused, 4 mutants caught\n' "$n"
