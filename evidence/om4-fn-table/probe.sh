#!/bin/sh
# Probe for the om4 function-table prototype. Run from the repo root: sh evidence/om4-fn-table/probe.sh
set -u
W=build/om4probe; mkdir -p $W
NEW=seed/rung4/om4.bin
OLD=$W/om4.old; git -c gc.auto=0 show 57a4135d:seed/rung4/om4.bin > $OLD; chmod +x $OLD
OMC=seed/out/omc
grow() { # k -> $W/g$k.om = omc.om + k one-line functions
  { cat seed/omc/omc.om; awk -v k=$1 'BEGIN{for(i=0;i<k;i++)printf "fn zq%d to ulong\nret 0\nend\n", i}'; } > $W/g$1.om
}
run() { # bin src out -> prints rc and first stderr line
  "$1" < "$2" > "$3" 2> "$3.err"; rc=$?; printf 'rc=%s %s' $rc "$(head -c 60 "$3.err")"
}
echo "== program mode: omc.om (495 fns) + k"
for k in 0 17 18 100 3601 3602; do
  grow $k
  o=$(run $OLD $W/g$k.om $W/old$k); n=$(run $NEW $W/g$k.om $W/new$k); c=$(run $OMC $W/g$k.om $W/omc$k)
  same=$(cmp -s $W/new$k $W/omc$k && echo same || echo DIFF)
  echo "k=$k total=$((495+k)) old:[$o] new:[$n] omc:[$c] new-vs-omc:$same"
done
echo "== grown compiler (k=100) birth: om4new -> g1 -> g2 -> g3"
chmod +x $W/new100; $W/new100 < $W/g100.om > $W/g2; chmod +x $W/g2; $W/g2 < $W/g100.om > $W/g3
for f in new100 g2 g3; do sha256sum $W/$f | cut -c1-16; done
echo "== function mode chains"
chain() { # n dir -> $W/c$2$1.om ; dir up: fK calls f(K-1); dir down: fK calls f(K+1)
  awk -v n=$1 -v d=$2 'BEGIN{for(i=0;i<n;i++){printf "fn g%d to int\n", i;
    if(d=="up"){ if(i==0) print "ret 7"; else printf "ret g%d\n", i-1 }
    else { if(i==n-1) print "ret 7"; else printf "ret g%d\n", i+1 }
    print "end"}}' > $W/c$2$1.om
}
for spec in "512 up" "513 up" "4096 up" "4097 up" "512 down" "1024 down" "2048 down" "4096 down"; do
  set -- $spec; chain $1 $2
  t0=$(date +%s.%N); n=$(run $NEW $W/c$2$1.om $W/fn$2$1); t1=$(date +%s.%N)
  o=$(run $OLD $W/c$2$1.om $W/fo$2$1); c=$(run $OMC $W/c$2$1.om $W/fc$2$1)
  same=$(cmp -s $W/fn$2$1 $W/fc$2$1 && echo same || echo DIFF)
  echo "fn-mode $2 n=$1 new:[$n] $(echo "$t1 - $t0" | bc)s old:[$o] omc:[$c] new-vs-omc:$same"
done
echo "== function mode cycle, 4096 functions (g_k calls g_(k+1) mod 4096)"
awk 'BEGIN{n=4096;for(i=0;i<n;i++){printf "fn g%d to int\nret g%d\nend\n", i, (i+1)%n}}' > $W/cyc.om
for b in $NEW $OMC; do t0=$(date +%s.%N); r=$(run $b $W/cyc.om $W/cyc.out); echo "cycle $b [$r] $(echo "$(date +%s.%N) - $t0" | bc)s"; done
