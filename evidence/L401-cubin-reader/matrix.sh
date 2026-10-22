#!/bin/sh
# L401 harness + Omega cubin reader: red/green matrix with negative controls.
# Builds four harness variants from the worktree, runs each against reader variants at the quick fuzz count.
# Readers in $S/readers: build_osc.py stub|real, then make_reader_mutants.py real.osc $S/readers.
set -u
W=$HOME/workspace/omega-osc-cubin
S=$HOME/handoffs/l401-osc
FUZZ=${FUZZ:-120}
mkdir -p $S/bin $S/src $S/roots $S/logs
cd $W || exit 1
SRCS="$(ls src/compiler/*.c | grep -v oscc_main.c) src/sha256.c src/compiler/model/osc_model.c"
CF="-std=gnu11 -Wall -Wextra -Werror -D_GNU_SOURCE -O2 -Isrc -Isrc/compiler -Isrc/compiler/model"

echo "== $(date -u +%FT%TZ) commit $(git rev-parse --short HEAD) fuzz=$FUZZ"
git show HEAD:tests/compiler/test_osc_compiler.c > $S/src/h_orig.c
cp tests/compiler/test_osc_compiler.c $S/src/h_new.c
python3 - "$S/src" <<'PY'
import sys, pathlib
d = pathlib.Path(sys.argv[1]); s = (d / "h_new.c").read_text()
cap_old = '''        if (l > kl && strncmp(p, key, kl) == 0) {
            char *line = malloc(l - kl + 1), *save = NULL, *t, *res = NULL, *cellsx = NULL;'''
assert s.count(cap_old) == 1
(d / "h_mutcap.c").write_text(s.replace(cap_old, cap_old.replace("== 0) {", "== 0 && l - kl < 400) {", 1)))
hex_old = "                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return -1;"
assert s.count(hex_old) == 1
(d / "h_muthex.c").write_text(s.replace(hex_old, "                (void)c;"))
print("mutants written")
PY
for h in orig new mutcap muthex; do
  cc $CF -o $S/bin/h_$h $S/src/h_$h.c $SRCS > $S/logs/cc_$h.log 2>&1 && echo "built h_$h" || { echo "BUILD FAIL h_$h"; tail -5 $S/logs/cc_$h.log; }
done

for r in stub real mut_arch mut_fits; do
  R=$S/roots/$r; rm -rf $R; mkdir -p $R/slices
  ln -s $W/tests/compiler/progs $R/progs; ln -s $W/tests/compiler/neg $R/neg
  for f in $W/tests/compiler/slices/*.osc; do [ "$(basename $f)" = cubin_elf.osc ] || cp $f $R/slices/; done
  cp $S/readers/$r.osc $R/slices/cubin_elf.osc
done

run() { # id harness reader expectation
  t0=$(date +%s)
  $S/bin/h_$2 $FUZZ $S/roots/$3 > $S/logs/$1.out 2>&1; rc=$?
  t1=$(date +%s)
  fails=$(grep -c "cubin_elf" $S/logs/$1.out)
  echo "-- $1: harness=$2 reader=$3 want=$4 rc=$rc secs=$((t1-t0)) cubin_lines_in_log=$fails"
  grep -E "^run lines:|^run-line grammar|^external byte slices:|OSC1_COMPILER_(PASS|FAIL)" $S/logs/$1.out | sed 's/^/   /'
  grep -E "lines were skipped|run-line grammar (accepted|refused|:)|malformed run lines" $S/logs/$1.out | head -3 | cut -c1-200 | sed 's/^/   ! /'
  grep "cubin_elf" $S/logs/$1.out | head -3 | cut -c1-160 | sed 's/^/   ! /'
}
run A_orig_mutarch   orig   mut_arch PASS-vacuous
run A2_orig_real     orig   real     PASS
run B_new_mutarch    new    mut_arch FAIL
run C_new_stub       new    stub     FAIL-all
run D_new_real       new    real     PASS
run E_mutcap_real    mutcap real     FAIL-skipped
run F_muthex_real    muthex real     FAIL-grammar
run G_new_mutfits    new    mut_fits FAIL-wrap
echo "== done $(date -u +%FT%TZ)"
