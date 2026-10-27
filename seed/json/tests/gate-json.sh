#!/bin/sh
# seed/json gate (omega-mv #788). Host-only. CPython is a read-only oracle (tests/gen.py), never a
# generator of compiled code. Prints JSONGATE PASS/FAIL/NOT_RUN; exit 0 PASS, 1 FAIL, 3 NOT_RUN.
set -eu
cd "$(dirname "$0")/../../.."
B=${JSON_BUILD:-build/seed/json}
OMC=${OMC:-seed/out/omc}
# Compiler pin recorded independently before any reseed; update together with seed/out/omc.
PIN=${JSON_OMC_PIN:-2c82480596bbdc50cda9626866209ef662d5834f278f839ff4ce0250c495029f}
J=seed/json
fail() { printf 'JSONGATE FAIL %s\n' "$*"; exit 1; }
notrun() { printf 'JSONGATE NOT_RUN %s\n' "$*"; exit 3; }
[ "$(uname -sm)" = 'Linux aarch64' ] || notrun 'requires Linux aarch64'
command -v python3 >/dev/null || notrun 'requires python3 (read-only oracle)'
python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3, 11) else 1)' || notrun 'requires python3 >= 3.11'
[ -x "$OMC" ] || notrun "no compiler at $OMC"
[ "$(sha256sum "$OMC" | cut -d ' ' -f 1)" = "$PIN" ] || fail "compiler $OMC differs from pin $PIN"
if command -v ionice >/dev/null; then LOW='ionice -c3 nice -n 19'; else LOW='nice -n 19'; fi
rm -rf "$B"
mkdir -p "$B/out"
printf 'SOURCE %s\n' "$(git rev-parse HEAD 2>/dev/null || echo unknown)"
printf 'COMPILER %s\n' "$PIN"
python3 --version
for f in "$J/json.om" "$J/jsonw.om" "$J/jsonf.om" "$J/tests/jsdrv.om"; do
    printf 'SRC %s %s\n' "$(sha256sum "$f" | cut -d ' ' -f 1)" "$f"
done

# 1. The library units declare no globals, texts or om2 line, use no host or I/O word, and compile
# alone (with an empty main: omc before 2c824805 refused any `const` in function mode with error 9;
# #806 lifted that, and the empty main keeps this check identical across pins).
HOST='read|write|pread|pwrite|exit|fclose|fsync|fchmod|fseek|socknew|listen|accept|fork|dupfd|monons|fopen|unlink|makedir|getdnts|connect|bind|waitpid|fstat|recv|send|sleepns|argcnt|argread|waitio|execprog|renames|rmdir|killpid|pipefds|getpid|realns|sockname|cwd|readlnk|sockopt|random|envcnt|envread|chdir|ftrunc|stat|link|symlnk|chmod|faccess|lstat|truncate|chown|fchdir|umask|fdsync|memfd|shutdown|devctl|mapnew|mapread|mapwrite'
for f in "$J/json.om" "$J/jsonw.om" "$J/jsonf.om"; do
    if grep -Eq '^(global|text|om2)( |$)' "$f"; then fail "$f declares a global, text or om2 line"; fi
    if grep -v '^note' "$f" | grep -Eq "(^| )($HOST)( |$)"; then fail "$f uses a host word"; fi
done
{ printf 'om2\n'; cat "$J/json.om" "$J/jsonw.om" "$J/jsonf.om"; printf 'fn main to int\nret 0\nend\n'; } > "$B/lib.om"
set +e
$LOW "$OMC" < "$B/lib.om" > "$B/lib.bin" 2> "$B/lib.err"
rc=$?
set -e
[ "$rc" = 0 ] || fail "library standalone compile rc=$rc $(cat "$B/lib.err")"
echo 'PASS library compiles standalone (no globals, texts, host words)'

# compose UNIT_DIR OUT: build the driver from the units in UNIT_DIR
compose() {
    { printf 'om2\n'; cat "$1/json.om" "$1/jsonw.om" "$1/jsonf.om" "$J/tests/jsdrv.om"; } > "$2.om"
    set +e
    $LOW "$OMC" < "$2.om" > "$2" 2> "$2.err"
    crc=$?
    set -e
    [ "$crc" = 0 ] || return 1
    chmod +x "$2"
}
compose "$J" "$B/jsdrv" || fail "driver compile: $(cat "$B/jsdrv.err")"
printf 'DRIVER %s\n' "$(sha256sum "$B/jsdrv" | cut -d ' ' -f 1)"

# 2. Corpus and expected outputs from the oracle (deterministic, seed 788).
$LOW python3 "$J/tests/gen.py" "$B/corpus"
C=$B/corpus
[ -s "$C/cases.tsv" ] || fail 'empty corpus'
rows=$(wc -l < "$C/cases.tsv")

# run DRIVER QUIET: every row; QUIET=1 stops at the first mismatch and prints nothing for it.
# Sets bad to the number of mismatching rows.
run() {
    bad=0
    tab=$(printf '\t')
    while IFS="$tab" read -r id mode a2 a3 kind; do
        set -- "$mode"
        [ "$a2" = - ] || set -- "$@" "$a2"
        [ "$a3" = - ] || set -- "$@" "$a3"
        o=$B/out/$id.$mode
        set +e
        $LOW timeout 60 "$DRV" "$@" < "$C/$id.in" > "$o" 2> "$o.err"
        rc=$?
        set -e
        ok=1
        if [ "$rc" != 0 ]; then
            ok=0
        elif [ "$kind" = err ]; then
            case "$(head -c 4 "$o")" in 'ERR ') ;; *) ok=0 ;; esac
        else
            cmp -s "$o" "$C/$id.$mode.want" || ok=0
        fi
        if [ "$ok" = 0 ]; then
            bad=$((bad + 1))
            [ "$QUIET" = 1 ] && return 0
            printf 'MISMATCH %s mode=%s args=%s rc=%s\n' "$id" "$mode" "$*" "$rc"
            [ "$bad" -gt 20 ] && return 0
        fi
    done < "$C/cases.tsv"
}

# 3. Parity: every corpus row, every mode.
DRV=$B/jsdrv QUIET=0 run
[ "$bad" = 0 ] || fail "parity rows=$rows mismatches=$bad (first ones listed above)"
printf 'PASS parity rows=%s (tokens, walk, f64, rewrite, writer scripts)\n' "$rows"

# 4. Driver boundary: input larger than its array is refused with exit 93, not truncated.
head -c 524289 /dev/zero | tr '\0' ' ' > "$B/big.in"
set +e
$LOW "$B/jsdrv" t < "$B/big.in" > /dev/null 2>&1
rc=$?
set -e
[ "$rc" = 93 ] || fail "oversized driver input rc=$rc (want 93)"
echo 'PASS oversized driver input refused (93)'

# 5. Mutants: each must make the parity run fail.
mutant() { # name file old new
    rm -rf "$B/m"
    mkdir -p "$B/m"
    cp "$J/json.om" "$J/jsonw.om" "$J/jsonf.om" "$B/m/"
    n=$(grep -cxF "$3" "$B/m/$2" || true)
    [ "$n" = 1 ] || fail "mutant $1: pattern found $n times in $2"
    awk -v old="$3" -v new="$4" '$0 == old { print new; next } { print }' "$B/m/$2" > "$B/m/x"
    mv "$B/m/x" "$B/m/$2"
    if ! compose "$B/m" "$B/m/jsdrv"; then fail "mutant $1 does not compile: $(cat "$B/m/jsdrv.err")"; fi
    DRV=$B/m/jsdrv QUIET=1 run
    [ "$bad" != 0 ] || fail "mutant $1 survived"
    printf 'PASS mutant %s caught\n' "$1"
}
mutant overlong-e0 json.om 'lo = uchar 160' 'lo = uchar 128'
mutant raw-control json.om 'if lt c 32' 'if lt c 31'
mutant depth-off-by-one json.om 'if ge d js_hg tape jshdlim' 'if gt d js_hg tape jshdlim'
mutant writer-escape jsonw.om 'if ge c 32' 'if ge c 31'
mutant ties-to-even jsonf.om 'ret eq rem js_dget fs q ulong sub i 1 2 1' 'ret true'
rm -rf "$B/m"
printf 'JSONGATE PASS rows=%s mutants=5\n' "$rows"
