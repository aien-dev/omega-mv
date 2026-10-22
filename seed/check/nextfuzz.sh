#!/bin/sh
# nextfuzz.sh N REF [OTHER] -- the next-word fuzz over the om4 words (decision 22, OMEGESE.md section 9).
# N random programs from nextgen.awk (seeds 1..N): REF must accept each. Then each program cut after every
# word, and each program with one word replaced by a word from the vocabulary (one per word place): REF
# must answer every input with exit 0 or an error number on stderr, never a crash (a signal, exit >= 128);
# with OTHER given, OTHER must give the same bytes, exit status and error number (prefix om4/omc aside).
set -eu
N=$1; REF=$2; OTH=${3:-}; D=$(dirname "$0"); W=${NFW:-/tmp/nextfuzz.$$}; mkdir -p $W
V="fn to end if else while bound for ret exit put get len read write new global text view edit true false note neg not add sub mul div rem and or xor shl shr eq ne lt le gt ge ( ) = bool uchar ushort uint ulong byte short int long void main 0 1 -1 255 18446744073709551615 9223372036854775808 x zz abcdefghij 123456789012345678901 1x \"s\" \"q\\\\x\""
# Seed-native helpers are built by the same seeded reference compiler.
for tool in elfcheck byteequal diagprefix; do
  "$REF" < "$D/../native/$tool.om" > "$W/$tool"
  [ -s "$W/$tool" ] || exit 2
  chmod +x "$W/$tool"
done
for tool in elfcheck byteequal diagprefix; do
  "$W/elfcheck" < "$W/$tool" || exit 2
done
acc=0; inputs=0; bad=0; codes=""
one() { # file -> checks it
  inputs=$((inputs + 1))
  set +e; $REF < $1 > $W/a 2> $W/ae; ra=$?; set -e
  if [ $ra -ge 128 ] || { [ $ra != 0 ] && ! grep -q "line [0-9]*: error $ra\$" $W/ae; }; then bad=$((bad + 1)); echo "CRASH or no error number: exit $ra on $(cp $1 $W/bad$bad.om; echo $W/bad$bad.om)"; fi
  codes="$codes $ra"
  if [ -n "$OTH" ]; then
    set +e; $OTH < $1 > $W/b 2> $W/be; rb=$?; set -e
    "$W/diagprefix" < "$W/ae" > "$W/aen"
    "$W/diagprefix" < "$W/be" > "$W/ben"
    { [ $ra = $rb ] && "$W/byteequal" 3< "$W/a" 4< "$W/b" && "$W/byteequal" 3< "$W/aen" 4< "$W/ben"; } || { bad=$((bad + 1)); echo "DIFFER: exit $ra / $rb on $(cp $1 $W/bad$bad.om; echo $W/bad$bad.om)"; }
  fi
}
s=1
while [ $s -le $N ]; do
  awk -v seed=$s -f $D/nextgen.awk > $W/p.om
  set +e; $REF < $W/p.om > /dev/null 2> $W/pe; r=$?; set -e
  if [ $r = 0 ]; then acc=$((acc + 1)); elif [ $r = 12 ]; then :; else bad=$((bad + 1)); echo "REFUSED generated program seed $s: $(cat $W/pe)"; fi
  rm -f $W/v.*
  awk -v seed=$s -v V="$V" -v W=$W 'BEGIN { srand(seed + 7919); nv = split(V, voc, " ") }
    { L[NR] = $0 } END {
      n = 0; for (i = 1; i <= NR; i++) { m = split(L[i], w, /[ \t]+/); for (j = 1; j <= m; j++) if (w[j] != "") { n++; wl[n] = i; wj[n] = j } }
      for (k = 1; k <= n; k++) {
        f = W "/v.c" k; for (i = 1; i < wl[k]; i++) print L[i] > f
        m = split(L[wl[k]], w, /[ \t]+/); s = ""; c = 0; for (j = 1; j <= m; j++) if (w[j] != "") { c++; s = s (c > 1 ? " " : "") w[j]; if (j == wj[k]) break }
        printf "%s", s > f; close(f)
        f = W "/v.m" k; for (i = 1; i <= NR; i++) { if (i != wl[k]) { print L[i] > f; continue }
          m = split(L[i], w, /[ \t]+/); s = ""; c = 0; for (j = 1; j <= m; j++) if (w[j] != "") { c++; s = s (c > 1 ? " " : "") (j == wj[k] ? voc[int(rand() * nv) + 1] : w[j]) }
          print s > f }
        close(f) } }' $W/p.om
  for f in $W/v.*; do one $f; done
  s=$((s + 1))
done
echo "$codes" | tr ' ' '\n' | grep . | sort -n | uniq -c | awk '{printf "%s:%s ", $2, $1} END {print ""}' > $W/codes
echo "nextfuzz: $N programs ($acc accepted, $((N - acc)) out of registers, error 12), $inputs cut or changed inputs, $bad bad; exit codes $(cat $W/codes)"
[ $bad = 0 ]
