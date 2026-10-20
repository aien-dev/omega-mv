#!/bin/sh
# r2 decision-identity check (AMENDMENTS A4): rowdiff DIR1 DIR2 prints the number of rows
# whose decision columns differ (every rows.csv column except the four timings: cpu_ns,
# wall_ns, adapter_cpu_ns, persist_cpu_ns). 0 = the two runs made the same decisions.
#   r2-check.sh rowdiff DIR1 DIR2
#   r2-check.sh keycount SEED R BIN   per-key reuse opportunities (blocks 2, 5) at R versus
#                                     R x the count at R = 1; prints the number of keys whose
#                                     count is not exactly R times (0 = the multiplier holds)
set -u
dec() { cut -d, -f1-15,19,21- "$1"; }
case ${1:-} in
rowdiff)
    a=$(mktemp); b=$(mktemp); dec "$2/rows.csv" > "$a"; dec "$3/rows.csv" > "$b"
    n=$(diff "$a" "$b" | grep -c '^>'); la=$(wc -l < "$a"); lb=$(wc -l < "$b")
    [ "$la" = "$lb" ] || n=$((n + 1))
    echo "$n"; rm -f "$a" "$b" ;;
keycount)
    s=$2; R=$3; BIN=$4; extra=${5:-}
    "$BIN" stream --seed "$s" --reuse 1 | awk -F, '$2==2||$2==5{c[$4]++} END{for(k in c) print k, c[k]}' | sort > /tmp/kc1.$$
    "$BIN" stream --seed "$s" --reuse "$R" $extra | awk -F, '$2==2||$2==5{c[$4]++} END{for(k in c) print k, c[k]}' | sort > /tmp/kcR.$$
    join -a1 -a2 -e0 -o 0,1.2,2.2 /tmp/kc1.$$ /tmp/kcR.$$ | awk -v R="$R" '$3 != R*$2 {bad++} END{print bad+0}'
    rm -f /tmp/kc1.$$ /tmp/kcR.$$ ;;
*) echo "usage: r2-check.sh rowdiff DIR1 DIR2 | keycount SEED R BIN [--mutant M5]"; exit 2 ;;
esac
