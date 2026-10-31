#!/bin/sh
# Determinism ban gate (docs/one_layer/OMEGESE.md section 12).
# detcheck.om and mkfix.om are seeded Omega, built by the pinned seed/out/omc.
# The C omegese, omega_emit and checker2 are referees only: they turn the real
# kernels into IR and enforce the race half of the ban. They never write
# detcheck's verdicts. Exit 0 PASS, 1 FAIL, 3 NOT_RUN. Artifacts: build/seed/detcheck.
set -u
cd "$(dirname "$0")/../.."
B=build/seed/detcheck
rm -rf "$B"; mkdir -p "$B/ir" "$B/neg"
: > "$B/gate.log"
say() { printf '%s\n' "$*" | tee -a "$B/gate.log"; }
fail() { say "FAIL $*"; exit 1; }
notrun() { say "NOT_RUN $*"; exit 3; }
[ "$(uname -m)" = aarch64 ] || notrun 'requires AArch64 Linux'
OMC=${OMC:-seed/out/omc}
OMS=${OMS:-build/omegese/omegese}
OME=${OME:-build/omega_emit/omega_emit}
C2=${C2:-build/checker2/checker2}
[ -f "$OMC" ] || fail "compiler $OMC missing"
pin=$(cut -d ' ' -f 1 seed/out/omc.sha256)
[ "$(sha256sum "$OMC" | cut -d ' ' -f 1)" = "$pin" ] || fail 'compiler differs from seed/out/omc.sha256'
for t in "$OMS" "$OME" "$C2"; do [ -x "$t" ] || notrun "referee $t not built (make $t)"; done
say "SOURCE $(git rev-parse HEAD) dirty=$(git status --porcelain -- seed/detcheck | wc -l)"
say "COMPILER $OMC sha256=$pin"
say "REFEREE checker2 sha256=$(sha256sum "$C2" | cut -d ' ' -f 1)"
say "PLATFORM $(uname -srm)"
build() { # source, output
    nice -n 19 "$OMC" < "$1" > "$2" 2> "$2.err" || { cat "$2.err"; return 1; }
    [ ! -s "$2.err" ] || { cat "$2.err"; return 1; }
    chmod +x "$2"
}
build seed/detcheck/detcheck.om "$B/detcheck" || fail 'detcheck.om does not compile'
build seed/detcheck/mkfix.om "$B/mkfix" || fail 'mkfix.om does not compile'
say "DETCHECK sha256=$(sha256sum "$B/detcheck" | cut -d ' ' -f 1)"

# 1. Every kernel the GPU back end is tested on, including the fixed-order tree
#    reduction (tests/checker2/kernels/reduce.json), is accepted by both checkers.
pos=0
for f in tests/osc_gpu/omegese/*.omegese; do
    "$OMS" emit "$f" "$B/ir/om_$(basename "$f" .omegese).json" > /dev/null 2>&1 || fail "omegese refuses $f"
done
for f in tests/checker2/kernels/*.json tests/osc_gpu/kernels/*.json "$B"/ir/om_*.json; do
    n=$(basename "$f" .json)
    "$OME" emit "$f" "$B/ir/$n.ir" > /dev/null 2>&1 || fail "omega_emit refuses $f"
    "$C2" check "$B/ir/$n.ir" > /dev/null 2>&1 || fail "checker2 refuses $n"
    out=$("$B/detcheck" < "$B/ir/$n.ir" 2>&1) || fail "detcheck refuses $n: $out"
    say "ACCEPT $n sha256=$(sha256sum "$B/ir/$n.ir" | cut -d ' ' -f 1) $out"
    pos=$((pos + 1))
done
[ -f "$B/ir/reduce.ir" ] || fail 'tree reduction kernel missing'

# 2. The race half: checker2 refuses every race fixture with the rule its name
#    carries; detcheck alone accepts them (races are not its rule).
race=0
for f in tests/checker2/kernels/neg/C2-GPU-GLOBAL-RACE__*.json tests/checker2/kernels/neg/C2-GPU-SHARED-RACE__*.json; do
    n=$(basename "$f" .json); rule=${n%%__*}
    "$OME" emit "$f" "$B/neg/$n.ir" > /dev/null 2>&1 || fail "omega_emit refuses $n"
    out=$("$C2" check "$B/neg/$n.ir" 2>&1) && fail "checker2 accepts race fixture $n"
    case "$out" in *"rule=$rule "*) ;; *) fail "$n refused for another reason: $out" ;; esac
    say "REFUSE $n checker2 $rule"
    race=$((race + 1))
done

# 3. The atomic half: each mkfix fixture gets its exact exit code and word; the
#    C referee refuses every ATOM unit as an unknown opcode (no back end lowers it).
run_cases() { # detcheck binary; prints one line per case: selector exit word
    grep -v '^#' seed/detcheck/cases.tsv | while IFS='	' read -r sel code word; do
        printf '%s' "$sel" | "$B/mkfix" > "$B/neg/atom_$sel.ir" || { echo "$sel mkfix-failed"; continue; }
        out=$("$1" < "$B/neg/atom_$sel.ir" 2>&1); rc=$?
        echo "$sel $rc $(printf '%s' "$out" | cut -d ' ' -f 2)"
    done
}
run_cases "$B/detcheck" > "$B/cases.out"
grep -v '^#' seed/detcheck/cases.tsv | while IFS='	' read -r sel code word; do echo "$sel $code $word"; done > "$B/cases.want"
cmp -s "$B/cases.want" "$B/cases.out" || { diff "$B/cases.want" "$B/cases.out" | tee -a "$B/gate.log"; fail 'atomic cases differ'; }
atoms=$(wc -l < "$B/cases.want")
for f in "$B"/neg/atom_*.ir; do
    out=$("$C2" check "$f" 2>&1) && fail "checker2 accepts ATOM unit $f"
    case "$out" in *"rule=C2-DECODE-OPCODE "*) ;; *) fail "checker2 refuses $f for another reason: $out" ;; esac
done
sed 's/^/CASE /' "$B/cases.out" >> "$B/gate.log"

# 4. Structural refusals fail closed.
head -c 40 "$B/ir/reduce.ir" | "$B/detcheck" > /dev/null 2>&1; [ $? = 22 ] || fail 'truncated unit not TRUNCATED'
{ cat "$B/ir/reduce.ir"; printf x; } | "$B/detcheck" > /dev/null 2>&1; [ $? = 27 ] || fail 'trailing byte not TRAILING'
{ head -c 7 "$B/ir/reduce.ir"; printf '\006'; tail -c +9 "$B/ir/reduce.ir"; } | "$B/detcheck" > /dev/null 2>&1
[ $? = 21 ] || fail 'wire version 6 not VERSION'
"$B/detcheck" < /dev/null > /dev/null 2>&1; [ $? = 22 ] || fail 'empty input not TRUNCATED'
{ printf X; tail -c +2 "$B/ir/reduce.ir"; } | "$B/detcheck" > /dev/null 2>&1; [ $? = 20 ] || fail 'bad magic not MAGIC'

# 5. Mutants: each drops one rule; the gate must see at least one wrong verdict.
caught=0
mutant() { # name, sed expression
    sed "$2" seed/detcheck/detcheck.om > "$B/mutant_$1.om"
    cmp -s seed/detcheck/detcheck.om "$B/mutant_$1.om" && fail "mutant $1 did not change the source"
    build "$B/mutant_$1.om" "$B/mutant_$1" || fail "mutant $1 does not compile"
    run_cases "$B/mutant_$1" > "$B/mutant_$1.out"
    if cmp -s "$B/cases.want" "$B/mutant_$1.out"; then fail "mutant $1 survived"; fi
    say "MUTANT $1 caught: $(diff "$B/cases.want" "$B/mutant_$1.out" | grep -c '^>') wrong verdicts"
    caught=$((caught + 1))
}
mutant no_float 's/^if eq el 13$/if eq el 99/'
mutant no_op 's/^if gt s 6$/if gt s 11/'
mutant no_result 's/^if ne d 65535$/if eq d 1/'
mutant no_mixed 's/^mixed v$/put state 15 v/'
say "PASS positives=$pos races=$race atom_cases=$atoms mutants_caught=$caught"
