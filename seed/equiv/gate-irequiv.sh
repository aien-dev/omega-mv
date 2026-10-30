#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Gate for seed/equiv/irequiv.om (issue 826): exhaustive u8/u16 equivalence of
# OSC IR v1 scalar functions. Host CPU only; no C participates in checking.
set -eu
cd "$(dirname "$0")/../.."
[ "$(uname -m)" = aarch64 ] || { echo "SKIP gate-irequiv: needs aarch64 (omc emits AArch64)"; exit 0; }
OMC=seed/out/omc
PIN=62775d2a391cfef7cde64e387655caa9809a267093112910ec76bfa568f3712b
[ "$(sha256sum "$OMC" | cut -d' ' -f1)" = "$PIN" ] || { echo "FAIL pin: $OMC"; exit 1; }
B=build/seed/equiv
mkdir -p "$B"
fails=0
bad() { echo "FAIL $*"; fails=$((fails + 1)); }

# The shared reader (lines 43-496) must stay byte-identical to irwasm and irspirv.
for o in seed/wasm/irwasm.om seed/spirv/irspirv.om; do
	sed -n 43,496p seed/equiv/irequiv.om > "$B/r1"
	sed -n 43,496p "$o" > "$B/r2"
	cmp -s "$B/r1" "$B/r2" || bad "reader drift vs $o"
done

"$OMC" < seed/equiv/irequiv.om > "$B/irequiv" 2> "$B/irequiv.err" || { cat "$B/irequiv.err"; echo "FAIL compile irequiv"; exit 1; }
[ -s "$B/irequiv.err" ] && bad "compile stderr not empty"
chmod +x "$B/irequiv"
for u in hexv clamp rot bitrev pop16 wide; do
	{ printf 'ir\n'; cat "seed/equiv/$u.unit.om"; } | "$OMC" > "$B/$u.ir" || bad "ir $u"
done

# expect UNIT STATUS "line substring" ARGS...
expect() {
	u=$1; want=$2; sub=$3; shift 3
	set +e
	out=$("$B/irequiv" "$@" < "$B/$u.ir" 2>&1); st=$?
	set -e
	echo "$out"
	[ "$st" = "$want" ] || bad "$u $*: status $st, want $want"
	case "$out" in *"$sub"*) ;; *) bad "$u $*: missing '$sub'";; esac
}

# Equivalent pairs: PASS over the full domain, exact counts.
expect hexv 0 "EQUIV PASS a=js_hexv b=hhexv params=uchar domain=256 checked=256 differing=0" check js_hexv hhexv
expect hexv 0 "EQUIV PASS a=js_hexv b=hexfold params=uchar domain=256 checked=256 differing=0" check js_hexv hexfold
expect clamp 0 "domain=65536 checked=65536 differing=0 both_trap=0" check clamp_add clamp_wrap
expect bitrev 0 "domain=256 checked=256 differing=0" check bitrev8 bitrev8s
expect rot 0 "domain=65536 checked=65536 differing=0 both_trap=63488" check r8 r8w
expect pop16 0 "params=ushort domain=65536 checked=65536 differing=0" check pop16 pop16s
# Negative controls: single-input mutants report that exact input and both results.
expect hexv 1 "differing=1 first=uchar:70 a_result=value:ulong:15 b_result=value:ulong:16" check js_hexv hexoff1
expect pop16 1 "differing=1 first=ushort:0 a_result=value:ushort:0 b_result=value:ushort:1" check pop16 pop16z
expect pop16 1 "differing=1 first=ushort:65535 a_result=value:ushort:16 b_result=value:ushort:15" check pop16 pop16e
# Trap codes are part of equality: value-for-trap is a difference.
expect rot 1 "differing=63488 first=uchar:0,uchar:8 a_result=trap:7 b_result=value:uchar:0" check r8 r8m
expect rot 0 "trap:7" eval r8 uchar:1 uchar:9
expect rot 0 "value:128" eval r8 uchar:1 uchar:1
# Wider domains: refused for check, sampled (exit 10, NOT_A_PROOF) otherwise.
expect wide 5 "irequiv: DOMAIN" check pop32 pop32s
expect wide 10 "EQUIV SAMPLED" sample pop32 pop32s 100000 7
expect wide 10 "NOT_A_PROOF" sample pop32 pop32s 100000 7
expect wide 1 "EQUIV FAIL" sample pop32 pop32t 100000 7
# Fuel exhaustion is INCONCLUSIVE, never PASS or FAIL.
expect wide 6 "EQUIV INCONCLUSIVE" check spin spin
# Refusals.
expect hexv 3 "UNKNOWN_FUNCTION" check js_hexv nope
expect wide 4 "SIGNATURE" check pop32 spin
expect rot 2 "USAGE" eval r8 uchar:1 ushort:1
expect rot 2 "USAGE" eval r8 uchar:256 uchar:1
expect rot 2 "USAGE" bogus r8
expect rot 7 "DUMP_TRAP" dump r8
head -c 100 "$B/pop16.ir" > "$B/trunc.ir"
set +e; "$B/irequiv" check pop16 pop16s < "$B/trunc.ir" > /dev/null 2>&1; st=$?; set -e
[ "$st" = 22 ] || bad "truncated IR: status $st, want 22"
# Full-domain dump: bitrev8 over 256 inputs is 2048 bytes and reverses 1 to 128.
"$B/irequiv" dump bitrev8 < "$B/bitrev.ir" > "$B/bitrev.dump"
[ "$(wc -c < "$B/bitrev.dump")" = 2048 ] || bad "dump size"
[ "$(od -An -tu8 -j8 -N8 "$B/bitrev.dump" | tr -d ' ')" = 128 ] || bad "dump bitrev8(1)"

if [ "$fails" = 0 ]; then echo "GATE-IREQUIV PASS"; else echo "GATE-IREQUIV FAIL ($fails)"; exit 1; fi
