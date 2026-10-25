#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# om/codec parity: compiles codec.om with the pinned seeded omc, regenerates the corpus with corpus.py,
# runs every stream through the .om and compares each stream's sha256 with the C oracle's recorded
# hashes in expected.txt. This script does not build or run any C: the C oracle (the product's
# omega_codec.c, omega_canonical.c, omega_validate.c, omega_core.c, sha256.c, built with the Makefile
# CFLAGS) was run once by a driver outside the tree; README.md names it and its rerun command.
#   usage: sh om/codec/parity/run.sh [--mutants]
#   OMEGA_OMC  the seed-born omc (default ../omega-seedqual-20261025/seed/out/omc), sha256 pinned below
# Exit 0 PASS, 1 FAIL, 3 NOT_RUN (no pinned omc or no python3).
set -eu
OMC_PIN=734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
OMC=${OMEGA_OMC:-$ROOT/../omega-seedqual-20261025/seed/out/omc}
[ -x "$OMC" ] || { echo "NOT_RUN: no omc at $OMC (set OMEGA_OMC)"; exit 3; }
[ "$(sha256sum < "$OMC" | cut -c1-64)" = "$OMC_PIN" ] || { echo "NOT_RUN: $OMC is not the pinned omc"; exit 3; }
command -v python3 >/dev/null || { echo "NOT_RUN: python3 missing"; exit 3; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
STREAMS="decode validate serialize format parse hexid header"

# run SRC OUTDIR: compile SRC and write every stream into OUTDIR
run() {
  mkdir -p "$2"
  nice -n 19 "$OMC" < "$1" > "$2/codec" || { echo "FAIL: omc refused $1"; return 1; }
  chmod +x "$2/codec"
  ( cd "$2" && nice -n 19 ./codec decode < "$T/corpus/decode.pack" && nice -n 19 ./codec text < "$T/corpus/text.pack" \
    && nice -n 19 ./codec hexid < "$T/corpus/hexid.pack" && nice -n 19 ./codec header ) || { echo "FAIL: codec exited $?"; return 1; }
  for s in $STREAMS; do printf '%s  %s\n' "$(sha256sum < "$2/$s.out" | cut -c1-64)" "$s"; done > "$2/got.txt"
}

nice -n 19 python3 "$HERE/corpus.py" "$T/corpus" > "$T/corpus.log"
cat "$T/corpus.log"
run "$ROOT/om/codec/codec.om" "$T/om"
fail=0
for s in $STREAMS; do
  want=$(awk -v s="$s" '$2 == s { print $1 }' "$HERE/expected.txt")
  got=$(awk -v s="$s" '$2 == s { print $1 }' "$T/om/got.txt")
  n=$(wc -l < "$T/om/$s.out")
  if [ "$want" = "$got" ]; then echo "PASS $s ($n lines) equals the recorded C hash"; else echo "FAIL $s ($n lines): $got, C recorded $want"; fail=1; fi
done
echo "records: $(wc -l < "$T/om/decode.out") wire inputs ($(awk '$2 == 0' "$T/om/decode.out" | wc -l) decoded), $(grep -c '^[0-9]* [0-9-]* [0-9]*\( [01]:[0-9a-f]*\)*$' "$T/om/parse.out") text inputs, $(wc -l < "$T/om/hexid.out") hexid records, 65536 header pairs"

# The WCAP limit (README): a graph whose bytes run past 2 MiB. The C decodes it; the port refuses with -2.
python3 - "$T/big.pack" <<'EOF'
import struct, sys
o0 = b"OMG0\x01\x06" + bytes(14)  # KIND_MEMORY, no fields, then junk inside clen
o0 += bytes(2097140 - len(o0))
o1 = b"OMG0\x01\x06" + bytes(14)
g = b"OMGG\x00\x02" + struct.pack(">I", len(o0)) + o0 + struct.pack(">I", len(o1)) + o1
open(sys.argv[1], "wb").write(struct.pack("<I", len(g)) + g)
EOF
mkdir -p "$T/big" && ( cd "$T/big" && nice -n 19 "$T/om/codec" decode < "$T/big.pack" )
echo "limit: a $(($(wc -c < "$T/big.pack") - 4))-byte graph past WCAP decodes to: $(cut -d' ' -f2 "$T/big/decode.out") (the C gives 0)"

# Every .omega/.osc/.bin file under tests/ and examples/ (the spec's program corpus). Not hashed:
# it follows the tree. At acb3f15c the C decoder accepted 0 of 248 (all refused at the OMGG magic).
nice -n 19 python3 "$HERE/corpus.py" "$T/corpus" --tree "$ROOT" > /dev/null
mkdir -p "$T/tree" && ( cd "$T/tree" && nice -n 19 "$T/om/codec" decode < "$T/corpus/tree.pack" )
echo "tree: $(wc -l < "$T/tree/decode.out") files under tests/ and examples/, accepted by the port: $(awk '$2 == 0' "$T/tree/decode.out" | wc -l)"

if [ "${1:-}" = --mutants ]; then
  # Each mutant is one edit inside one function of codec.om; it must change its stream's hash.
  python3 - "$ROOT/om/codec/codec.om" "$T" > "$T/mlist" <<'EOF'
import sys
src, t = sys.argv[1], sys.argv[2]
s = open(src).read()
M = [  # name, function, old, new, stream that must change
    ("attr sort reversed", "acmp", "if lt ci cj\nret -1", "if lt ci cj\nret 1", "decode"),
    ("key past its NUL", "ekl", "ret i\n", "ret kl\n", "decode"),
    ("relation kind order reversed", "rcmp", "if lt ki kj\nret -1", "if lt ki kj\nret 1", "decode"),
    ("constraint length tie reversed", "ccmp", "if lt li lj\nret -1", "if lt li lj\nret 1", "decode"),
    ("klen 64 accepted", "dattrs", "if ge kl 64", "if gt kl 64", "decode"),
    ("effect padding unchecked", "effchk", "if ge i prm", "if gt i prm", "decode"),
    ("id limit 4000", "ccanon", "if gt n 4096", "if gt n 4000", "decode"),
    ("unsigned width 255", "vtype", "if and eq tag 3 or lt wd 1 gt wd 256", "if and eq tag 3 or lt wd 1 gt wd 255", "validate"),
    ("value bytes rounding", "vvalue", "ex = div add wd 7 8", "ex = div add wd 8 8", "validate"),
    ("operand window offset", "dtgt", "p = add 33 mul a 32", "p = add 32 mul a 32", "validate"),
    ("struct value offset", "sattr", "ret ulong get w add add o kl add 2 v", "ret ulong get w add add o kl add 3 v", "validate"),
    ("serialize length check", "cserial", "if gt add add pos 4 L mx", "if gt add pos L mx", "serialize"),
    ("stale line length", "cformat", "if ge wv sub cap pos\nret -1", "if gt wv sub cap pos\nret -1", "format"),
    ("value last 8 bytes", "fline", "s = sub bl 8", "s = sub bl 7", "format"),
    ("no binary prefix", "spref", "bs = 2\n", "bs = 8\n", "parse"),
    ("overflow ignored", "sdig", "put r 1 1\n", "put r 1 0\n", "parse"),
    ("strstr to line end", "seol", "if eq get s i 10", "if eq get s i 11", "parse"),
    ("negative pair", "hx2", "ret and neg v 255", "ret v", "hexid"),
    ("memcmp sign", "cidcmp", "if lt p q\nret -1", "if lt p q\nret 1", "hexid"),
    ("v1 effect code", "cchkhdr", "ret -2", "ret -1", "header"),
    ("sha K[63]", "shak", "put k 63 3329325298", "put k 63 3329325299", "decode"),
]
for i, (name, fn, old, new, stream) in enumerate(M):
    a = s.index("\nfn %s " % fn)
    b = s.index("\nfn ", a + 1)
    body = s[a:b]
    if body.count(old) != 1:
        sys.exit("mutant %s: '%s' is not unique in %s" % (name, old, fn))
    open("%s/m%d.om" % (t, i), "w").write(s[:a] + body.replace(old, new) + s[b:])
    print("%d %s %s" % (i, stream, name))
EOF
  mfail=0
  n=0
  while read -r i stream name; do
    n=$((n + 1))
    if ! run "$T/m$i.om" "$T/mo" > /dev/null; then echo "FAIL mutant $i ($name): did not build or run"; mfail=1; continue; fi
    changed=""
    for s in $STREAMS; do
      a=$(awk -v s="$s" '$2 == s { print $1 }' "$T/om/got.txt"); b=$(awk -v s="$s" '$2 == s { print $1 }' "$T/mo/got.txt")
      [ "$a" = "$b" ] || changed="$changed $s"
    done
    case " $changed " in
      *" $stream "*) echo "killed mutant $i ($name): changes$changed" ;;
      *) echo "FAIL mutant $i ($name): $stream unchanged (changes:${changed:- nothing})"; mfail=1 ;;
    esac
    rm -rf "$T/mo"
  done < "$T/mlist"
  [ $mfail -eq 0 ] && echo "PASS all $n mutants change their stream's hash" || { echo "FAIL a mutant changed no stream"; fail=1; }
fi
[ $fail -eq 0 ] && echo "PASS om/codec parity: every stream equals the C oracle's recorded hash" || { echo "FAIL om/codec parity"; exit 1; }
