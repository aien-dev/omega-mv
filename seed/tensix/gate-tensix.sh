#!/bin/sh
# Host-only check of the Tenstorrent Tensix RISC-V vecadd (seed/tensix). Two producers of the
# same RV32IM kernel: (1) the unchanged rung-2 assembler seed/rung2/asm2.bin with the plain-text
# table rv32im.tbl and hand-written listings; (2) seeded Omega: seed/out/omc builds irrv32.om,
# which lifts the OSC IR v1 of a scalar uint unit (vecadd.unit.om, mix.unit.om) into the
# elementwise loop. Every checker below is an interop oracle, not Omega-native: GNU objdump and
# LLVM objdump disassemble; ttsim (Tenstorrent's open simulator, Wormhole and Blackhole
# libraries) and qemu-riscv32 execute; oracle.py only moves bytes in and out. None of them
# generates or edits an instruction. Nothing here touches a Tenstorrent card.
# Optional: TTSIM_DIR (holds libttsim_wh_aarch64.so and libttsim_bh_aarch64.so of ttsim v1.15.0),
# RISCV_OBJDUMP, RISCV_OBJCOPY, LLVM_OBJDUMP, QEMU_RISCV32, PYTHON.
# Exit 0 PASS, 1 FAIL, 3 NOT_RUN (some oracle absent, nothing failed). Artifacts: build/seed/tensix.
set -u
cd "$(dirname "$0")/../.."
R=seed/tensix
B=build/seed/tensix
rm -rf "$B"
mkdir -p "$B"
: > "$B/gate.log"
say() { printf '%s\n' "$*" | tee -a "$B/gate.log"; }
fails=0
notrun=0
ok() { say "PASS $*"; }
bad() { say "FAIL $*"; fails=$((fails + 1)); }
nr() { say "NOT_RUN $*"; notrun=$((notrun + 1)); }
sha() { sha256sum "$1" | cut -d ' ' -f 1; }
first_diff() { cmp "$1" "$2" 2>&1 | sed -n 1p; }

ASM=seed/rung2/asm2.bin
TAB=$R/rv32im.tbl
ASM_PIN=6988abe072fac3cdef2240d5ad13a3ecd8fb576fa09112d344b9e7c2315969c4
OMC=seed/out/omc
OMC_PIN=9b77c78df67f7ca030dd11de16976d6d0a2371cccda88b27de722b33b94bb545
TTSIM_VERSION=v1.15.0
TTSIM_WH_PIN=e2a56ffe04072a10d477db1ef85cdbd5d392d9fc52043bb949b90a4c01ff6b4f
TTSIM_BH_PIN=2367dccc964c0ce4a20b14520e5f7ac6191bb227fb20e87aca1e94bf7432ced2
TTSIM_DIR=${TTSIM_DIR:-build/oracle/ttsim-$TTSIM_VERSION}
GNU=${RISCV_OBJDUMP:-riscv64-unknown-elf-objdump}
OBJCOPY=${RISCV_OBJCOPY:-riscv64-unknown-elf-objcopy}
LLVM=${LLVM_OBJDUMP:-/usr/lib/llvm-18/bin/llvm-objdump}
QEMU=${QEMU_RISCV32:-qemu-riscv32}
PY=${PYTHON:-python3}
have() { command -v "$1" >/dev/null 2>&1; }

[ "$(uname -m)" = aarch64 ] || { say "NOT_RUN asm2.bin is an AArch64 Linux program"; exit 3; }
[ -f "$ASM" ] && [ "$(sha "$ASM")" = "$ASM_PIN" ] || { say "FAIL assembler $ASM pin differs"; exit 1; }
say "SOURCE $(git rev-parse HEAD 2>/dev/null || echo unknown)"
say "COMPILER $ASM sha256=$ASM_PIN (unchanged rung-2 assembler); $OMC sha256=$(sha "$OMC" 2>/dev/null) (pin $OMC_PIN; builds irrv32.om)"
say "TABLE $TAB sha256=$(sha "$TAB") entries=$(grep -c '^= ' "$TAB")"
say "PLATFORM $(uname -srm)"

# ---- 1. assemble twice; both builds must be identical and equal the committed vecadd.bin ----
asm() { cat "$TAB" "$@" | nice -n 19 "$ASM"; }
build() { # name listing...
    n=$1
    shift
    asm "$@" > "$B/$n.bin" 2> "$B/$n.err" || { bad "asm2 refused $n: $(cat "$B/$n.err")"; return; }
    asm "$@" > "$B/$n.again.bin" 2>/dev/null
    cmp -s "$B/$n.bin" "$B/$n.again.bin" && ok "determinism $n ($(wc -c < "$B/$n.bin") bytes, sha256=$(sha "$B/$n.bin"))" ||
        bad "determinism $n: two builds differ"
}
build vecadd $R/vecadd.s2
build tt $R/boot-tt.s2 $R/vecadd.s2
build qemu $R/boot-qemu.s2 $R/vecadd.s2
build edges $R/edges.s2
cmp -s "$B/vecadd.bin" $R/vecadd.bin && ok "vecadd.bin equals the committed bytes" || bad "vecadd.bin differs from committed $R/vecadd.bin"
dd if="$B/tt.bin" bs=4 skip=9 2>/dev/null | cmp -s - "$B/vecadd.bin" && ok "tt image = boot-tt (36 bytes) + vecadd" || bad "tt image tail is not vecadd"
dd if="$B/qemu.bin" bs=4 skip=15 2>/dev/null | cmp -s - "$B/vecadd.bin" && ok "qemu image = boot-qemu (60 bytes) + vecadd" || bad "qemu image tail is not vecadd"

# Fixtures: little-endian uint32 words; expect = (a + b) mod 2^32, computed with Wolfram (see README).
for f in a b expect; do xxd -r -p $R/$f.hex > "$B/$f.bin"; done
N=$(grep -c . $R/a.hex)
for f in a b expect; do
    [ "$(wc -c < "$B/$f.bin")" -eq $((4 * N)) ] || bad "$f.hex does not hold $N words"
done
say "FIXTURE n=$N a=$(sha "$B/a.bin") b=$(sha "$B/b.bin") expect=$(sha "$B/expect.bin")"
xxd -r -p $R/mix-expect.hex > "$B/mix-expect.bin"
[ "$(wc -c < "$B/mix-expect.bin")" -eq $((4 * N)) ] || bad "mix-expect.hex does not hold $N words"

# ---- 1b. seeded Omega path: omc builds irrv32.om; irrv32 lifts a scalar unit's OSC IR v1 ----
# into the elementwise RV32 loop. The kernel bytes come from Omega; the checkers stay oracles.
omir() { { printf 'ir\n'; cat "$1"; } | nice -n 19 "$OMC" > "$2" 2> "$2.err"; }
OM_OK=
if [ ! -x "$OMC" ]; then
    nr "seeded Omega path: $OMC absent"
elif [ "$(sha "$OMC")" != "$OMC_PIN" ]; then
    bad "compiler $OMC pin differs"
elif ! nice -n 19 "$OMC" < $R/irrv32.om > "$B/irrv32" 2> "$B/irrv32.err"; then
    bad "omc refused irrv32.om: $(cat "$B/irrv32.err")"
else
    nice -n 19 "$OMC" < $R/irrv32.om > "$B/irrv32.again"
    cmp -s "$B/irrv32" "$B/irrv32.again" && ok "determinism omc irrv32.om ($(wc -c < "$B/irrv32") bytes, sha256=$(sha "$B/irrv32"))" ||
        bad "determinism: two omc builds of irrv32.om differ"
    chmod +x "$B/irrv32"
    OM_OK=1
    for u in vecadd mix; do
        if omir $R/$u.unit.om "$B/$u.ir" && "$B/irrv32" < "$B/$u.ir" > "$B/om-$u.bin" 2> "$B/om-$u.err"; then
            "$B/irrv32" < "$B/$u.ir" > "$B/om-$u.again.bin"
            cmp -s "$B/om-$u.bin" "$B/om-$u.again.bin" && ok "determinism irrv32 $u.unit.om ($(wc -c < "$B/om-$u.bin") bytes, sha256=$(sha "$B/om-$u.bin"), IR sha256=$(sha "$B/$u.ir"))" ||
                bad "determinism irrv32 $u: two emissions differ"
            dd if="$B/tt.bin" bs=4 count=9 2>/dev/null | cat - "$B/om-$u.bin" > "$B/om-$u-tt.bin"
            dd if="$B/qemu.bin" bs=4 count=15 2>/dev/null | cat - "$B/om-$u.bin" > "$B/om-$u-qemu.bin"
        else
            bad "irrv32 path refused $u.unit.om: $(cat "$B/$u.ir.err" "$B/om-$u.err" 2>/dev/null)"
            OM_OK=
        fi
    done
    [ -n "$OM_OK" ] && { cmp -s "$B/om-vecadd.bin" $R/vecadd.bin && ok "irrv32 vecadd output is byte-identical to the asm2 vecadd.bin" ||
        bad "irrv32 vecadd differs from asm2 vecadd.bin: $(first_diff "$B/om-vecadd.bin" $R/vecadd.bin)"; }
fi

# ---- 2. two independent disassemblers ----
dis_gnu() { "$GNU" -D -b binary -m riscv:rv32 -M no-aliases,numeric "$1" | grep -E '^ +[0-9a-f]+:' | cut -f3- | sed 's/ *#.*//; s/[[:space:]]*$//'; }
dis_llvm() {
    "$OBJCOPY" -I binary -O elf32-littleriscv -B riscv --rename-section .data=.text,code,alloc,load,contents "$1" "$1.o" &&
        "$LLVM" -d -M no-aliases -M numeric --mattr=+m "$1.o" | grep -E '^ +[0-9a-f]+:' | cut -f2- | sed 's/ <.*//; s/[[:space:]]*$//'
}
# mnemonic followed by its register operands, in order (immediates are printed in different bases)
regs() { tr ',()' '   ' | awk '{ s = $1; for (i = 2; i <= NF; i++) if ($i ~ /^x[0-9]+$/) s = s " " $i; print s }'; }
if have "$GNU"; then
    say "ORACLE gnu $("$GNU" --version | sed -n 1p)"
    for p in vecadd edges; do
        dis_gnu "$B/$p.bin" > "$B/$p.gnu"
        diff "$B/$p.gnu" $R/$p.expect > "$B/$p.gnu.diff" && ok "GNU objdump $p: $(wc -l < "$B/$p.gnu") words match $p.expect (written by hand)" ||
            bad "GNU objdump $p differs from $p.expect: $(sed -n 2,3p "$B/$p.gnu.diff" | tr '\n' ' ')"
    done
    grep '^  ' $R/edges.s2 | awk '{ print $1 }' > "$B/edges.names"
    cut -f1 "$B/edges.gnu" | diff - "$B/edges.names" > /dev/null && ok "edges.s2 covers every table entry: $(sort -u "$B/edges.names" | wc -l) of $(grep -c '^= ' "$TAB") names" ||
        bad "GNU mnemonics differ from the edges.s2 names"
    [ "$(sort -u "$B/edges.names" | wc -l)" -eq "$(grep -c '^= ' "$TAB")" ] || bad "edges.s2 misses a table entry"
else
    nr "GNU objdump ($GNU) absent"
fi
if have "$GNU" && [ -n "$OM_OK" ]; then
    dis_gnu "$B/om-mix.bin" > "$B/om-mix.gnu"
    diff "$B/om-mix.gnu" $R/mix.expect > "$B/om-mix.gnu.diff" && ok "GNU objdump om-mix (CONST lui+addi, MOV, mul, xor): $(wc -l < "$B/om-mix.gnu") words match mix.expect (written by hand)" ||
        bad "GNU objdump om-mix differs from mix.expect: $(sed -n 2,3p "$B/om-mix.gnu.diff" | tr '\n' ' ')"
fi
if have "$LLVM" && have "$OBJCOPY" && have "$GNU"; then
    say "ORACLE llvm $("$LLVM" --version | grep -i 'llvm version' | sed 's/^ *//')"
    for p in vecadd edges; do
        dis_llvm "$B/$p.bin" > "$B/$p.llvm" || bad "LLVM objdump failed on $p"
        regs < "$B/$p.llvm" > "$B/$p.llvm.regs"
        regs < "$B/$p.gnu" > "$B/$p.gnu.regs"
        cmp -s "$B/$p.llvm.regs" "$B/$p.gnu.regs" && ok "LLVM objdump $p: mnemonics and registers equal GNU's on all $(wc -l < "$B/$p.llvm") words" ||
            bad "LLVM and GNU disagree on $p"
    done
else
    nr "LLVM objdump ($LLVM) or objcopy ($OBJCOPY) absent"
fi

# ---- 3. execution oracles ----
ttsim_run() { # arch image out
    timeout 600 nice -n 19 "$PY" $R/oracle.py ttsim "$TTSIM_DIR/libttsim_$1_aarch64.so" "$1" "$2" \
        "$B/a.bin" "$B/b.bin" "$N" "$3" > "$3.log" 2>&1
}
qemu_run() { # image out
    "$PY" $R/oracle.py elf "$1" "$B/a.bin" "$B/b.bin" "$N" "$1.elf" && chmod +x "$1.elf" &&
        timeout 60 nice -n 19 "$QEMU" "$1.elf" > "$2" 2> "$2.err"
}
ttsim_ok() { # arch: 0 when the pinned library is present
    lib=$TTSIM_DIR/libttsim_$1_aarch64.so
    eval "pin=\$TTSIM_$(echo "$1" | tr a-z A-Z)_PIN"
    [ -f "$lib" ] && [ "$(sha "$lib")" = "$pin" ]
}
have "$PY" || nr "Python ($PY) absent; ttsim and qemu oracles skipped"
TT_DONE=
for arch in wh bh; do
    if have "$PY" && ttsim_ok $arch; then
        say "ORACLE ttsim $TTSIM_VERSION libttsim_${arch}_aarch64.so sha256=$(sha "$TTSIM_DIR/libttsim_${arch}_aarch64.so") driver oracle.py sha256=$(sha $R/oracle.py) $("$PY" --version 2>&1)"
        if ttsim_run $arch "$B/tt.bin" "$B/out-$arch.bin" && cmp -s "$B/out-$arch.bin" "$B/expect.bin"; then
            ok "ttsim $arch: $N of $N sums equal expect ($(grep '^ttsim' "$B/out-$arch.bin.log"))"
            TT_DONE=$arch
        else
            bad "ttsim $arch: $(tail -n 1 "$B/out-$arch.bin.log") $(first_diff "$B/out-$arch.bin" "$B/expect.bin")"
        fi
    else
        nr "ttsim $arch: $TTSIM_DIR/libttsim_${arch}_aarch64.so absent or not the pinned $TTSIM_VERSION build"
    fi
done
Q_DONE=
if have "$PY" && have "$QEMU"; then
    say "ORACLE qemu $("$QEMU" --version | sed -n 1p)"
    if qemu_run "$B/qemu.bin" "$B/out-qemu.bin" && cmp -s "$B/out-qemu.bin" "$B/expect.bin"; then
        ok "qemu-riscv32: $N of $N sums equal expect"
        Q_DONE=1
    else
        bad "qemu-riscv32: $(first_diff "$B/out-qemu.bin" "$B/expect.bin") $(cat "$B/out-qemu.bin.err" 2>/dev/null)"
    fi
else
    nr "qemu-riscv32 ($QEMU) absent"
fi
# Seeded Omega kernels: the same boot stubs followed by irrv32's output.
if [ -n "$OM_OK" ]; then
    for u in vecadd mix; do
        want=$B/expect.bin
        [ $u = mix ] && want=$B/mix-expect.bin
        for arch in wh bh; do
            if have "$PY" && ttsim_ok $arch; then
                if ttsim_run $arch "$B/om-$u-tt.bin" "$B/om-$u-$arch.out" && cmp -s "$B/om-$u-$arch.out" "$want"; then
                    ok "ttsim $arch on Omega-emitted $u: $N of $N words equal $(basename "$want") ($(grep '^ttsim' "$B/om-$u-$arch.out.log"))"
                else
                    bad "ttsim $arch on Omega-emitted $u: $(tail -n 1 "$B/om-$u-$arch.out.log") $(first_diff "$B/om-$u-$arch.out" "$want")"
                fi
            else
                nr "ttsim $arch on Omega-emitted $u (library absent)"
            fi
        done
        if have "$PY" && have "$QEMU"; then
            if qemu_run "$B/om-$u-qemu.bin" "$B/om-$u-qemu.out" && cmp -s "$B/om-$u-qemu.out" "$want"; then
                ok "qemu-riscv32 on Omega-emitted $u: $N of $N words equal $(basename "$want")"
            else
                bad "qemu-riscv32 on Omega-emitted $u: $(first_diff "$B/om-$u-qemu.out" "$want") $(cat "$B/om-$u-qemu.out.err" 2>/dev/null)"
            fi
        else
            nr "qemu-riscv32 on Omega-emitted $u (absent)"
        fi
    done
fi

# ---- 4. negative controls: each mutation must be caught ----
mutate() { # sed-expr in out: the edit must change the file
    sed "$1" "$2" > "$3"
    ! cmp -s "$2" "$3" || { bad "control edit '$1' did not change $2"; return 1; }
}
# C1: one bit flipped in the table (add's funct7 bit 30: add becomes sub).
if mutate 's/^= add     0x00000033/= add     0x40000033/' "$TAB" "$B/c1.tbl"; then
    cat "$B/c1.tbl" $R/vecadd.s2 | "$ASM" > "$B/c1.bin"
    if have "$GNU"; then
        dis_gnu "$B/c1.bin" | cmp -s - $R/vecadd.expect && bad "C1 table bit flip not seen by GNU objdump" ||
            ok "C1 table bit flip (add->sub) caught by GNU objdump"
    fi
    if [ -n "$TT_DONE" ]; then
        cat "$B/c1.tbl" $R/boot-tt.s2 $R/vecadd.s2 | "$ASM" > "$B/c1-tt.bin"
        ttsim_run "$TT_DONE" "$B/c1-tt.bin" "$B/c1-out.bin"
        cmp -s "$B/c1-out.bin" "$B/expect.bin" && bad "C1 table bit flip not seen by ttsim $TT_DONE" ||
            ok "C1 table bit flip caught by ttsim $TT_DONE ($(first_diff "$B/c1-out.bin" "$B/expect.bin"))"
    else
        nr "C1 on ttsim (no ttsim run)"
    fi
fi
# C2: one wrong expected value (last word) must fail against the real ttsim/qemu output.
sed '$ s/^../00/' $R/expect.hex | xxd -r -p > "$B/c2-expect.bin"
GOOD=
[ -n "$TT_DONE" ] && GOOD=$B/out-$TT_DONE.bin
[ -z "$GOOD" ] && [ -n "$Q_DONE" ] && GOOD=$B/out-qemu.bin
if [ -n "$GOOD" ]; then
    cmp -s "$GOOD" "$B/c2-expect.bin" && bad "C2 wrong expected value accepted" ||
        ok "C2 wrong expected value caught ($(first_diff "$GOOD" "$B/c2-expect.bin"))"
else
    nr "C2 (no executed output)"
fi
# C3: wrong register in the listing (add t2,t0,t0) must change qemu's output.
if mutate 's/^  add 7 5 6 /  add 7 5 5 /' $R/vecadd.s2 "$B/c3.s2"; then
    if [ -n "$Q_DONE" ]; then
        asm $R/boot-qemu.s2 "$B/c3.s2" > "$B/c3.bin"
        qemu_run "$B/c3.bin" "$B/c3-out.bin"
        cmp -s "$B/c3-out.bin" "$B/expect.bin" && bad "C3 wrong register not seen by qemu" ||
            ok "C3 wrong register caught by qemu-riscv32 ($(first_diff "$B/c3-out.bin" "$B/expect.bin"))"
    else
        nr "C3 (no qemu run)"
    fi
fi
# C4: one branch-offset piece wrong (bne imm[11]) must stop ttsim from finishing correctly.
if mutate 's/^  bne 13 0 -1 63 0 1 /  bne 13 0 -1 63 0 0 /' $R/vecadd.s2 "$B/c4.s2"; then
    if [ -n "$TT_DONE" ]; then
        asm $R/boot-tt.s2 "$B/c4.s2" > "$B/c4.bin"
        if ttsim_run "$TT_DONE" "$B/c4.bin" "$B/c4-out.bin" && cmp -s "$B/c4-out.bin" "$B/expect.bin"; then
            bad "C4 wrong branch offset not seen by ttsim $TT_DONE"
        else
            ok "C4 wrong branch offset caught by ttsim $TT_DONE ($(tail -n 1 "$B/c4-out.bin.log"))"
        fi
    else
        nr "C4 (no ttsim run)"
    fi
fi
# C5: an immediate outside its 12-bit field must be refused by asm2.
if mutate 's/^  lw 5 10 0 /  lw 5 10 2048 /' $R/vecadd.s2 "$B/c5.s2"; then
    asm "$B/c5.s2" > "$B/c5.bin" 2> "$B/c5.err" && bad "C5 out-of-range immediate accepted by asm2" ||
        ok "C5 out-of-range immediate refused by asm2 ($(sed -n 1p "$B/c5.err"))"
fi
# C6: the scalar unit mutated (add -> sub) must change irrv32's bytes and ttsim's output.
if [ -n "$OM_OK" ] && mutate 's/^ret add a b$/ret sub a b/' $R/vecadd.unit.om "$B/c6.unit.om"; then
    omir "$B/c6.unit.om" "$B/c6.ir" && "$B/irrv32" < "$B/c6.ir" > "$B/c6.bin"
    cmp -s "$B/c6.bin" $R/vecadd.bin && bad "C6 mutated unit gave the vecadd bytes" ||
        ok "C6 mutated unit (add->sub) changes irrv32 output ($(first_diff "$B/c6.bin" $R/vecadd.bin))"
    if [ -n "$TT_DONE" ]; then
        dd if="$B/tt.bin" bs=4 count=9 2>/dev/null | cat - "$B/c6.bin" > "$B/c6-tt.bin"
        ttsim_run "$TT_DONE" "$B/c6-tt.bin" "$B/c6-out.bin"
        cmp -s "$B/c6-out.bin" "$B/expect.bin" && bad "C6 mutated unit not seen by ttsim $TT_DONE" ||
            ok "C6 mutated unit caught by ttsim $TT_DONE ($(first_diff "$B/c6-out.bin" "$B/expect.bin"))"
    else
        nr "C6 on ttsim (no ttsim run)"
    fi
elif [ -z "$OM_OK" ]; then
    nr "C6 (seeded Omega path did not run)"
fi
# C7: the emitter mutated (pointer a0 stride 4 -> 8) must change the kernel that qemu runs.
if [ -n "$OM_OK" ] && mutate 's/^itype 4 10 0 10 19$/itype 8 10 0 10 19/' $R/irrv32.om "$B/c7.om"; then
    if "$OMC" < "$B/c7.om" > "$B/c7-irrv32" && chmod +x "$B/c7-irrv32" && "$B/c7-irrv32" < "$B/vecadd.ir" > "$B/c7.bin"; then
        if [ -n "$Q_DONE" ]; then
            dd if="$B/qemu.bin" bs=4 count=15 2>/dev/null | cat - "$B/c7.bin" > "$B/c7-qemu.bin"
            qemu_run "$B/c7-qemu.bin" "$B/c7-out.bin"
            cmp -s "$B/c7-out.bin" "$B/expect.bin" && bad "C7 mutated emitter not seen by qemu" ||
                ok "C7 mutated emitter (stride 8) caught by qemu-riscv32 ($(first_diff "$B/c7-out.bin" "$B/expect.bin"))"
        else
            nr "C7 (no qemu run)"
        fi
    else
        bad "C7 mutated emitter did not build or run"
    fi
elif [ -z "$OM_OK" ]; then
    nr "C7 (seeded Omega path did not run)"
fi
# C8: a unit irrv32 cannot lower faithfully (div traps on zero in Omega) must be refused, no bytes out.
if [ -n "$OM_OK" ] && mutate 's/^ret add a b$/ret div a b/' $R/vecadd.unit.om "$B/c8.unit.om"; then
    omir "$B/c8.unit.om" "$B/c8.ir"
    "$B/irrv32" < "$B/c8.ir" > "$B/c8.bin" 2> "$B/c8.err"
    rc=$?
    [ "$rc" -ne 0 ] && [ ! -s "$B/c8.bin" ] && ok "C8 div unit refused by irrv32 (exit $rc, $(cat "$B/c8.err"), 0 bytes)" ||
        bad "C8 div unit not refused (exit $rc, $(wc -c < "$B/c8.bin") bytes)"
elif [ -z "$OM_OK" ]; then
    nr "C8 (seeded Omega path did not run)"
fi

sha256sum $R/* "$ASM" "$OMC" > "$B/SHA256SUMS" 2>/dev/null
if [ "$fails" -gt 0 ]; then
    say "RESULT FAIL ($fails failed, $notrun not run)"
    exit 1
fi
if [ "$notrun" -gt 0 ]; then
    say "RESULT NOT_RUN ($notrun checks could not run; nothing failed)"
    exit 3
fi
say "RESULT PASS (interop oracles, not Omega-native; no Tenstorrent hardware was used)"
exit 0
