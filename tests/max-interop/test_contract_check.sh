#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Test for tools/max-interop/contract_check.om (#680).
# Builds the checker with seed/out/omc, then:
#  - every frozen contract in spec/max-interop/*.contract is accepted (mode=check)
#    and its printed id equals its sha256 and the id frozen in the checker;
#  - a contract with one byte changed is refused;
#  - a manifest naming a frozen contract is accepted, one with a changed
#    contract hash is refused;
#  - mutants that skip the frozen-hash comparison are caught by the checks above.
# Usage: sh tests/max-interop/test_contract_check.sh   (from any directory)
set -eu
cd "$(dirname "$0")/../.."
OMC=${OMC:-seed/out/omc}
W=$(mktemp -d "${TMPDIR:-/tmp}/cc680.XXXXXX")
trap 'rm -rf "$W"' EXIT HUP INT TERM
SRC=tools/max-interop/contract_check.om
fail=0
ok() { echo "PASS $*"; }
bad() { echo "FAIL $*"; fail=1; }

build() { # src out
    "$OMC" < "$1" > "$2" && [ -s "$2" ] && chmod +x "$2"
}

frozen_id() { # contract basename -> id compiled into the checker
    case $1 in
        matmul-bf16-v1.contract) sed -n 's/^text mid "\([0-9a-f]*\)"$/\1/p' "$SRC" ;;
        gqa-causal-v1.contract) sed -n 's/^text gid "\([0-9a-f]*\)"$/\1/p' "$SRC" ;;
    esac
}

manifest() { # op contract-id
    printf 'mode=manifest\nexpect=%s\nomega-input-manifest/1\nop=%s\ncontract=%s\nfixture=f1\ninput_sha256=%s\nseed=1\ncount=4\n' \
        "$2" "$1" "$2" e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
}

flip() { # hex string -> same string with its first digit changed
    first=$(printf %.1s "$1"); rest=${1#?}
    if [ "$first" = 0 ]; then printf '1%s' "$rest"; else printf '0%s' "$rest"; fi
}

# suite BIN: returns 0 when every check passes
suite() {
    cc=$1; s=0
    n=0
    for f in spec/max-interop/*.contract; do
        n=$((n + 1))
        b=$(basename "$f"); h=$(sha256sum "$f" | cut -d' ' -f1); fz=$(frozen_id "$b")
        out=$( { echo mode=check; cat "$f"; } | "$cc") && rc=0 || rc=$?
        if [ "$rc" = 0 ] && [ "$out" = "OK $h" ] && [ "$h" = "$fz" ]; then ok "accept $b $h"; else bad "accept $b rc=$rc out=$out sha=$h frozen=$fz"; s=1; fi
        # one byte changed: last value byte before the final newline
        { echo mode=check; head -c -2 "$f"; printf 'X\n'; } > "$W/m.in"
        out=$("$cc" < "$W/m.in") && rc=0 || rc=$?
        case "$rc:$out" in 1:REFUSED*) ok "refuse one-byte-changed $b ($out)";; *) bad "one-byte-changed $b accepted rc=$rc out=$out"; s=1;; esac
        op=$(sed -n 's/^op=//p' "$f")
        out=$(manifest "$op" "$h" | "$cc") && rc=0 || rc=$?
        if [ "$rc" = 0 ] && [ "$out" = "OK $h" ]; then ok "manifest frozen $b"; else bad "manifest frozen $b rc=$rc out=$out"; s=1; fi
        hx=$(flip "$h")
        out=$(manifest "$op" "$hx" | "$cc") && rc=0 || rc=$?
        case "$rc:$out" in 1:REFUSED*) ok "refuse changed-hash $b ($out)";; *) bad "changed-hash $b accepted rc=$rc out=$out"; s=1;; esac
    done
    [ "$n" -ge 2 ] || { bad "expected >=2 frozen contracts, found $n"; s=1; }
    return $s
}

build "$SRC" "$W/cc" || { echo "FAIL build $SRC"; exit 1; }
echo "omc=$(sha256sum "$OMC" | cut -d' ' -f1) checker=$(sha256sum "$W/cc" | cut -d' ' -f1)"
suite "$W/cc" || fail=1

# Mutants: skip the frozen-id byte comparison (compare the value with itself).
for site in mid gid; do
    from="if ne vbyte i j get $site j"
    c=$(grep -c -F -x "$from" "$SRC" || true)
    [ "$c" = 1 ] || { bad "mutant $site: site found $c times"; continue; }
    awk -v f="$from" '$0==f{print "if ne vbyte i j vbyte i j"; next} {print}' "$SRC" > "$W/mut.om"
    build "$W/mut.om" "$W/mut" || { bad "mutant $site: build failed"; continue; }
    if (suite "$W/mut") > "$W/mut.log" 2>&1; then bad "mutant skip-hash-$site SURVIVED"; else ok "mutant skip-hash-$site caught: $(grep -c ^FAIL "$W/mut.log") check(s) failed"; fi
done

[ "$fail" = 0 ] && echo "CONTRACT-CHECK PASS" || { echo "CONTRACT-CHECK FAIL"; exit 1; }
