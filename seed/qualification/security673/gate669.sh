#!/bin/sh
# gate669.sh -- qualification for Omega issue 669: AArch64 19-bit branch displacement.
# Run from the repo root: sh seed/qualification/security673/gate669.sh [compiler]
# The fixture generator is seeded .om (branch-corpus.om). The independent decoder is GNU objdump,
# outside the seed compiler closure. The expected target is the unique mov x1, #12345 marker emitted
# immediately after the conditional block. Results: evidence/SECURITY-673/.
set -eu
C=${1:-build/security673/fix/stage2}
G=seed/qualification/security673/branch-corpus.om
B=${B:-build/security673/gate669}
E=evidence/SECURITY-673
OBJDUMP=${OBJDUMP:-objdump}
mkdir -p "$B" "$E"
log=$E/gate669.log
: > "$log"
fail=0
cases=0
say() { printf '%s\n' "$*" | tee -a "$log"; }
bad() { say "FAIL: $*"; fail=1; }
sha() { sha256sum "$1" | cut -d' ' -f1; }
norm() { h=${1#0x}; while [ "${h#0}" != "$h" ]; do h=${h#0}; done; [ -z "$h" ] && h=0; printf '%s' "$h"; }

say "gate669 $(date -u +%FT%TZ)"
say "source_head $(git rev-parse HEAD)"
say "omc_source sha256 $(sha seed/omc/omc.om)"
say "compiler $C sha256 $(sha "$C")"
say "decoder $($OBJDUMP --version | head -1)"
say "generator sha256 $(sha "$G")"

# read count as 4 little-endian bytes without command substitution (NUL bytes would be dropped)
le32() {
  a=$(( $1 & 255 )); b=$(( ($1 >> 8) & 255 )); c=$(( ($1 >> 16) & 255 )); d=$(( ($1 >> 24) & 255 ))
  printf "\\$(printf '%03o' "$a")\\$(printf '%03o' "$b")\\$(printf '%03o' "$c")\\$(printf '%03o' "$d")"
}
gen() { # mode count file
  { printf "\\$(printf '%03o' "$1")"; le32 "$2"; } | ./$B/gen > "$3"
}
compile() { # source output error -> rc
  set +e
  "$C" < "$1" > "$2" 2> "$3"
  rc=$?
  set -e
  printf '%s' "$rc"
}
expected_target() { # binary -> "branch_addr marker_addr"
  tail -c +177 "$1" > "$B/dec.text"
  "$OBJDUMP" -D -b binary -m aarch64 "$B/dec.text" > "$B/dec.dis" 2> "$B/dec.err" || return 1
  m=$(awk '/#12345/ { a=$1; sub(":", "", a); print a; exit }' "$B/dec.dis")
  b=$(awk '$3 ~ /^b\./ { print $4; exit }' "$B/dec.dis")
  a=$(awk '$3 ~ /^b\./ { x=$1; sub(":", "", x); print x; exit }' "$B/dec.dis")
  [ -n "$m" ] && [ -n "$b" ] && [ -n "$a" ] || return 1
  printf '%s %s %s' "$(norm "$a")" "$(norm "$b")" "$(norm "$m")"
}

./"$C" < "$G" > "$B/gen" 2> "$B/gen.err" || { bad "generator did not build: $(cat "$B/gen.err")"; say "GATE669 FAIL"; exit 1; }
chmod +x "$B/gen"
say "built generator with compiler: $(sha "$B/gen")"

# 1. sanity: the fixture is executable and the branch lands on the marker
gen 0 16 "$B/small.om"
rc=$(compile "$B/small.om" "$B/small.bin" "$B/small.err"); cases=$((cases + 1))
if [ "$rc" = 0 ] && [ -s "$B/small.bin" ]; then
  chmod +x "$B/small.bin"; set +e; ./$B/small.bin; rr=$?; set -e
  got=$(expected_target "$B/small.bin" || true)
  set -- $got; a=${1:-} t=${2:-} m=${3:-}
  if [ "$rr" = 0 ] && [ -n "$t" ] && [ "$t" = "$m" ]; then
    d=$(( (0x$m - 0x$a) / 4 ))
    say "PASS sanity: N=16 exit 0, branch at 0x$a targets 0x$t = marker, displacement $d words"
  else bad "sanity: exit $rr, branch 0x${t:-?} marker 0x${m:-?}"; fi
else bad "sanity fixture did not compile: rc=$rc $(cat "$B/small.err")"; fi

# 2. exact forward maximum: 2^18 - 1 words = 1,048,572 bytes
gen 0 262142 "$B/max.om"
rc=$(compile "$B/max.om" "$B/max.bin" "$B/max.err"); cases=$((cases + 1))
if [ "$rc" = 0 ] && [ -s "$B/max.bin" ]; then
  chmod +x "$B/max.bin"; set +e; ./$B/max.bin; rr=$?; set -e
  got=$(expected_target "$B/max.bin" || true)
  set -- $got; a=${1:-} t=${2:-} m=${3:-}
  d=$(( (0x$m - 0x$a) / 4 ))
  if [ "$rr" = 0 ] && [ "$t" = "$m" ] && [ "$d" = 262143 ]; then
    say "PASS exact maximum: displacement $d words = 1048572 bytes, exit 0, objdump target = marker"
  else bad "maximum: exit $rr, branch 0x${t:-?} marker 0x${m:-?}, d=$d"; fi
else bad "maximum fixture did not compile: rc=$rc $(cat "$B/max.err")"; fi

# 3. one word beyond the maximum must be refused with error 24 and no artifact
gen 0 262143 "$B/over.om"
rc=$(compile "$B/over.om" "$B/over.bin" "$B/over.err"); cases=$((cases + 1))
want="omc: line 262148: error 24"
if [ "$rc" = 24 ] && [ ! -s "$B/over.bin" ] && [ "$(cat "$B/over.err")" = "$want" ]; then
  say "PASS one beyond: rc 24, empty stdout, stderr '$want'"
else bad "one beyond: rc=$rc size=$(wc -c < "$B/over.bin") err=$(cat "$B/over.err")"; fi

# 4. a displacement that fits 19 raw bits but not the signed range is also refused
gen 0 524287 "$B/wrap.om"
rc=$(compile "$B/wrap.om" "$B/wrap.bin" "$B/wrap.err"); cases=$((cases + 1))
if [ "$rc" = 24 ] && [ ! -s "$B/wrap.bin" ] && grep -q 'error 24$' "$B/wrap.err"; then
  say "PASS signed range: N=524287 (would wrap inside 19 bits) refused: $(cat "$B/wrap.err")"
else bad "signed range: rc=$rc size=$(wc -c < "$B/wrap.bin") err=$(cat "$B/wrap.err")"; fi

# 5. other reachable encoders: nested if (outer and inner), while exit, and a trap-site branch
for spec in "1 nested-if" "2 while-exit" "3 trap-site"; do
  set -- $spec; mode=$1; name=$2
  gen "$mode" 300000 "$B/$name.om"
  rc=$(compile "$B/$name.om" "$B/$name.bin" "$B/$name.err"); cases=$((cases + 1))
  if [ "$rc" = 24 ] && [ ! -s "$B/$name.bin" ] && grep -q 'error 24$' "$B/$name.err"; then
    say "PASS $name: 300000 statements refused before an artifact: $(cat "$B/$name.err")"
  else bad "$name: rc=$rc size=$(wc -c < "$B/$name.bin") err=$(cat "$B/$name.err")"; fi
done

# 6. negative control: a compiler with the bound moved by one word accepts N=262144; the independent
#    decoder must see the branch miss the marker. The program itself exits 0, so the decoder is the oracle.
sed 's/gt diff 1048572/gt diff 1048580/' seed/omc/omc.om > "$B/omc-mutant.om"
cmp -s "$B/omc-mutant.om" seed/omc/omc.om && bad "mutant source did not change"
./seed/rung4/om4.bin < "$B/omc-mutant.om" > "$B/mutant" 2> "$B/mutant.build.err" || bad "mutant did not build"
chmod +x "$B/mutant" 2>/dev/null || true
say "mutant compiler sha256 $(sha "$B/mutant")"
gen 0 262144 "$B/mutant-case.om"
rc=$( "$B/mutant" < "$B/mutant-case.om" > "$B/mutant-case.bin" 2> "$B/mutant-case.err"; echo $? ); cases=$((cases + 1))
if [ "$rc" = 0 ] && [ -s "$B/mutant-case.bin" ]; then
  chmod +x "$B/mutant-case.bin"
  got=$(expected_target "$B/mutant-case.bin" || true)
  set -- $got; a=${1:-} t=${2:-} m=${3:-}
  set +e; ./$B/mutant-case.bin; rr=$?; set -e
  if [ -n "$t" ] && [ "$t" != "$m" ]; then
    say "PASS mutant caught by independent decoder: target 0x$t != marker 0x$m (program exit $rr; exit status alone is not the oracle)"
  else bad "mutant escaped: decoder target 0x${t:-?} = marker 0x${m:-?}"; fi
else bad "mutant case did not compile: rc=$rc $(cat "$B/mutant-case.err")"; fi

# 7. fixed compiler refuses the same mutant case
rc=$(compile "$B/mutant-case.om" "$B/fixed-case.bin" "$B/fixed-case.err"); cases=$((cases + 1))
if [ "$rc" = 24 ] && [ ! -s "$B/fixed-case.bin" ]; then
  say "PASS fixed compiler refuses the mutant case: $(cat "$B/fixed-case.err")"
else bad "fixed compiler on mutant case: rc=$rc size=$(wc -c < "$B/fixed-case.bin")"; fi

say "cases run: $cases"
if [ "$fail" = 0 ]; then say "GATE669 PASS"; else say "GATE669 FAIL"; fi
exit "$fail"
