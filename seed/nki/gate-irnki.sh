#!/bin/sh
# Host-only verification of seed/nki/irnki.om, the OSC IR v1 -> AWS NKI (nki.isa) emitter.
# The NKI CPU simulator (nki.simulate, nki 0.7.0, Apache-2.0) is an interop oracle, not
# Omega-native, and never generates kernels: irnki.om writes every kernel and a native
# Omega program computes every expected lane. Simulator results are not hardware
# qualification; the Trainium/Inferentia hardware receipt stays NOT_RUN.
# NKI_ORACLE_DIR names a directory prepared by seed/nki/setup-oracle.sh (x86_64 CPython +
# nki wheel run under qemu-user, because the nki wheel is published for x86_64 only).
# Optional: OMC, NKI_TARGETS (default "trn1 trn2 trn3").
# Exit 0 PASS, 1 FAIL, 3 NOT_RUN. Every artifact stays under build/seed/nki.
set -eu
cd "$(dirname "$0")/../.."
B=build/seed/nki
mkdir -p "$B"
find "$B" -mindepth 1 -maxdepth 1 -exec rm -rf {} +
: > "$B/gate.log"
say() { printf '%s\n' "$*" | tee -a "$B/gate.log"; }
fail() { say "FAIL $*"; exit 1; }
notrun() { say "NOT_RUN $*"; exit 3; }
[ "$(uname -m)" = aarch64 ] || notrun 'requires AArch64 Linux (seed/out/omc emits AArch64)'
OMC=${OMC:-seed/out/omc}
pin=509cf7d5e8efd6cbfdafabd8876c921b7173c060f4cd7b315cffc5fd0a567c96
[ -f "$OMC" ] || fail "compiler $OMC missing"
[ "$(sha256sum "$OMC" | cut -d ' ' -f 1)" = "$pin" ] || fail 'compiler pin differs'
D=${NKI_ORACLE_DIR:-}
[ -n "$D" ] || notrun 'NKI_ORACLE_DIR unset; prepare it with seed/nki/setup-oracle.sh DIR'
for f in qemu/usr/bin/qemu-x86_64 python/bin/python3.12 sysroot/usr/lib/x86_64-linux-gnu/libc.so.6 site/nki/__init__.py; do
    [ -e "$D/$f" ] || notrun "oracle file $D/$f missing; rerun seed/nki/setup-oracle.sh"
done
D=$(cd "$D" && pwd)
TARGETS=${NKI_TARGETS:-trn1 trn2 trn3}
oracle() {
    target=$1
    shift
    env -i PATH=/usr/bin:/bin PYTHONPATH="$D/site" NEURON_LOGICAL_NC_CONFIG=1 \
        NEURON_PLATFORM_TARGET_OVERRIDE="$target" \
        nice -n 19 "$D/qemu/usr/bin/qemu-x86_64" -L "$D/sysroot" \
        "$D/python/bin/python3.12" -s -S seed/nki/oracle.py "$@"
}
version=$(oracle trn3 version 2> "$B/oracle-version.err") ||
    { cat "$B/oracle-version.err"; notrun 'NKI simulator does not start'; }
want='nki 0.7.0+37713544100.gfede04bb numpy 2.2.6 python 3.12.16'
[ "$version" = "$want" ] || notrun "oracle is '$version', pinned '$want'"
say "SOURCE $(git rev-parse HEAD)"
say "COMPILER $OMC sha256=$pin"
say "ORACLE interop oracle, not Omega-native: nki.simulate ($version) targets=$TARGETS"
say "ORACLE_HOST qemu-x86_64 $("$D/qemu/usr/bin/qemu-x86_64" --version | sed -n 1p) sha256=$(sha256sum "$D/qemu/usr/bin/qemu-x86_64" | cut -d ' ' -f 1)"
say "ORACLE_DRIVER oracle.py sha256=$(sha256sum seed/nki/oracle.py | cut -d ' ' -f 1)"
say "PLATFORM $(uname -srm)"
nice -n 19 "$OMC" < seed/nki/irnki.om > "$B/irnki" 2> "$B/compile.err"
[ ! -s "$B/compile.err" ] || fail 'emitter compile stderr'
chmod +x "$B/irnki"
# lift UNIT LANE OUT: IR v1 bytes of a unit, then the NKI module, emitted twice.
lift() {
    { printf 'ir\n'; cat "seed/nki/$1.unit.om"; } | nice -n 19 "$OMC" > "$B/$1.ir" 2> "$B/$1.ir.err"
    [ ! -s "$B/$1.ir.err" ] || fail "$1 IR stderr"
    if [ "$2" = float32 ]; then mode=f32; else mode=; fi
    # shellcheck disable=SC2086 # empty mode means no argument
    "$B/irnki" $mode < "$B/$1.ir" > "$B/$3.py" 2> "$B/$3.err" || fail "irnki $1 $2 exit $?"
    # shellcheck disable=SC2086
    "$B/irnki" $mode < "$B/$1.ir" > "$B/$3.again.py"
    cmp -s "$B/$3.py" "$B/$3.again.py" || fail "$3 emission is not deterministic"
    [ ! -s "$B/$3.err" ] || fail "$3 emitter stderr"
    say "EMIT $3 from $1.unit.om lane=$2 deterministic sha256=$(sha256sum "$B/$3.py" | cut -d ' ' -f 1)"
}
# ref UNIT LANE OUT: one native AArch64 Omega program per function computes the expected
# lanes from the same unit source. float32 rereads the unit with uint as f32, the same
# retyping irnki applies to the lanes.
ref() {
    mkdir -p "$B/$3.ref"
    for name in $(sed -n 's/^fn \([A-Za-z0-9_]*\) .*/\1/p' "seed/nki/$1.unit.om"); do
        p=$B/$3.ref/$name
        if [ "$2" = float32 ]; then
            sed '/^fn /s/ uint/ f32/g' "seed/nki/$1.unit.om" > "$p.om"
            call="r = ulong bitcast uint $name bitcast f32 a bitcast f32 b"
        else
            cat "seed/nki/$1.unit.om" > "$p.om"
            call="r = ulong $name a b"
        fi
        cat >> "$p.om" <<EOF
note gate driver: stdin is 262144 a lanes then 262144 b lanes (LE u32); stdout the results.
global inb uchar 2097152
global outb uchar 1048576
global tailb uchar 1
fn rdall to void
n = ulong 0
for t ulong 2097152
if eq n len inb
break
end
k = read 0 inb n sub len inb n
if le k 0
exit 91
end
n = add n ulong k
end
if ne n len inb
exit 91
end
k = read 0 tailb 0 1
if ne k 0
exit 92
end
end
fn lane o ulong to uint
v = ulong 0
for j ulong 4
v = or v shl ulong get inb add o j mul j 8
end
ret uint v
end
fn main to int
rdall
for i ulong 262144
a = lane mul i 4
b = lane add 1048576 mul i 4
$call
for j ulong 4
put outb add mul i 4 j uchar and shr r mul j 8 255
end
end
p = ulong 0
for t ulong 1048577
if eq p len outb
break
end
k = write 1 outb p sub len outb p
if le k 0
exit 93
end
p = add p ulong k
end
ret 0
end
EOF
        nice -n 19 "$OMC" < "$p.om" > "$p.bin.exe" 2> "$p.err" || { cat "$p.err"; fail "native reference $name compile"; }
        [ ! -s "$p.err" ] || { cat "$p.err"; fail "native reference $name compile stderr"; }
        chmod +x "$p.bin.exe"
        nice -n 19 "$p.bin.exe" < "$B/$2.in" > "$B/$3.ref/$name.bin" || fail "native reference $name exit $?"
    done
    names=$(sed -n 's/^fn \([A-Za-z0-9_]*\) .*/\1/p' "seed/nki/$1.unit.om" | tr '\n' ' ')
    say "REFERENCE $3 native Omega lanes for: $names"
}
# check ID LANE EXPECTREF: run every kernel of ID.py on every target.
check() {
    for t in $TARGETS; do
        # shellcheck disable=SC2086 # names are [A-Za-z0-9_] words
        oracle "$t" check "$B/$1.py" "$B/$2.in" "$B/$3" $names > "$B/$1.$t.log" 2>&1 || return 1
        tee -a "$B/gate.log" < "$B/$1.$t.log"
    done
}
for lane in uint32 float32; do
    oracle trn3 inputs "$lane" > "$B/$lane.in" 2> "$B/$lane.in.err" || { cat "$B/$lane.in.err"; fail "inputs $lane"; }
    say "INPUT $lane 2x262144 lanes sha256=$(sha256sum "$B/$lane.in" | cut -d ' ' -f 1)"
done
lift vecadd float32 vecadd-f32
lift vecadd uint32 vecadd-u32
lift ops uint32 ops-u32
grep -q '^            nisa.tensor_tensor(dst=tc, data1=ta, data2=tb, op=nl.add)$' "$B/vecadd-f32.py" ||
    fail 'vecadd kernel does not use nisa.tensor_tensor add'
ref vecadd float32 vecadd-f32
ref vecadd uint32 vecadd-u32
ref ops uint32 ops-u32
names=vecadd
check vecadd-f32 float32 vecadd-f32.ref || { cat "$B"/vecadd-f32.*.log; fail 'vecadd float32 differs from native Omega'; }
check vecadd-u32 uint32 vecadd-u32.ref || { cat "$B"/vecadd-u32.*.log; fail 'vecadd uint32 differs from native Omega'; }
names=$(sed -n 's/^fn \([A-Za-z0-9_]*\) .*/\1/p' seed/nki/ops.unit.om | tr '\n' ' ')
check ops-u32 uint32 ops-u32.ref || { cat "$B"/ops-u32.*.log; fail 'ops uint32 differ from native Omega'; }
oracle trn3 limit "$B/vecadd-f32.py" vecadd > "$B/limit.log" 2>&1 || { cat "$B/limit.log"; fail 'kernel accepted a shape outside 128x512 tiles'; }
say "CONTROL limit $(sed -n 1p "$B/limit.log")"

# Negative controls: each must make the oracle comparison FAIL.
names=vecadd
ALL=$TARGETS
TARGETS=trn3
neg() {
    if check "$1" "$2" "$3"; then fail "negative control $1 passed"; fi
    say "CONTROL $1 rejected: $(grep -m 1 '^FAIL\|oracle:' "$B/$1.trn3.log")"
}
# (a) the emitted kernel text changed from add to subtract
sed 's/op=nl.add)/op=nl.subtract)/' "$B/vecadd-f32.py" > "$B/mut-text.py"
cmp -s "$B/vecadd-f32.py" "$B/mut-text.py" && fail 'text mutant unchanged'
neg mut-text float32 vecadd-f32.ref
# (b) the IR BIN subcode changed from add (1) to sub (2) before lifting
[ "$(od -An -j 38 -N 2 -t u1 "$B/vecadd.ir" | tr -s ' ')" = ' 3 1' ] || fail 'vecadd IR BIN is not at offset 38'
cp "$B/vecadd.ir" "$B/mut-ir.ir"
printf '\002' | dd of="$B/mut-ir.ir" bs=1 seek=39 conv=notrunc 2> /dev/null
"$B/irnki" f32 < "$B/mut-ir.ir" > "$B/mut-ir.py"
neg mut-ir float32 vecadd-f32.ref
# (c) one expected lane flipped by one bit
mkdir -p "$B/mut-ref"
cp "$B/vecadd-f32.ref/vecadd.bin" "$B/mut-ref/vecadd.bin"
b0=$(od -An -j 4000 -N 1 -t u1 "$B/mut-ref/vecadd.bin" | tr -d ' ')
printf "\\$(printf '%03o' $((b0 ^ 1)))" | dd of="$B/mut-ref/vecadd.bin" bs=1 seek=4000 conv=notrunc 2> /dev/null
cp "$B/vecadd-f32.py" "$B/mut-ref.py"
neg mut-ref float32 mut-ref
# (d) a kernel set that drops a function
sed '/^    "vecadd": /d' "$B/vecadd-f32.py" > "$B/mut-set.py"
neg mut-set float32 vecadd-f32.ref
# Refusals: exact exit code, the error text on stderr and nothing on stdout.
refuse() {
    want=$1; label=$2; shift 2
    set +e
    "$B/irnki" "$@" < "$B/r.ir" > "$B/r.out" 2> "$B/r.err"
    got=$?
    set -e
    [ "$got" = "$want" ] || fail "refusal $label exit $got, expected $want"
    [ ! -s "$B/r.out" ] || fail "refusal $label wrote stdout"
    grep -q '^irnki: ' "$B/r.err" || fail "refusal $label without message"
    say "REFUSE $label exit $got $(cat "$B/r.err")"
}
unit() { { printf 'ir\n'; printf '%s\n' "$@"; } | nice -n 19 "$OMC" > "$B/r.ir" || fail "unit $1 IR"; }
cp "$B/vecadd.ir" "$B/r.ir"; refuse 30 usage-extra-arg f32 x
cp "$B/ops.ir" "$B/r.ir"; refuse 24 f32-xor f32
cp "$B/vecadd.ir" "$B/r.ir"; refuse 30 usage-bad-lane f64
unit 'fn q a uint b uint to uint' 'ret div a b' 'end'; refuse 24 div
unit 'fn q a uint b uint to uint' 'ret shl a b' 'end'; refuse 24 shl
unit 'fn q a int b int to int' 'ret add a b' 'end'; refuse 25 int-tag
unit 'fn q a ulong b ulong to ulong' 'ret add a b' 'end'; refuse 25 ulong-tag
unit 'fn q a uint b uint to uint' 'ret add mul a b a' 'end'; refuse 29 two-ops
unit 'fn q a uint to uint' 'ret add a a' 'end'; refuse 29 one-param
printf 'OSC1IR\000\002\001\000' > "$B/r.ir"; refuse 21 version
printf 'OSC2IR\000\001\001\000' > "$B/r.ir"; refuse 20 magic
head -c 30 "$B/vecadd.ir" > "$B/r.ir"; refuse 22 truncated
{ cat "$B/vecadd.ir"; printf '\000'; } > "$B/r.ir"; refuse 27 trailing
printf 'OSC1IR\000\001\000\000' > "$B/r.ir"; refuse 26 no-functions
say "HARDWARE NOT_RUN: no Trainium/Inferentia instance; simulator results are not hardware qualification"
say "PASS irnki: kernels from Omega equal native Omega lanes on nki.simulate targets=$ALL; controls on trn3"
sha256sum seed/nki/irnki.om seed/nki/oracle.py seed/nki/*.unit.om "$B/irnki" "$B"/*.py "$B"/*.ir | tee -a "$B/gate.log"
