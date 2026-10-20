#!/bin/sh
# Gate G4 of the OSC-4 float slice (docs/osc/OSC-4-FLOATS.md): the IR bytes of every
# unit an older compiler accepts are unchanged, and every unit only the new compiler
# accepts that uses floats carries IR version 6.
# Usage: sh tests/compiler/float_g4.sh OLD_OSCC NEW_OSCC [progs-dir] [scratch-dir]
# For each progs/*.osc: OLD and NEW write NAME.ir (oscc FILE OUT); if OLD accepts the
# unit the two .ir files must be byte-identical; if only NEW accepts it, its version
# byte (offset 7, after the 7-byte magic) must be 6.
set -eu
old=$1; new=$2
progs=${3:-tests/compiler/progs}
dir=${4:-build/compiler/float_g4}
mkdir -p "$dir/old" "$dir/new"
same=0; newonly=0; bad=0; v6=0
for f in "$progs"/*.osc; do
    n=$(basename "$f" .osc)
    if ! "$new" "$f" "$dir/new/$n" > /dev/null 2>&1; then
        echo "FAIL $n: the new compiler refuses it"; bad=$((bad + 1)); continue
    fi
    ver=$(od -An -tu1 -j7 -N1 "$dir/new/$n.ir" | tr -d ' ')
    [ "$ver" = 6 ] && v6=$((v6 + 1))
    if "$old" "$f" "$dir/old/$n" > /dev/null 2>&1; then
        if cmp -s "$dir/old/$n.ir" "$dir/new/$n.ir"; then same=$((same + 1))
        else echo "FAIL $n: IR bytes differ from the old compiler's"; bad=$((bad + 1)); fi
    else
        newonly=$((newonly + 1))
        if [ "$ver" != 6 ]; then echo "FAIL $n: new unit has IR version $ver, want 6"; bad=$((bad + 1)); fi
    fi
done
[ $((same + newonly)) -gt 0 ] || { echo "FLOAT_G4_FAIL no units in $progs"; exit 1; }
[ "$bad" -eq 0 ] || { echo "FLOAT_G4_FAIL identical=$same new_only=$newonly bad=$bad"; exit 1; }
echo "FLOAT_G4_PASS identical=$same new_only=$newonly version6=$v6"
