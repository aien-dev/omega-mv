#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# NIST SP 800-90B entropy assessment of the bytes fips.om draws (Linux getrandom), with the NIST
# reference tool usnistgov/SP800-90B_EntropyAssessment (pinned commit below, built separately).
# What this grades: the Linux kernel's getrandom output, which the kernel has already conditioned
# (ChaCha20 CRNG). It is not an Omega noise source, and it does not replace an Entropy Source
# Validation (ESV): that would need a kernel/platform entropy certificate.
# usage: sh om/tls/test/fips/entropy.sh OMC EA_CPP_DIR [OUTDIR]
#   EA_CPP_DIR holds ea_non_iid, ea_iid and ea_restart built from SP800-90B_EntropyAssessment
#   87c104d0ed4cbc96103e7b8b38d6f2c7e0a6b289 (cpp/, `make`).
#   1. entropy.om writes 1,000,000 samples (one process) -> samples.bin
#   2. 1000 restarts: 1000 separate processes of 1000 samples each -> restart.bin (row dataset)
#   3. ea_non_iid and ea_iid on samples.bin (8 bits per sample)
#   4. ea_restart -n with the non-IID H_I and ea_restart -i with the IID H_I on restart.bin
# Final line: ENTROPY PASS non_iid=H iid=H iid_tests=passed restart_iid=H restart_non_iid=H, or
# ENTROPY FAIL / ENTROPY NOT_RUN (tool missing).
set -eu
omc=${1:?usage: entropy.sh OMC EA_CPP_DIR [OUTDIR]}
ea=${2:?usage: entropy.sh OMC EA_CPP_DIR [OUTDIR]}
here=$(cd "$(dirname "$0")" && pwd)
lib=$(cd "$here/../.." && pwd)
root=$(cd "$lib/../.." && pwd)
out=${3:-"$root/build/tls/entropy"}
nice="nice -n 19 ionice -c3"
for t in ea_non_iid ea_iid ea_restart; do
	[ -x "$ea/$t" ] || { echo "ENTROPY NOT_RUN: $ea/$t missing"; exit 3; }
done
mkdir -p "$out"
$nice sh "$lib/build.sh" "$omc" "$here/entropy.om" "$out/entropy" 2> "$out/build.err" || { echo "ENTROPY FAIL build: $(cat "$out/build.err")"; exit 1; }
cd "$out"
$nice ./entropy 1000000 > samples.bin
: > restart.bin
i=0
while [ "$i" -lt 1000 ]; do ./entropy 1000 >> restart.bin; i=$((i + 1)); done
[ "$(wc -c < samples.bin)" -eq 1000000 ] && [ "$(wc -c < restart.bin)" -eq 1000000 ] || { echo "ENTROPY FAIL: sample counts"; exit 1; }
sha256sum samples.bin restart.bin
$nice "$ea/ea_non_iid" -v samples.bin 8 > non_iid.txt 2>&1
$nice "$ea/ea_iid" -v samples.bin 8 > iid.txt 2>&1
hn=$(awk -F': ' '/^min\(H_original, 8 X H_bitstring\)/ { print $2 }' non_iid.txt)
hi=$(awk -F': ' '/^min\(H_original, 8 X H_bitstring\)/ { print $2 }' iid.txt)
grep -q '^\*\* Passed IID permutation tests' iid.txt && it=passed || it=failed
[ -n "$hn" ] && [ -n "$hi" ] || { echo "ENTROPY FAIL: no estimate (non_iid=$hn iid=$hi)"; exit 1; }
$nice "$ea/ea_restart" -n -v restart.bin 8 "$hn" > restart_non_iid.txt 2>&1 && rn=0 || rn=$?
$nice "$ea/ea_restart" -i -v restart.bin 8 "$hi" > restart_iid.txt 2>&1 && ri=0 || ri=$?
rhn=$(awk -F': ' '/^min\(H_r, H_c, H_I\)/ { print $2 }' restart_non_iid.txt)
rhi=$(awk -F': ' '/^min\(H_r, H_c, H_I\)/ { print $2 }' restart_iid.txt)
if [ "$rn" -eq 0 ] && [ "$ri" -eq 0 ] && [ "$it" = passed ] && [ -n "$rhn" ] && [ -n "$rhi" ] \
	&& grep -q 'Restart Sanity Check Passed' restart_non_iid.txt && grep -q 'Restart Sanity Check Passed' restart_iid.txt; then
	echo "ENTROPY PASS non_iid=$hn iid=$hi iid_tests=$it restart_iid=$rhi restart_non_iid=$rhn"
	exit 0
fi
echo "ENTROPY FAIL non_iid=$hn iid=$hi iid_tests=$it restart_iid=$rhi (rc $ri) restart_non_iid=$rhn (rc $rn)"
exit 1
