#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Negative controls for ledger.sh. Each control runs ledger.sh on a scratch copy of om/tls/test (and
# build/tls) with one planted defect, and must make it exit 1:
#   row      one ledger.tsv row deleted (a JSON file is no longer accounted for)
#   fail     "FAIL <label>" appended to the first run row's suite log
#   header   the JSON sha256 in that row's conversion header replaced by zeros
# Usage: sh om/tls/test/wycheproof/ledger-nc.sh WYCHEPROOF_DIR LOGDIR
# Prints "LEDGER-NC <name> rc=N detected|MISSED" per control, then LEDGER-NC PASS|FAIL; exit 0 or 1.
set -u
wyc=${1:?usage: ledger-nc.sh WYCHEPROOF_DIR LOGDIR}
logs=${2:?usage: ledger-nc.sh WYCHEPROOF_DIR LOGDIR}
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT
# first row with a suite, a label and a committed conversion
row=$(awk -F'\t' '!/^#/ && $3 != "-" && $5 != "-" { print; exit }' "$here/ledger.tsv")
json=$(printf '%s\n' "$row" | cut -f1)
suite=$(printf '%s\n' "$row" | cut -f3)
label=$(printf '%s\n' "$row" | cut -f4)
vec=$(printf '%s\n' "$row" | cut -f5)
[ -n "$json" ] || { echo "LEDGER-NC FAIL no run row in ledger.tsv"; exit 1; }
sum=$(sha256sum "$wyc/testvectors_v1/$json" | cut -d' ' -f1)
bad=0
ctl() {
	name=$1
	rm -rf "$tmp/t"
	mkdir -p "$tmp/t/om/tls" "$tmp/t/build/tls" "$tmp/t/logs"
	cp -R "$root/om/tls/test" "$tmp/t/om/tls/"
	[ -d "$root/build/tls" ] && cp -R "$root/build/tls/." "$tmp/t/build/tls/"
	cp -R "$logs/." "$tmp/t/logs/" 2>/dev/null
	t=$tmp/t/om/tls/test
	case $name in
	row) grep -vF "$json	" "$t/wycheproof/ledger.tsv" > "$tmp/x" && cp "$tmp/x" "$t/wycheproof/ledger.tsv" ;;
	fail) echo "FAIL $label: planted by ledger-nc.sh" >> "$tmp/t/logs/$suite.log" ;;
	header) grep -q "$sum" "$t/$vec" || { echo "LEDGER-NC header: $sum not in $vec"; bad=1; return; }
		sed "s/$sum/0000000000000000000000000000000000000000000000000000000000000000/" "$t/$vec" > "$tmp/x" &&
		cp "$tmp/x" "$t/$vec" ;;
	esac
	sh "$t/wycheproof/ledger.sh" "$wyc" "$tmp/t/logs" > "$tmp/$name.out" 2>&1
	rc=$?
	if [ "$rc" -eq 1 ]; then
		echo "LEDGER-NC $name rc=$rc detected ($(tail -1 "$tmp/$name.out"))"
	else
		echo "LEDGER-NC $name rc=$rc MISSED ($(tail -1 "$tmp/$name.out"))"
		bad=1
	fi
}
echo "LEDGER-NC row=$json suite=$suite label=$label vectors=$vec"
for c in row fail header; do ctl "$c"; done
if [ "$bad" -eq 0 ]; then echo "LEDGER-NC PASS"; exit 0; fi
echo "LEDGER-NC FAIL"
exit 1
