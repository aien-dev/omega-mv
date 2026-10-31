#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Gate for seed/equiv/irequiv.om (issue 826): exhaustive u8/u16 equivalence of
# OSC IR v1 scalar functions. Host CPU only; no C participates in checking.
set -eu
cd "$(dirname "$0")/../.."
[ "$(uname -m)" = aarch64 ] || { echo "SKIP gate-irequiv: needs aarch64 (omc emits AArch64)"; exit 0; }
OMC=seed/out/omc
PIN=078200466af2dc0452da7793b186d91bc9539f0cf5fafa15bcdb2a48e3bedef7
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

# Conformance with native code: every row of the irwasm/irspirv tables is run as a
# real native program (the dumper of seed/wasm/gate-irwasm.sh, all 64 result bits)
# and through `irequiv eval` on the same unit's IR. Values must be bit-identical and
# traps must carry the same code; the native exit must also match the table.
for source in tests/one_layer/omegese/dev/*.omegese seed/wasm/*.unit.om seed/spirv/*.unit.om; do
	id=$(basename "$source" .omegese)
	id=${id%.unit.om}
	printf '%s\n' "$source" > "$B/nx-$id.source"
	{ printf 'ir\n'; cat "$source"; } | "$OMC" > "$B/nx-$id.ir" || bad "ir $source"
done
tab=$(printf '\t')
nx_rows=0
nx_equal=0
conform() {
	table=$1; want=$2; rows=0
	while IFS="$tab" read -r unit function result typed status; do
		case "$unit" in ''|'#'*) continue ;; esac
		rows=$((rows + 1)); nx_rows=$((nx_rows + 1))
		om=$B/nx-row-$nx_rows.om
		cat "$(cat "$B/nx-$unit.source")" > "$om"
		cat >> "$om" <<'DUMP'
global dumpbuf uchar 8
fn ir_dump x ulong to void
for j ulong 8
put dumpbuf j uchar and shr x mul j 8 255
end
p = ulong 0
while lt p 8 bound 8
k = write 1 dumpbuf p sub ulong 8 p
if le k 0
exit 90
end
p = add p ulong k
end
end
fn main to int
DUMP
		actual=; eargs=
		if [ "$typed" != - ]; then
			for a in $(printf '%s' "$typed" | tr ',' ' '); do
				ty=${a%%:*}; v=${a#*:}
				if [ "$ty" = bool ]; then actual="$actual $v"; else actual="$actual $ty $v"; fi
				eargs="$eargs $a"
			done
		fi
		case "$result" in
			bool|void|byte|short|int|long|uchar|ushort|uint|ulong) ;;
			*) bad "$table row $rows: unknown result $result"; continue ;;
		esac
		case "$result" in
			bool) printf 'if %s %s\nir_dump ulong 1\nelse\nir_dump ulong 0\nend\n' "$function" "$actual" ;;
			void) printf '%s %s\nir_dump ulong 0\n' "$function" "$actual" ;;
			byte|short|int|long) printf 'ir_dump bitcast ulong long %s %s\n' "$function" "$actual" ;;
			uchar|ushort|uint|ulong) printf 'ir_dump ulong %s %s\n' "$function" "$actual" ;;
		esac >> "$om"
		printf 'ret 0\nend\n' >> "$om"
		"$OMC" < "$om" > "$B/nx-row-$nx_rows" || { bad "$table row $rows: native compile"; continue; }
		chmod +x "$B/nx-row-$nx_rows"
		set +e
		timeout 10 "$B/nx-row-$nx_rows" > "$B/nx-row-$nx_rows.out"; rc=$?
		ir=$("$B/irequiv" eval "$function" $eargs < "$B/nx-$unit.ir" 2>&1); st=$?
		set -e
		[ "$rc" = "$status" ] || bad "$table row $rows: native exit $rc, table $status"
		if [ "$rc" = 0 ] && [ "$(wc -c < "$B/nx-row-$nx_rows.out")" = 8 ]; then
			native="value:$(od -An -tu8 "$B/nx-row-$nx_rows.out" | tr -d ' ')"
		elif [ "$rc" -gt 100 ] && [ "$rc" -lt 200 ]; then
			native="trap:$((rc - 100))"
		else
			native="native-exit:$rc"
		fi
		if [ "$st" = 0 ] && [ "$ir" = "$native" ]; then
			nx_equal=$((nx_equal + 1))
		else
			bad "$table row $rows $unit.$function $typed: native $native, irequiv $ir (exit $st)"
		fi
	done < "$table"
	[ "$rows" = "$want" ] || bad "$table denominator $rows, want $want"
}
conform seed/wasm/cases.tsv 129
conform seed/spirv/regression.tsv 8
conform seed/spirv/demo.tsv 3
[ "$nx_equal" = 140 ] || bad "native conformance equal=$nx_equal of $nx_rows, want 140"
echo "CONFORM native rows=$nx_rows equal=$nx_equal"

if [ "$fails" = 0 ]; then echo "GATE-IREQUIV PASS"; else echo "GATE-IREQUIV FAIL ($fails)"; exit 1; fi
