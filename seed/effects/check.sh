#!/bin/sh
# Qualification for seed/effects (effect-derived capability manifest).
# The tool is seeded Omega; this coordinator only compiles, runs and compares.
# strace, bwrap, od and dd are read-only oracles / launchers, not implementation.
# Exit 0 PASS, 1 FAIL, 3 NOT_RUN (wrong compiler or missing oracle).
set -u
cd "$(dirname "$0")/../.."
ROOT=$(pwd)
OMC=${OMC:-$ROOT/seed/out/omc}
PIN=$(sed -n 's/^OMC_PIN=//p' om/units.sh)
[ -x "$OMC" ] || { echo "NOT_RUN effects: no omc at $OMC"; exit 3; }
[ "$(sha256sum < "$OMC" | cut -c1-64)" = "$PIN" ] || { echo "NOT_RUN effects: omc is not the pinned compiler $PIN"; exit 3; }
for t in strace bwrap od dd awk; do
    command -v $t > /dev/null || { echo "NOT_RUN effects: oracle $t missing"; exit 3; }
done
B=$ROOT/build/seed/effects
rm -rf "$B"; mkdir -p "$B/probe" "$B/corpus" "$B/run"
C=seed/effects/cases
E=$B/effects
fails=0; checks=0
ok() { checks=$((checks + 1)); printf 'ok   %s\n' "$*"; }
bad() { checks=$((checks + 1)); fails=$((fails + 1)); printf 'FAIL %s\n' "$*"; }
want() { # want NAME EXPECTED_EXIT ACTUAL_EXIT
    if [ "$2" = "$3" ]; then ok "$1 exit=$3"; else bad "$1 exit=$3 expected=$2"; fi
}
build() { # build SRC OUT
    "$OMC" < "$1" > "$2" 2> "$2.err" && chmod +x "$2"
}
sys_of() { # syscalls line of an elf report
    sed -n 's/^ELF 1 .* syscalls=//p'
}

build seed/effects/effects.om "$E" || { cat "$E.err"; echo "SEMANTIC EFFECTS FAIL build"; exit 1; }
echo "omc $PIN"
echo "effects sha256 $(sha256sum < "$E" | cut -c1-64)"

# 1. Closure: every keyword with a host code (>= 100, except const) is in the table,
#    and the table holds nothing else besides read/write.
sed -n 16,18p seed/omc/genkw.awk | grep -oE '[a-z0-9]+:[0-9]+' \
    | awk -F: '$2 >= 100 && $1 != "const" { print $1 }' | sort > "$B/kw.txt"
"$E" table | cut -d' ' -f1 | grep -v -x -e read -e write | sort > "$B/tab.txt"
if cmp -s "$B/kw.txt" "$B/tab.txt"; then ok "closure: $(wc -l < "$B/kw.txt") host keywords == table"
else bad "closure: host keywords differ from table"; diff "$B/kw.txt" "$B/tab.txt"; fi
# omc emits SVC at exactly these sites; a new site means the table must be re-reviewed.
n=$(grep -c '^emit 3556769793$' seed/omc/omc.om)
if [ "$n" = 5 ]; then ok "omc.om SVC emit sites = 5"; else bad "omc.om SVC emit sites = $n (expected 5): review table"; fi

# 2. Probe parity: compile one program per table word with the pinned omc and read the
#    syscalls back out of the ELF; they must equal the table row (93 = exit is always present).
probe() { # probe EXPR OUT
    printf 'fn main to int\na = new uchar 8 65\nb = new uchar 64 0\nc = new uchar 128 0\nr = long %s\nret 0\nend\n' "$1" > "$2.om"
    build "$2.om" "$2"
}
probe_parity() { # probe_parity TOOL -> prints mismatches, returns count
    m=0
    "$1" table > "$B/probe/table.txt"
    while read -r w cl s1 s2; do
        expr=$(grep "^$w|" $C/probes.txt | head -1 | cut -d'|' -f2-)
        [ -n "$expr" ] || { echo "  no probe for $w"; m=$((m + 1)); continue; }
        probe "$expr" "$B/probe/$w" || { echo "  probe $w does not compile: $(cat "$B/probe/$w.err")"; m=$((m + 1)); continue; }
        got=$("$E" elf < "$B/probe/$w" | sys_of | tr ',' '\n' | grep -v -x 93 | sort -n | tr '\n' ' ')
        exp=$(printf '%s\n%s\n' "$s1" "$s2" | grep -v -x -- - | sort -n -u | tr '\n' ' ')
        [ "$got" = "$exp" ] || { echo "  $w table=[$exp] elf=[$got]"; m=$((m + 1)); }
    done < "$B/probe/table.txt"
    return $m
}
if probe_parity "$E"; then ok "probe parity: $(wc -l < "$B/probe/table.txt") words, table == pinned omc"
else bad "probe parity"; fi
printf 'fn main to int\nret 0\nend\n' > "$B/probe/base.om"; build "$B/probe/base.om" "$B/probe/base"
b=$("$E" elf < "$B/probe/base" | sys_of)
[ "$b" = 93 ] && ok "base program syscalls = 93" || bad "base program syscalls = $b"

# 3. Self manifest: the tool reads stdin/files and writes stdout, nothing else.
"$E" src allow=io,fd,path,env < seed/effects/effects.om > "$B/self.txt"; want "self manifest allow=io,fd,path,env" 0 $?
grep -qx 'EFFECTS 1 words=6 classes=io,fd,path,env syscalls=56,57,63,64,93' "$B/self.txt" && ok "self manifest exact" || bad "self manifest: $(tail -1 "$B/self.txt")"
"$E" verify seed/effects/effects.om "$E" > "$B/selfv.txt"; want "self verify source vs binary" 0 $?

# 4. Fixtures: manifest, policy, verify.
for f in plain clock files net; do build $C/$f.om "$B/$f" || bad "fixture $f does not compile"; done
"$E" src < $C/plain.om | grep -qx 'EFFECTS 1 words=1 classes=io syscalls=64,93' && ok "plain manifest" || bad "plain manifest"
"$E" src < $C/clock.om | grep -qx 'EFFECTS 1 words=4 classes=io,ident,time,random syscalls=64,93,113,172,278' && ok "clock manifest" || bad "clock manifest"
"$E" src allow=io,ident,time,random < $C/clock.om > /dev/null; want "clock allow=io,ident,time,random" 0 $?
"$E" src allow=io,fd,path < $C/net.om > "$B/net.txt"; want "NEGATIVE net under allow=io,fd,path" 1 $?
grep -qx 'EFFECTS REFUSED word=socknew class=net line=3' "$B/net.txt" && ok "net refusal names word, class and line" || bad "net refusal text"
"$E" src allow=io,nosuch < $C/plain.om > /dev/null; want "NEGATIVE unknown class" 2 $?
for f in plain clock files net; do "$E" verify $C/$f.om "$B/$f" > /dev/null; want "verify $f" 0 $?; done
"$E" verify $C/plain.om "$B/net" > "$B/undecl.txt"; want "NEGATIVE verify plain source vs net binary" 1 $?
grep -q '^VERIFY FAIL undeclared=57,198' "$B/undecl.txt" && ok "undeclared syscalls named (57,198)" || bad "undeclared text: $(tail -1 "$B/undecl.txt")"

# 5. Corpus: every program-mode unit in seed/host/tests and the fixtures; binary within source.
cn=0; cf=0
for s in seed/host/tests/*.om seed/rung4/tests/*.om; do
    grep -q '^fn main' "$s" || continue
    o=$B/corpus/$(echo "$s" | tr '/' '_')
    build "$s" "$o" || continue
    cn=$((cn + 1))
    "$E" verify "$s" "$o" > "$o.v" || { cf=$((cf + 1)); echo "  corpus FAIL $s: $(tail -1 "$o.v")"; }
done
[ $cn -gt 0 ] && [ $cf = 0 ] && ok "corpus verify: $cn compiled programs, binary syscalls within source manifest" || bad "corpus verify: $cf of $cn"
"$E" verify seed/omc/omc.om "$OMC" > "$B/omc.v"; want "verify omc.om vs pinned omc" 0 $?

# 6. Dynamic oracle: syscalls observed by strace (after the initial execve) within the ELF set.
dyn() { # dyn NAME BINARY [ARGS] < stdin
    n=$1; shift
    (cd "$B/run" && strace -f -n -qq -o "$B/run/$n.st" "$@" > /dev/null 2>&1)
    obs=$(sed 1d "$B/run/$n.st" | sed -n 's/^[0-9]* \[ *\([0-9]*\)\].*/\1/p' | sort -n -u | tr '\n' ' ')
    allow=$("$E" elf < "$1" | sys_of | tr ',' ' ')
    extra=""
    for x in $obs; do case " $allow " in *" $x "*) ;; *) extra="$extra $x";; esac; done
    [ -z "$extra" ] && ok "strace $n observed [$obs] within ELF" || bad "strace $n undeclared:$extra"
}
dyn plain "$B/plain"
dyn clock "$B/clock"
dyn files "$B/files"
dyn net "$B/net"
dyn self "$E" src < seed/effects/effects.om
dyn selfv "$E" verify "$ROOT/seed/effects/effects.om" "$E"

# 7. Enforcement: the manifest compiled to seccomp cBPF, loaded by bwrap before execve.
#    'launch' adds execve (221), which bwrap needs to start the program.
sec() { # sec NAME FILTER_SOURCE BINARY EXPECTED_EXIT
    "$E" bpf launch < "$2" > "$B/run/$1.bpf"
    (cd "$B/run" && bwrap --dev-bind / / --seccomp 9 "$3" > /dev/null 2>&1 9< "$B/run/$1.bpf")
    want "seccomp $1" "$4" $?
}
sec plain $C/plain.om "$B/plain" 0
sec clock $C/clock.om "$B/clock" 0
sec files $C/files.om "$B/files" 0
sec net $C/net.om "$B/net" 0
sec plain-runs-net $C/plain.om "$B/net" 159
sec plain-runs-clock $C/plain.om "$B/clock" 159
sec clock-runs-files $C/clock.om "$B/files" 159
"$E" bpf launch < seed/effects/effects.om > "$B/run/self.bpf"
bwrap --dev-bind / / --seccomp 9 "$E" src < seed/effects/effects.om 9< "$B/run/self.bpf" | tail -1 | grep -q '^EFFECTS 1 words=6 ' \
    && ok "effects runs under its own filter" || bad "effects under its own filter"
"$E" bpf < $C/plain.om > "$B/run/exact.bpf"
[ "$(wc -c < "$B/run/exact.bpf")" = 64 ] && ok "exact filter for plain = 8 instructions (64 bytes)" || bad "exact filter size $(wc -c < "$B/run/exact.bpf")"

# 8. Fail-closed ELF controls (patched copies of plain).
word_at_svc() { # index of the first SVC word in the text segment
    od -An -v -tx4 -w4 "$1" | awk '$1 == "d4000001" { print NR - 1; exit }'
}
patch() { # patch IN OUT WORDINDEX LE_BYTES(octal escapes)
    cp "$1" "$2"
    printf "$4" | dd of="$2" bs=1 seek=$(($3 * 4)) conv=notrunc 2> /dev/null
}
i=$(word_at_svc "$B/plain")
patch "$B/plain" "$B/run/nomovz" $((i - 1)) '\037\040\003\325'          # NOP replaces MOVZ X8
"$E" elf < "$B/run/nomovz" > "$B/run/nomovz.txt"; want "NEGATIVE svc without MOVZ X8" 3 $?
grep -q 'reason=prev-not-movz-x8' "$B/run/nomovz.txt" && ok "  reason prev-not-movz-x8" || bad "  reason text"
patch "$B/plain" "$B/run/movzx9" $((i - 1)) '\011\010\200\322'        # MOVZ X9,#64: right number, wrong register
"$E" elf < "$B/run/movzx9" > "$B/run/movzx9.txt"; want "NEGATIVE svc after MOVZ X9" 3 $?
grep -q 'reason=prev-not-movz-x8' "$B/run/movzx9.txt" && ok "  reason prev-not-movz-x8" || bad "  reason text"
patch "$B/plain" "$B/run/btgt" $((i - 2)) '\002\000\000\024'            # B +2 lands on the SVC
"$E" elf < "$B/run/btgt" > "$B/run/btgt.txt"; want "NEGATIVE branch into SVC" 3 $?
grep -q 'reason=branch-target' "$B/run/btgt.txt" && ok "  reason branch-target" || bad "  reason text"
patch "$B/plain" "$B/run/br" $((i - 2)) '\000\000\037\326'              # BR X0
"$E" elf < "$B/run/br" > "$B/run/br.txt"; want "NEGATIVE indirect branch" 3 $?
patch "$B/plain" "$B/run/wx" 17 '\007'                                  # p_flags of phdr 0 = RWX
"$E" elf < "$B/run/wx" > "$B/run/wx.txt"; want "NEGATIVE W+X segment" 3 $?
grep -qx 'ELF REFUSED wx-segment' "$B/run/wx.txt" && ok "  reason wx-segment" || bad "  reason text"
printf 'not an elf' > "$B/run/junk"
"$E" elf < "$B/run/junk" > /dev/null; want "NEGATIVE not an ELF" 3 $?

# 9. Tool mutants must be killed by the checks above.
mut() { # mut NAME SED_EXPR
    sed "$2" seed/effects/effects.om > "$B/mut-$1.om"
    if cmp -s seed/effects/effects.om "$B/mut-$1.om"; then bad "mutant $1 did not apply"; return; fi
    build "$B/mut-$1.om" "$B/mut-$1" || { bad "mutant $1 does not compile"; return; }
    k=""
    probe_parity "$B/mut-$1" > /dev/null || k="$k probe-parity"
    "$B/mut-$1" table | cut -d' ' -f1 | grep -v -x -e read -e write | sort | cmp -s - "$B/kw.txt" || k="$k closure"
    "$B/mut-$1" verify $C/clock.om "$B/clock" > /dev/null || k="$k verify"
    "$B/mut-$1" elf < "$B/run/movzx9" > /dev/null; [ $? = 3 ] || k="$k movzx9"
    [ -n "$k" ] && ok "mutant $1 killed by:$k" || bad "mutant $1 survived"
}
mut drop-getpid 's/getpid ident 172 -\\n//'
mut wrong-random 's/random random 278 -/random random 277 -/'
mut svc-any-prev 's/^if ne and p 4292870175 3531603976$/if eq p 1/'

if [ $fails = 0 ]; then echo "SEMANTIC EFFECTS PASS checks=$checks"; exit 0; fi
echo "SEMANTIC EFFECTS FAIL failed=$fails checks=$checks"; exit 1
