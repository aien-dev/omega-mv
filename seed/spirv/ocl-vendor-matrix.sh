#!/usr/bin/env bash
# Verification only: replays gate-irspirv artifacts (modules plus native dumps produced on
# AArch64 by seed/spirv/gate-irspirv.sh) through one opencl-host build that pins another CPU
# OpenCL runtime. Unlike parity.py it does not stop at the first API failure: every row gets
# built/ran/match columns and the host's error text, because the point is to list what breaks.
# usage: ocl-vendor-matrix.sh BUILD_DIR HOST TABLE PREFIX OUT.tsv [--mutant-baseline]
# --mutant-baseline substitutes mutant-flipped.spv for unit "mutant" (negative control).
# Exit 0 when every row built, ran and matched; 1 when any row did not; 2 on usage/artifact error.
# HOST_WRAP (optional) is a command prefix such as an intercept-layer loader.
set -u
[ $# -eq 5 ] || [ $# -eq 6 ] || { echo 'usage: ocl-vendor-matrix.sh BUILD_DIR HOST TABLE PREFIX OUT.tsv [--mutant-baseline]' >&2; exit 2; }
B=$1 HOST=$2 TABLE=$3 PREFIX=$4 OUT=$5 MUTANT=${6:-}
[ -z "$MUTANT" ] || [ "$MUTANT" = --mutant-baseline ] || { echo "unknown option $MUTANT" >&2; exit 2; }
tag=${MUTANT:+mutant}; tag=${tag:-vendor}
LOGS=${OUT%.tsv}.logs   # per-row host stdout/stderr, beside the table
mkdir -p "$LOGS"
printf 'case\tunit\tkernel\targs\tnative_status\tnative_bits\tbuilt\tran\tresult_bits\tresult_status\tmatch\thost_exit\terror\n' > "$OUT"
i=0 bad=0
while IFS=$'\t' read -r unit name _ret typed status; do
    case "$unit" in ''|'#'*) continue ;; esac
    i=$((i+1))
    module=$B/$unit.spv
    [ -n "$MUTANT" ] && [ "$unit" = mutant ] && module=$B/mutant-flipped.spv
    [ -f "$module" ] || { echo "missing module $module" >&2; exit 2; }
    [ -f "$B/$PREFIX-$i.status" ] || { echo "missing native $B/$PREFIX-$i.status" >&2; exit 2; }
    nstatus=$(cat "$B/$PREFIX-$i.status")
    [ "$nstatus" = "$status" ] || { echo "row $i native status $nstatus expected $status" >&2; exit 2; }
    if [ "$nstatus" = 0 ]; then
        [ "$(wc -c < "$B/$PREFIX-$i.out")" = 8 ] || { echo "row $i incomplete native dump" >&2; exit 2; }
        nbits=$(od -An -t u8 "$B/$PREFIX-$i.out" | tr -d ' \n')
    else
        nbits=0
    fi
    args=()
    if [ "$typed" != - ]; then
        IFS=, read -ra parts <<< "$typed"
        for p in "${parts[@]}"; do
            kind=${p%%:*} v=${p#*:}
            if [ "$kind" = bool ]; then
                case "$v" in true) args+=(1) ;; false) args+=(0) ;; *) echo "bad bool $v" >&2; exit 2 ;; esac
            elif [ "${v#-}" != "$v" ]; then
                args+=("$(printf '%u' "$v")")   # two's complement, as parity.py masks to 64 bits
            else
                args+=("$v")
            fi
        done
    fi
    base=$LOGS/$i
    # shellcheck disable=SC2086 # HOST_WRAP is an intentional word-split prefix
    timeout 120 ${HOST_WRAP:-} "$HOST" "$module" "$name" "${args[@]}" > "$base.out" 2> "$base.err"
    rc=$?
    built=yes ran=no rbits=- rstatus=- match=no
    case $rc in
        0) ran=yes ;;
        6) built=no ;;
        7|8) ;;
        3|4|5) built=env ;;
        124) built=unknown; echo "opencl-host: timed out after 120 s" >> "$base.err" ;;
        *) built=unknown; echo "opencl-host: terminated with exit $rc" >> "$base.err" ;;
    esac
    if [ $rc = 0 ]; then
        if read -r rbits rstatus < "$base.out" && [ -n "$rstatus" ]; then
            [ "$rbits" = "$nbits" ] && [ "$rstatus" = "$nstatus" ] && match=yes
        else
            ran=no; rbits=-; rstatus=-; echo "opencl-host: malformed stdout" >> "$base.err"
        fi
    fi
    # Error text: host stderr minus the identity lines and the success-path build log.
    if [ "$match" = yes ]; then
        err=-
    else
        err=$(grep -v -e '^opencl-host: library=' -e '^opencl-host: platform=' -e '^opencl-host: il=' "$base.err" |
              tr '\t\n' '  ' | tr -s ' ' | cut -c1-400)
        [ -n "$err" ] || err="result mismatch: native $nbits/$nstatus got $rbits/$rstatus"
        [ "$ran" = yes ] && err="result mismatch: native $nbits/$nstatus got $rbits/$rstatus"
        bad=$((bad+1))
    fi
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$i" "$unit" "$name" \
        "$(IFS=,; echo "${args[*]:--}")" "$nstatus" "$nbits" "$built" "$ran" "$rbits" "$rstatus" \
        "$match" "$rc" "$err" >> "$OUT"
done < "$TABLE"
[ "$i" -gt 0 ] || { echo "empty table $TABLE" >&2; exit 2; }
built_no=$(awk -F'\t' 'NR>1 && $7!="yes"' "$OUT" | wc -l)
ran_no=$(awk -F'\t' 'NR>1 && $8!="yes"' "$OUT" | wc -l)
echo "MATRIX $PREFIX $tag rows=$i matched=$((i-bad)) not_matched=$bad not_built=$built_no not_ran=$ran_no table=$OUT"
[ "$bad" = 0 ]
