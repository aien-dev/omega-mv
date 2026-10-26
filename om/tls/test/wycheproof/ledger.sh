#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Wycheproof ledger for om/tls: accounts for every test of every testvectors_v1 file at the pinned
# C2SP/wycheproof commit, per file and per primitive, as pass, fail, na (not applicable, with the
# written reason), not_run (applicable, but the suite or its log is absent) or unexplained.
# Usage: sh om/tls/test/wycheproof/ledger.sh WYCHEPROOF_DIR LOGDIR
#   WYCHEPROOF_DIR  a C2SP/wycheproof clone at $pin
#   LOGDIR          holds SUITE.log, the stdout+stderr of `sh om/tls/test/SUITE/run.sh OMC`, for the
#                   suites named in ledger.tsv (aesgcm chacha sha2 x25519 ec rsa mlkem ecdh); the
#                   driver outputs those runs left in build/tls/SUITE/ are read for MISMATCH lines.
# Rows come from ledger.tsv (one per JSON file; tab separated):
#   json primitive suite label vectors selector not_run_file reason
# A test counts as run when its record is in the committed conversion `vectors` (lines matching the
# awk regex `selector`, comments excluded; the conversion header must carry the JSON's sha256). It
# passes when the suite log holds "PASS label:", fails when it holds "FAIL label" (one failure per
# MISMATCH line on a selected record; every run record when the driver left none). Tests not run are
# na when not_run_file lists them (per-file "tests=N", per-flag ": N", or a whole-file line) or the
# row gives a reason; anything left is unexplained.
# Exit 0 PASS: every test is pass or na. Exit 1 FAIL: a failure, an unexplained test or a ledger
# error. Exit 3 NOT_RUN: no failure, but some applicable tests did not run here.
set -eu
pin=d17cc89fa5c8f4f4d378b5385ebf75da5d0926c9
wyc=${1:?usage: ledger.sh WYCHEPROOF_DIR LOGDIR}
logs=${2:?usage: ledger.sh WYCHEPROOF_DIR LOGDIR}
here=$(cd "$(dirname "$0")" && pwd)
tests=$(cd "$here/.." && pwd)
root=$(cd "$here/../../../.." && pwd)
build="$root/build/tls"
tsv="$here/ledger.tsv"
tv="$wyc/testvectors_v1"

got=$(git -C "$wyc" rev-parse HEAD 2> /dev/null || echo none)
if [ "$got" != "$pin" ]; then
	echo "WYCHEPROOF-LEDGER NOT_RUN: $wyc is at $got, not $pin"
	exit 3
fi
echo "wycheproof $pin"
echo "ledger.tsv sha256 $(sha256sum "$tsv" | cut -d' ' -f1)"

err=0
# every JSON file has exactly one row, every row names an existing file
ls "$tv" | grep '\.json$' | sort > "${TMPDIR:-/tmp}/ledger-files.$$"
grep -v '^#' "$tsv" | cut -f1 | sort > "${TMPDIR:-/tmp}/ledger-rows.$$"
missing=$(comm -23 "${TMPDIR:-/tmp}/ledger-files.$$" "${TMPDIR:-/tmp}/ledger-rows.$$")
extra=$(comm -13 "${TMPDIR:-/tmp}/ledger-files.$$" "${TMPDIR:-/tmp}/ledger-rows.$$" | sort | uniq)
dup=$(uniq -d "${TMPDIR:-/tmp}/ledger-rows.$$")
rm -f "${TMPDIR:-/tmp}/ledger-files.$$" "${TMPDIR:-/tmp}/ledger-rows.$$"
for f in $missing; do echo "ERROR no ledger row for $f"; err=1; done
for f in $extra; do echo "ERROR ledger row for absent file $f"; err=1; done
for f in $dup; do echo "ERROR duplicate ledger row $f"; err=1; done

# sum of the not-run counts a NOT_RUN file gives for one JSON (TOTAL when a line names the file
# without a count), or nothing when no line names it
nr_count() {
	awk -v j="$1" -v t="$2" '
		/^#/ { next }
		index($0, j) {
			found = 1
			if (match($0, /tests=[0-9]+/)) { n += substr($0, RSTART + 6, RLENGTH - 6); next }
			if (match($0, /: [0-9]+$/)) { n += substr($0, RSTART + 2); next }
			whole = 1
		}
		END { if (whole) print t; else if (found) print n }' "$3"
}

# failures among selected records: MISMATCH lines of OUT whose line number is a selected record
mismatches() {
	awk -v sel="$1" 'FNR == NR { if ($0 !~ /^#/ && $0 ~ sel) ok[FNR] = 1; next }
		$1 == "MISMATCH" && $2 == "line" && ($3 + 0) in ok { n++ }
		END { print n + 0 }' "$2" "$3"
}

printf '%-52s %-20s %6s %6s %5s %6s %7s %5s  %s\n' file primitive total pass fail na not_run unexp note
tab=$(printf '\t')
summary="${TMPDIR:-/tmp}/ledger-sum.$$"
: > "$summary"
grep -v '^#' "$tsv" | while IFS="$tab" read -r json prim suite label vec sel nrf reason; do
	f="$tv/$json"
	[ -f "$f" ] || continue
	total=$(grep -o '"tcId"' "$f" | wc -l | tr -d ' ')
	declared=$(grep -m 1 '"numberOfTests"' "$f" | tr -dc 0-9)
	note=""
	if [ "$total" != "$declared" ]; then
		echo "ERROR $json: $total tcId entries, numberOfTests $declared"
		echo "ERR" >> "$summary"
		continue
	fi
	run=0 pass=0 fail=0 na=0 notrun=0
	present=yes
	if [ "$vec" != "-" ]; then
		if [ ! -f "$tests/$vec" ]; then
			present=no
			note="suite $suite not in this tree ($vec)"
		else
			sum=$(sha256sum "$f" | cut -d' ' -f1)
			if ! grep '^#' "$tests/$vec" | grep -q "$sum"; then
				echo "ERROR $json: $vec header does not carry sha256 $sum"
				echo "ERR" >> "$summary"
				continue
			fi
			run=$(awk -v sel="$sel" '$0 !~ /^#/ && $0 ~ sel { n++ } END { print n + 0 }' "$tests/$vec")
		fi
	fi
	if [ "$nrf" != "-" ] && [ -f "$tests/$nrf" ]; then
		na=$(nr_count "$json" "$total" "$tests/$nrf")
		na=${na:-0}
		[ "$na" -gt 0 ] && note="${note:+$note; }na per $nrf"
	elif [ "$nrf" != "-" ]; then
		note="${note:+$note; }$nrf not in this tree"
	elif [ "$reason" != "-" ]; then
		na=$((total - run))
		note="${note:+$note; }$reason"
	fi
	if [ "$present" = no ]; then
		# the conversion is absent: everything not listed na is applicable but not run here
		notrun=$((total - na))
		[ "$notrun" -lt 0 ] && notrun=0
		run=0
	else
		log="$logs/$suite.log"
		if [ "$run" -eq 0 ]; then
			:
		elif [ ! -f "$log" ]; then
			notrun=$run
			note="${note:+$note; }no log $suite.log"
		elif grep -q "^PASS $label:" "$log"; then
			pass=$run
		elif grep -q "^FAIL $label[ :]" "$log"; then
			out=""
			for c in "$build/$suite/$label.out" "$build/$suite/$label.log"; do [ -f "$c" ] && out=$c && break; done
			m=0
			[ -n "$out" ] && m=$(mismatches "$sel" "$tests/$vec" "$out")
			[ "$m" -eq 0 ] && m=$run
			fail=$m
			pass=$((run - m))
			note="${note:+$note; }FAIL in $suite.log${out:+, see $out}"
		else
			notrun=$run
			note="${note:+$note; }$suite.log has no PASS/FAIL line for $label"
		fi
	fi
	unexp=$((total - pass - fail - na - notrun))
	if [ "$unexp" -lt 0 ]; then
		echo "ERROR $json: run $run + na $na exceeds total $total"
		echo "ERR" >> "$summary"
		continue
	fi
	printf '%-52s %-20s %6d %6d %5d %6d %7d %5d  %s\n' "$json" "$prim" "$total" "$pass" "$fail" "$na" "$notrun" "$unexp" "$note"
	printf '%s %d %d %d %d %d %d\n' "$prim" "$total" "$pass" "$fail" "$na" "$notrun" "$unexp" >> "$summary"
done

echo
echo "per primitive:"
grep -v '^ERR$' "$summary" | awk '{ t[$1] += $2; p[$1] += $3; f[$1] += $4; a[$1] += $5; r[$1] += $6; u[$1] += $7; n[$1]++ }
	END { for (k in t) printf "  %-20s files=%-3d total=%-6d pass=%-6d fail=%-4d na=%-6d not_run=%-5d unexplained=%d\n", k, n[k], t[k], p[k], f[k], a[k], r[k], u[k] }' | sort
errs=$(grep -c '^ERR$' "$summary" || true)
set -- $(grep -v '^ERR$' "$summary" | awk '{ n++; t += $2; p += $3; f += $4; a += $5; r += $6; u += $7 } END { print n + 0, t + 0, p + 0, f + 0, a + 0, r + 0, u + 0 }')
rm -f "$summary"
[ "$errs" -eq 0 ] || err=1
line="files=$1 tests=$2 pass=$3 fail=$4 na=$5 not_run=$6 unexplained=$7 errors=$errs"
if [ "$err" -ne 0 ] || [ "$4" -ne 0 ] || [ "$7" -ne 0 ]; then
	echo "WYCHEPROOF-LEDGER FAIL $line"
	exit 1
fi
if [ "$6" -ne 0 ]; then
	echo "WYCHEPROOF-LEDGER NOT_RUN $line"
	exit 3
fi
echo "WYCHEPROOF-LEDGER PASS $line"
