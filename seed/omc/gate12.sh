#!/bin/sh
# gate12.sh -- documented capacities (OMEGESE 9.6). Each case is generated, compiled by
# seed/out/omc, and checked for the documented accept/refuse boundary. RC=0 only when all pass.
set -eu
R=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$R"
S=seed/out/omc
[ -x "$S" ] || { printf 'FAIL: %s missing; run sh seed/omega-from-seed.sh first\n' "$S" >&2; exit 1; }
mkdir -p build/seed/r12
T=$(mktemp -d build/seed/r12/cap.XXXXXX)
trap 'rm -rf "$T"' EXIT HUP INT TERM
bad() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }
say() { printf '%s\n' "$*"; }

accept() { # name file
    set +e
    "$S" < "$2" > "$T/$1.out" 2> "$T/$1.err"
    rc=$?
    set -e
    [ "$rc" = 0 ] || bad "$1: exit $rc ($(head -1 "$T/$1.err"))"
    [ -s "$T/$1.out" ] || bad "$1: empty output"
    say "PASS $1 accepted"
}
refuse() { # name file code
    set +e
    "$S" < "$2" > "$T/$1.out" 2> "$T/$1.err"
    rc=$?
    set -e
    [ "$rc" = "$3" ] || bad "$1: exit $rc, wanted $3 ($(head -1 "$T/$1.err"))"
    grep -q "error $3" "$T/$1.err" || bad "$1: diagnostic does not name error $3"
    say "PASS $1 error $3"
}

awk 'BEGIN{for(i=0;i<1024;i++)printf "global g%d uchar 1\n",i; print "fn main to int"; print "ret 0"; print "end"}' > "$T/g1024.om"
awk 'BEGIN{for(i=0;i<1025;i++)printf "global g%d uchar 1\n",i; print "fn main to int"; print "ret 0"; print "end"}' > "$T/g1025.om"
awk 'BEGIN{for(i=0;i<511;i++)printf "fn q%d to int\nret 0\nend\n",i; print "fn main to int"; print "ret 0"; print "end"}' > "$T/f511.om"
awk 'BEGIN{for(i=0;i<512;i++)printf "fn q%d to int\nret 0\nend\n",i; print "fn main to int"; print "ret 0"; print "end"}' > "$T/f512.om"
for n in 512 513; do
    awk -v n="$n" 'BEGIN{print "fn main to int"; print "x = ulong 1"; for(i=0;i<n;i++)print "if eq x 1"; print "ret 0"; for(i=0;i<n;i++)print "end"; print "ret 0"; print "end"}' > "$T/b$n.om"
done
printf 'fn main to int\na = new uchar 4194305 0\nret 0\nend\n' > "$T/local.om"
printf 'fn main to int\na = new uchar 16777217 0\nret 0\nend\n' > "$T/arr.om"
awk 'BEGIN{for(i=0;i<64;i++)printf "global h%d uchar 16777216\n",i; print "global k uchar 1"; print "fn main to int"; print "ret 0"; print "end"}' > "$T/pools.om"

accept g1024 "$T/g1024.om"
refuse g1025 "$T/g1025.om" 14
accept f511 "$T/f511.om"
refuse f512 "$T/f512.om" 14
accept b512 "$T/b512.om"
refuse b513 "$T/b513.om" 14
refuse local "$T/local.om" 23
refuse arr "$T/arr.om" 23
refuse pools "$T/pools.om" 23

# The input buffer is 8 MiB; one byte over must be refused with error 14.
dd if=/dev/zero bs=8388609 count=1 2>/dev/null | tr '\0' '\n' > "$T/big.om"
refuse big "$T/big.om" 14

say "PASS gate12 10 cases"
exit 0
