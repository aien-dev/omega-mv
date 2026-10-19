#!/bin/sh
# Check the net(H) closed form against the direct finite sum with aien-calc
# receipts, using the numbers in a dev receipt (01-prediction = dev-receipt.json).
# Usage: calc-check.sh <dev-receipt.json> <evidence-dir>
# Writes <evidence-dir>/calc-check.md and exports every receipt to
# <evidence-dir>/calc-receipts/.
set -eu
R="$1"; OUT="$2"
mkdir -p "$OUT"
MD="$OUT/calc-check.md"
num() { python3 -c "import json,sys; d=json.load(open('$R')); print($1)"; }

{
echo "# aien-calc receipts: net(H) closed form against the direct finite sum"
echo
echo "Source of the numbers: $(basename "$R") (development partition). Units: nanoseconds. H = opportunities."
echo
} > "$MD"

run() {   # name expr
  name="$1"; expr="$2"
  out=$(aien-calc eval --precision 12 "$expr")
  id=$(echo "$out" | awk '/^receipt/ {print $2}')
  dec=$(echo "$out" | awk '/^decimal/ {print $2}')
  aien-calc export "$id" "$OUT" >/dev/null
  printf '%s\n' "- $name: \`$expr\` = **$dec** ($id)" >> "$MD"
}

# r = 1 streams: closed form H*p*Delta - C - m*H and the direct sum written out as H terms.
for s in S1_reverse "S1_merge_P+"; do
  p=$(num "d['prediction']['$s']['p']"); d=$(num "d['prediction']['$s']['Delta_ns']")
  C=$(num "d['prediction']['$s']['C_ns']"); m=$(num "d['prediction']['$s']['m_ns']"); H=$(num "d['prediction']['$s']['H_opportunities']")
  r=$(num "d['prediction']['$s']['r']")
  is_one=$(python3 -c "print(1 if float('$r') >= 1 else 0)")
  echo >> "$MD"; echo "## $s (p=$p, Delta=$d, r=$r, C=$C, m=$m, H=$H)" >> "$MD"
  if [ "$is_one" = "1" ]; then
    # The geometric form is undefined at r = 1: aien-calc refuses the division (receipt kept on purpose).
    refused=$(aien-calc eval "$p*$d*(1-$r^$H)/(1-$r) - $C - $m*$H" 2>&1 | awk '/^refused|^receipt/ {printf "%s ", $0}')
    printf '%s\n' "- geometric form at r = 1: $refused (handled by the r = 1 case below)" >> "$MD"
    run "closed form (r=1): H*p*Delta - C - m*H" "$H*$p*$d - $C - $m*$H"
    terms=$(python3 -c "print('+'.join(['1']*$H))")
    run "direct sum (r=1): p*Delta*(sum of H ones) - C - m*H" "$p*$d*($terms) - $C - $m*$H"
  else
    run "closed form: p*Delta*(1-r^H)/(1-r) - C - m*H" "$p*$d*(1-$r^$H)/(1-$r) - $C - $m*$H"
    terms=$(python3 -c "print('+'.join(['$r^%d'%k for k in range($H)]))")
    run "direct sum: p*Delta*(r^0+...+r^(H-1)) - C - m*H" "$p*$d*($terms) - $C - $m*$H"
  fi
  run "break-even opportunities C/(p*Delta - m)" "$C/($p*$d - $m)"
done

# P0: p = 0.
C=$(num "d['prediction']['P0_no_payoff']['C_ns']"); m=$(num "d['prediction']['P0_no_payoff']['m_ns']"); H=$(num "d['prediction']['P0_no_payoff']['H_opportunities']")
echo >> "$MD"; echo "## P0_no_payoff (p=0, C=$C, m=$m, H=$H)" >> "$MD"
run "closed form (p=0): -C - m*H" "0 - $C - $m*$H"

# Model check with r < 1 (the geometric case the dev streams do not exercise): p=1, Delta=1e6, r=9/10, C=5e6, H=39.
echo >> "$MD"; echo "## model check, r < 1 (p=1, Delta=1000000, r=9/10, C=5000000, m=0, H=39)" >> "$MD"
run "closed form: Delta*(1-(9/10)^39)/(1-9/10) - C" "1000000*(1-(9/10)^39)/(1-9/10) - 5000000"
terms=$(python3 -c "print('+'.join(['(9/10)^%d'%k for k in range(39)]))")
run "direct sum: Delta*((9/10)^0+...+(9/10)^38) - C" "1000000*($terms) - 5000000"
run "sup over H of the geometric part: p*Delta/(1-r)" "1000000/(1-9/10)"
run "no-finite-payoff condition C >= p*Delta/(1-r): C - p*Delta/(1-r) (>= 0 means never pays)" "5000000 - 1000000/(1-9/10)"
run "same with C = 12000000: C - p*Delta/(1-r)" "12000000 - 1000000/(1-9/10)"
run "r = 0 case: p*Delta*0^0 - C with 0^0 = 1 (one opportunity): Delta - C" "1000000 - 5000000"
echo >> "$MD"
echo "Receipts: $OUT/calc-receipts/<hex>.json; verify with \`aien-calc verify calc:<hex>\`." >> "$MD"
echo "wrote $MD"
