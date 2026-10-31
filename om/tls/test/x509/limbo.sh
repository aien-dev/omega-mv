#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# C2SP x509-limbo against om/tls/x509.om. Usage: sh om/tls/test/x509/limbo.sh OMC [LIMBO_DIR]
# LIMBO_DIR is a clone of https://github.com/C2SP/x509-limbo (cloned into build/ when absent) and is
# checked against the pinned commit below. gen_limbo.py turns limbo.json into driver R records,
# driver.om runs them, limbo.py compares with the expected results; every disagreement must be
# listed in limbo_allow.txt with its reason. Final line "LIMBO PASS|FAIL cases=.. agree=.. ...".
set -eu
omc=${1:?usage: limbo.sh OMC [LIMBO_DIR]}
here=$(cd "$(dirname "$0")" && pwd)
tls=$(cd "$here/../.." && pwd)
root=$(cd "$tls/../.." && pwd)
pin=$(sed -n 's/^OMC_PIN=//p' "$root/om/units.sh")
limbo_pin=1080e565f26e544bbe9cf99953be8a57c08e7e20
out="$root/build/tls/x509/limbo"
src=${2:-$root/build/tls/x509/limbo-src}
run() { nice -n 19 ionice -c3 "$@"; }

[ "$(sha256sum < "$omc" | cut -c1-64)" = "$pin" ] || { echo "LIMBO FAIL omc is not the pinned compiler"; exit 1; }
[ -d "$src/.git" ] || git clone -q https://github.com/C2SP/x509-limbo "$src"
head=$(git -C "$src" rev-parse HEAD)
case $limbo_pin in "$head"*) ;; *)
	git -C "$src" fetch -q origin "$limbo_pin" 2>/dev/null || true
	git -C "$src" -c advice.detachedHead=false checkout -q "$limbo_pin" || { echo "LIMBO FAIL limbo not at $limbo_pin"; exit 1; }
	head=$(git -C "$src" rev-parse HEAD) ;;
esac
rm -rf "$out"
mkdir -p "$out"
python3 "$here/gen_limbo.py" "$src/limbo.json" "$out" > /dev/null
{ echo om2; cat "$tls/sha2.om" "$tls/hkdf.om" "$tls/ec.om" "$tls/rsa.om" "$tls/x509.om" "$here/driver.om"; } > "$out/driver.om"
if ! run "$omc" < "$out/driver.om" > "$out/driver" 2> "$out/driver.err" || [ -s "$out/driver.err" ]; then
	echo "LIMBO FAIL omc refused driver.om: $(cat "$out/driver.err")"
	exit 1
fi
chmod +x "$out/driver"
rc=0
(cd "$out" && run ./driver < cases.txt > results.txt) || rc=$?
echo "limbo $head driver $(sha256sum < "$out/driver" | cut -c1-64) exit $rc"
[ "$rc" -eq 0 ] || { echo "LIMBO FAIL driver exit $rc (a trap is a failure)"; exit 1; }
python3 "$here/limbo.py" "$out/meta.tsv" "$out/results.txt" "$here/limbo_allow.txt" | tee "$out/judge.txt" | grep -v '^ *[0-9]* explained'
tail -1 "$out/judge.txt" | grep -q '^LIMBO PASS'
