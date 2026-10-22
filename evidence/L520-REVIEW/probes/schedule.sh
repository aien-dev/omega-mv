#!/bin/bash
set -eu
d=${1:?output dir}
mkdir -p "$d"
probe_dir=$(cd "$(dirname "$0")" && pwd)
quiet-build cc -std=gnu11 -O2 -Wall -Wextra -Werror -D_GNU_SOURCE -Isrc -o build/L520/block1_faults bench/m1/block1_sched.c "$probe_dir/schedule-faults.c" src/sha256.c src/omega_canonical.c src/omega_validate.c src/omega_core.c src/omega_codec.c src/aarch64_encoder.c src/aarch64_decoder.c src/omega_realize.c src/omega_realize_synth.c src/omega_program.c src/omega_machine.c src/omega_exec.c src/omega_verify.c -lm -Wl,--wrap=omega_program_eval,--wrap=omega_synthesize_for_dgx_spark,--wrap=omega_program_emit_schedule,--wrap=omega_exec_native_f3,--wrap=omega_realize_differential_inputs,--wrap=mmap,--wrap=mprotect
for f in eval-error wrong-result synth emit exec no-inputs mmap mprotect; do
 set +e
 quiet-build env L520_FAULT="$f" build/L520/block1_faults 1 1 > "$d/$f.out" 2> "$d/$f.err"
 rc=$?
 set -e
 echo "$f exit=$rc"
 test "$rc" -ne 0
done
for args in '0 1' '1 0' 'abc 1' '1 1 extra' '999999999999999999999999999 1' '1 100000001' '-1 1'; do
 set +e
 quiet-build build/L520/block1_sched $args > "$d/negative.out" 2> "$d/negative.err"
 rc=$?
 set -e
 echo "args=$args exit=$rc"
 test "$rc" -eq 2
done
awk -F, 'NR==1 {if(NF!=9)exit 1;next} {if(NF!=9||$4!="ok"||$5!=2000||$7!=2000000||$6<=0||$8<=0||$9!="PRELOAD")exit 1;counts[$2]++;rows++} END {if(rows!=20||counts["PRELOAD"]!=10||counts["SEQUENTIAL"]!=10)exit 1;print "independent CSV rows PASS 20"}' "$d/../block1.csv"
