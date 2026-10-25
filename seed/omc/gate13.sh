#!/bin/sh
# gate13.sh -- compiler robustness. Two deterministic generators (embedded LCG, no rand()):
#   soup  : 300 token streams from the language vocabulary plus garbage; every run must refuse with
#           a documented error code (0..23), never crash, trap or time out.
#   valid : 300 structured, balanced programs; every one must compile, and twice to identical bytes.
# RC=0 only when every case passed.
set -eu
R=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$R"
S=seed/out/omc
[ -x "$S" ] || { printf 'FAIL: %s missing; run sh seed/omega-from-seed.sh first\n' "$S" >&2; exit 1; }
mkdir -p build/seed/r13
T=$(mktemp -d build/seed/r13/fuzz.XXXXXX)
trap 'rm -rf "$T"' EXIT HUP INT TERM
bad() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }
say() { printf '%s\n' "$*"; }

cat > "$T/soup.awk" <<'AWK'
BEGIN{
  seed = 12345;
  n = split("fn main to int ulong uchar if else end while bound for ret put get new view edit struct text global note x y a b i n m = + - mul add sub div rem and or shl shr eq ne lt le gt ge neg not sizeof offsetof break trap continue random envcnt envread chdir ( ) junk own free", T, " ");
  for (p = 0; p < 300; p++) {
    f = sprintf("%s/s%04d.om", dir, p);
    printf "" > f;
    seed = (seed * 25173 + 13849) % 65536;
    if (seed % 2 == 0) print "fn main to int" > f;
    seed = (seed * 25173 + 13849) % 65536;
    lines = 4 + (seed % 24);
    for (l = 0; l < lines; l++) {
      seed = (seed * 25173 + 13849) % 65536;
      k = 1 + (seed % 6);
      line = "";
      for (j = 0; j < k; j++) {
        seed = (seed * 25173 + 13849) % 65536;
        idx = 1 + (seed % n);
        if (j > 0) line = line " ";
        line = line T[idx];
      }
      print line > f;
    }
    seed = (seed * 25173 + 13849) % 65536;
    if (seed % 2 == 0) print "end" > f;
    close(f);
  }
}
AWK

cat > "$T/valid.awk" <<'AWK'
BEGIN{
  for (p = 0; p < 300; p++) {
    seed = 777 + p * 13;
    f = sprintf("%s/v%04d.om", dir, p);
    print "fn main to int" > f;
    print "a = new ulong 4 0" > f;
    print "x = ulong 1" > f;
    print "y = ulong 2" > f;
    depth = 0;
    for (l = 0; l < 24; l++) {
      seed = (seed * 25173 + 13849) % 65536;
      r = seed / 65536;
      if (r < 0.60 || depth == 0) {
        seed = (seed * 25173 + 13849) % 65536;
        k = seed % 8;
        if (k == 0) print "x = add x 1" > f;
        else if (k == 1) print "y = mul y 2" > f;
        else if (k == 2) print "x = add x y" > f;
        else if (k == 3) print "put a 0 x" > f;
        else if (k == 4) print "x = get a 1" > f;
        else if (k == 5) print "x = sub x 1" > f;
        else if (k == 6) print "y = xor y 1" > f;
        else print "x = rem x 7" > f;
      } else if (r < 0.80 && depth < 4) {
        print "if lt x 100" > f; depth++;
      } else if (r < 0.90 && depth < 4) {
        print "while lt x 100 bound 1000" > f; depth++;
      } else if (r < 0.95 && depth < 4) {
        print "for i ulong 4" > f; depth++;
      } else if (depth > 0) {
        print "end" > f; depth--;
      }
    }
    while (depth > 0) { print "end" > f; depth--; }
    print "ret int x" > f;
    print "end" > f;
    close(f);
  }
}
AWK

awk -v dir="$T" -f "$T/soup.awk"
awk -v dir="$T" -f "$T/valid.awk"

ok=0
n=0
for f in "$T"/s*.om; do
    n=$((n+1))
    set +e
    timeout 10 "$S" < "$f" > "$T/o1" 2> "$T/e1"
    r1=$?
    timeout 10 "$S" < "$f" > "$T/o2" 2> "$T/e2"
    r2=$?
    set -e
    [ "$r1" = "$r2" ] || bad "soup $f: exit $r1 then $r2"
    cmp -s "$T/o1" "$T/o2" || bad "soup $f: output differs between runs"
    cmp -s "$T/e1" "$T/e2" || bad "soup $f: diagnostic differs between runs"
    [ "$r1" -ge 0 ] && [ "$r1" -le 23 ] || bad "soup $f: exit $r1 outside 0..23"
    ok=$((ok+1))
done
[ "$ok" = 300 ] || bad "soup: ran $ok of 300"
say "PASS 300 token-stream programs: 0..23, deterministic, no crash or timeout"

ok=0
n=0
for f in "$T"/v*.om; do
    n=$((n+1))
    set +e
    timeout 10 "$S" < "$f" > "$T/o1" 2> "$T/e1"
    r1=$?
    timeout 10 "$S" < "$f" > "$T/o2" 2> "$T/e2"
    r2=$?
    set -e
    [ "$r1" = 0 ] || bad "valid $f: exit $r1 ($(head -1 "$T/e1"))"
    cmp -s "$T/o1" "$T/o2" || bad "valid $f: bytes differ between runs"
    [ -s "$T/o1" ] || bad "valid $f: empty output"
    ok=$((ok+1))
done
[ "$ok" = 300 ] || bad "valid: ran $ok of 300"
say "PASS 300 structured programs: compiled, byte-identical twice"

say "PASS gate13 600 programs"
exit 0
