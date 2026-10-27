#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# emit-smt.sh UNIT.om OUTDIR: omc --emit-smt. omc takes no arguments, so the mode is a leading line
# "smt" on its input (as "ir" is for the IR dump). omc prints one SMT-LIB 2 script per function, each
# starting "; omc-smt 1 fn NAME line=N"; this script writes them to OUTDIR/NAME.smt2 and prints the
# file names on stdout. Exit: 0 written, 1 usage, 2 omc refused the unit (its error on stderr).
set -eu
[ $# = 2 ] || { echo 'usage: emit-smt.sh UNIT.om OUTDIR' >&2; exit 1; }
OMC=${OMC:-seed/out/omc}
mkdir -p "$2"
all="$2/.all.smt"
if ! { printf 'smt\n'; cat "$1"; } | "$OMC" > "$all"; then
    echo "emit-smt: omc refused $1" >&2
    exit 2
fi
awk -v d="$2" '
/^; omc-smt 1 fn / { if (f != "") close(f); f = d "/" $5 ".smt2"; print f }
f != "" { print > f }
' "$all"
rm -f "$all"
