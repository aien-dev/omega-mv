#!/bin/sh
# Gate G4 of the OSC-4 float slice (docs/osc/OSC-4-FLOATS.md): every unit an older
# compiler accepts keeps its identity, and every unit only the new compiler accepts
# that can be written as an IR file carries IR version 6.
# Usage: sh tests/compiler/float_g4.sh OLD_OSCC NEW_OSCC [progs-dir] [scratch-dir]
# For each progs/*.osc, `oscc FILE` prints ir_sha256 (the digest of the canonical IR
# bytes, version byte included) and code_sha256 (the machine code):
#  - OLD accepts it: both digests must be equal between OLD and NEW;
#  - only NEW accepts it: `oscc FILE OUT` writes OUT.ir where the unit container allows
#    (units passing references between functions are not written); its version byte
#    (offset 7, after the 7-byte magic) must be 6.
set -eu
old=$1; new=$2
progs=${3:-tests/compiler/progs}
dir=${4:-build/compiler/float_g4}
mkdir -p "$dir"
same=0; newonly=0; v6=0; nofile=0; bad=0
dig() { "$1" "$2" 2>/dev/null | sed -n 's/^\(ir\|code\)_sha256=//p' | paste -sd' ' -; }
for f in "$progs"/*.osc; do
    n=$(basename "$f" .osc)
    dn=$(dig "$new" "$f")
    [ -n "$dn" ] || { echo "FAIL $n: the new compiler refuses it"; bad=$((bad + 1)); continue; }
    do_=$(dig "$old" "$f")
    if [ -n "$do_" ]; then
        if [ "$do_" = "$dn" ]; then same=$((same + 1))
        else echo "FAIL $n: digests differ (old $do_, new $dn)"; bad=$((bad + 1)); fi
        continue
    fi
    newonly=$((newonly + 1))
    "$new" "$f" "$dir/$n" > /dev/null 2>&1 || true
    if [ -f "$dir/$n.ir" ]; then
        ver=$(od -An -tu1 -j7 -N1 "$dir/$n.ir" | tr -d ' ')
        if [ "$ver" = 6 ]; then v6=$((v6 + 1)); else echo "FAIL $n: new unit has IR version $ver, want 6"; bad=$((bad + 1)); fi
    else
        nofile=$((nofile + 1))
    fi
done
[ $((same + newonly)) -gt 0 ] || { echo "FLOAT_G4_FAIL no units in $progs"; exit 1; }
[ "$bad" -eq 0 ] || { echo "FLOAT_G4_FAIL identical=$same new_only=$newonly bad=$bad"; exit 1; }
echo "FLOAT_G4_PASS identical=$same new_only=$newonly new_version6=$v6 new_not_writable=$nofile"
