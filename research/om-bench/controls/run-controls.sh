#!/bin/sh
# om-bench (#653) negative and positive controls for the driver and for gate.om.
# Each control asserts its diagnostic text, not only an exit status.
# Usage: run-controls.sh OUTDIR   (OUTDIR must not exist)
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
BENCH=$HERE/../bench
REPO=${OMB_REPO:-$(cd "$HERE/../../.." && pwd)}
# Host-only driver regressions use synthetic subprocesses and in-memory tasks.
# They never compile or execute a candidate, call a model, or read sealed data.
# Usage: run-controls.sh --host-only (no Spark/compiler required)
if [ "${1:-}" = "--host-only" ]; then
  PYTHONDONTWRITEBYTECODE=1 python3 - "$REPO" <<'PY_HOST_CONTROLS'
import contextlib, importlib.util, io, json, os, pathlib, subprocess, sys, tempfile
ROOT = pathlib.Path(sys.argv[1]).resolve()
SCRIPT = ROOT / 'research/om-bench/run-arms.sh'
MOD = ROOT / 'research/om-bench/bench/ombench.py'
checks = 0

def check(ok, name):
    global checks
    assert ok, name
    checks += 1
    print('PASS', name)

# Exercise actual shell script with fake python3 and gate, never candidate/compiler execution.
with tempfile.TemporaryDirectory() as tmp:
    t = pathlib.Path(tmp); bin = t/'bin'; bin.mkdir(); run=t/'run'; run.mkdir()
    (bin/'python3').write_text('''#!/bin/sh
stage=$2
echo "$stage" >> "$TRACE"
if [ "$stage" = "$FAIL_STAGE" ]; then exit 17; fi
exit 0
'''); (bin/'python3').chmod(0o755)
    (run/'gate').write_text('''#!/bin/sh
echo gate >> "$TRACE"
echo "OM2GATE FAIL cases=1 pass=0 fail=1 invalid=0 not_run=0"
exit "$GATE_RC"
'''); (run/'gate').chmod(0o755)
    for stage, gate_rc, want_rc, want_trace in [
        ('gen',0,17,['gen']),('judge',0,17,['gen','judge']),
        ('gatelist',0,17,['gen','judge','gatelist']),
        ('',91,91,['gen','judge','gatelist','gate']),
        ('crosscheck',0,17,['gen','judge','gatelist','gate','crosscheck']),
        ('summary',0,17,['gen','judge','gatelist','gate','crosscheck','summary']),
        *[('',rc,0,['gen','judge','gatelist','gate','crosscheck','summary']) for rc in (0,1,2,3)],
    ]:
        trace=t/'trace'; trace.write_text('')
        env=dict(os.environ, PATH=str(bin)+':'+os.environ['PATH'], TRACE=str(trace),FAIL_STAGE=stage,GATE_RC=str(gate_rc),OMB_REPO=str(t))
        r=subprocess.run(['sh',str(SCRIPT),str(run),'dev','a'],env=env,capture_output=True,text=True)
        check(r.returncode==want_rc and trace.read_text().splitlines()==want_trace, f'shell {stage or "completed gate"} rc={gate_rc}: exit={r.returncode}')
        if want_rc:
            check(f'rc={want_rc}' in (run/'a-dev/status.txt').read_text(), 'child failure logged')
    (run/'a-dev/summary-dev.json').write_text('prior summary')
    trace.write_text('')
    r=subprocess.run(['sh',str(SCRIPT),str(run),'dev','a'],env=env,capture_output=True,text=True)
    check(r.returncode==1 and trace.read_text()=='' and (run/'a-dev/summary-dev.json').read_text()=='prior summary','runner refuses stale summary without touching it')
    trace.write_text(''); env.update(FAIL_STAGE='',GATE_RC='91')
    r=subprocess.run(['sh',str(SCRIPT),str(run),'dev','fpy'],env=env,capture_output=True,text=True)
    check(r.returncode==0 and trace.read_text().splitlines()==['gen','judge','summary'], 'foreign arm does not cross-judge')

# Import functions only, with sealed path pointed to an empty temporary directory.
with tempfile.TemporaryDirectory() as tmp:
    t=pathlib.Path(tmp); os.environ['OMB_SEALED']=str(t/'no-sealed-data')
    spec=importlib.util.spec_from_file_location('ombench', MOD); mod=importlib.util.module_from_spec(spec); spec.loader.exec_module(mod)
    check(mod.system_prompt('om').endswith((ROOT/'research/om-bench/REFERENCE-CARD.md').read_text()), 'Omega prompt loads unchanged public card')
    check(mod.system_prompt('py')=='You write complete Python 3 programs.', 'foreign prompt unchanged')
    mod.task_dirs=lambda split: (str(t),['ctl-synthetic'])
    mod.load_task=lambda split,tid: {'tid':tid,'family':'ctl-synthetic','cases':[{'n':0}]}
    row={'tid':'ctl-synthetic','family':'ctl-synthetic','code_sha256':'synthetic','class':'pass','cases':[{'n':0,'status':'pass'}]}
    vp=t/'verdicts-dev.jsonl'; log=t/'gate.log'
    def report(body, cls='pass', status='pass', code='synthetic'):
        row.update({'class':cls,'cases':[{'n':0,'status':status}],'code_sha256':code});vp.write_text(json.dumps(row)+'\n');log.write_text(body)
    good='VERDICT ctl-synthetic.00 PASS\nOM2GATE PASS cases=1 pass=1 fail=0 invalid=0 not_run=0\n'
    def cross():
        with contextlib.redirect_stdout(io.StringIO()): return mod.crosscheck(str(t),'dev',str(log))
    report(good);check(cross()==0,'completed matching gate accepted')
    report('VERDICT ctl-synthetic.00 FAIL\nOM2GATE FAIL cases=1 pass=0 fail=1 invalid=0 not_run=0\n','wrong_output','wrong_output');check(cross()==0,'wrong candidate remains failure in denominator')
    report('VERDICT ctl-synthetic.nocode NOT_RUN\nOM2GATE NOT_RUN cases=1 pass=0 fail=0 invalid=0 not_run=1\n','no_code','',None);check(cross()==0,'no-code result retained')
    report('VERDICT ctl-synthetic.00 INVALID\nOM2GATE INVALID cases=1 pass=0 fail=0 invalid=1 not_run=0\n','runtime_mechanical','timeout');check(cross()==0,'candidate timeout remains mechanical outcome')
    for body in ['',good.replace('OM2GATE PASS','OM2GATE FAIL'),good.replace('cases=1','cases=2'),good.replace('pass=1','pass=0'),good.replace('VERDICT ctl-synthetic.00 PASS\n','VERDICT ctl-synthetic.00 PASS\nVERDICT ctl-synthetic.00 PASS\n'),good.split('OM2GATE')[0],good.replace('PASS\n','FAIL\n',1),good+'garbage\n','IDENTITY MISMATCH: every case is NOT_RUN\n'+good]:
        report(body);check(cross()!=0,'incomplete/disagreeing/mismatched gate rejected')
        try:
            with contextlib.redirect_stdout(io.StringIO()): mod.summary(str(t),'dev','a')
        except SystemExit: pass
        else: raise AssertionError('summary accepted invalid gate')
        check(not (t/'summary-dev.json').exists(),'standalone summary refuses invalid gate')
    report('OM2GATE NOT_RUN cases=1 pass=0 fail=0 invalid=0 not_run=1\n','no_code','',None)
    check(cross()!=0, 'all-no-code missing verdict refused')
    report(good)
    with contextlib.redirect_stdout(io.StringIO()): mod.summary(str(t),'dev','a')
    check(json.loads((t/'summary-dev.json').read_text())['totals']['classes']=={'pass':1},'valid summary still works')
    prior=(t/'summary-dev.json').read_bytes()
    report('')
    try:
        mod.summary(str(t),'dev','a')
    except SystemExit: pass
    else: raise AssertionError('summary overwrote existing artifact')
    check((t/'summary-dev.json').read_bytes()==prior,'summary preserves existing artifact on refusal')
print('PASS synthetic host controls:',checks)
PY_HOST_CONTROLS
  exit $?
fi
OMC=$REPO/seed/out/omc
SHA=2c82480596bbdc50cda9626866209ef662d5834f278f839ff4ce0250c495029f
OUT=$1
[ -e "$OUT" ] && { echo "OUTDIR exists"; exit 91; }
mkdir -p "$OUT"
FAILS=0
ok()  { echo "CONTROL PASS $1"; }
bad() { echo "CONTROL FAIL $1"; FAILS=$((FAILS+1)); }

# Task tree: one split "ctl", each task a copy of a public dev task.
T=$OUT/tasks/ctl
mkdir -p "$T"
mk() { cp -r "$BENCH/pubtasks/dev/$2" "$T/$1"; }
for n in ref mut empty trap slow e13 authz nocode; do mk ctl-fibo-$n fibo.v1; done
for n in ref mut; do mk ctl-brk-$n brackets.v1; done

R=$OUT/run-om
place() {  # place TID SRCFILE LANG
  d=$R/cand/$1; mkdir -p "$d"
  case $3 in om) f=prog.om;; py) f=prog.py;; c) f=prog.c;; esac
  cp "$2" "$d/$f"
  s=$(sha256sum "$2" | cut -d' ' -f1)
  printf '{"tid":"%s","code_sha256":"%s","attempts":[]}\n' "$1" "$s" > "$d/gen.json"
}
for n in ref mut empty trap slow e13 authz; do place ctl-fibo-$n "$HERE/fibo-$n.om" om; done
for n in ref mut; do place ctl-brk-$n "$HERE/brk-$n.om" om; done
mkdir -p "$R/cand/ctl-fibo-nocode"
printf '{"tid":"ctl-fibo-nocode","code_sha256":null,"attempts":[]}\n' > "$R/cand/ctl-fibo-nocode/gen.json"

export OMB_TASKROOT=$OUT/tasks
python3 "$BENCH/ombench.py" judge --run "$R" --split ctl --arm a > "$OUT/judge-om.txt" 2>&1
echo "judge rc=$?" >> "$OUT/judge-om.txt"
expect() {  # expect RUN TID CLASS
  got=$(python3 -c "import json,sys
for l in open(sys.argv[1]):
  r=json.loads(l)
  if r['tid']==sys.argv[2]: print(r['class'])" "$1/verdicts-ctl.jsonl" "$2")
  [ "$got" = "$3" ] && ok "$2 -> $3" || bad "$2 expected $3 got '$got'"
}
expect "$R" ctl-fibo-ref pass
expect "$R" ctl-brk-ref pass
expect "$R" ctl-fibo-mut wrong_output
expect "$R" ctl-brk-mut wrong_output
expect "$R" ctl-fibo-empty runtime_mechanical
expect "$R" ctl-fibo-trap runtime_mechanical
expect "$R" ctl-fibo-slow runtime_mechanical
expect "$R" ctl-fibo-e13 compile_refused
expect "$R" ctl-fibo-authz authz_violation
expect "$R" ctl-fibo-nocode no_code

# gate.om judges the same Omega candidates; crosscheck must agree case by case.
"$OMC" < "$REPO/seed/qualification/om2/gate.om" > "$OUT/gate" && chmod +x "$OUT/gate" || bad "gate build"
python3 "$BENCH/ombench.py" gatelist --run "$R" --split ctl > /dev/null
(cd "$REPO" && "$OUT/gate" "$OMC" "$SHA" "$R/gate-ctl.cases" "$OUT/gate-scratch" > "$OUT/gate-ctl.log" 2>&1)
echo "gate rc=$?" > "$OUT/gate-ctl.status"
grep -q '^OM2GATE ' "$OUT/gate-ctl.log" && ok "gate final line present" || bad "gate final line missing"
python3 "$BENCH/ombench.py" crosscheck --run "$R" --split ctl --gate-log "$OUT/gate-ctl.log" > "$OUT/crosscheck.txt" 2>&1
rc=$?
grep -q 'crosscheck agree=[1-9][0-9]* disagree=0' "$OUT/crosscheck.txt" && [ $rc -eq 0 ] \
  && ok "gate.om and comparator agree" || bad "crosscheck rc=$rc $(head -3 "$OUT/crosscheck.txt")"

# gate.om identity guard: a wrong compiler hash must make the gate INVALID with every case NOT_RUN.
(cd "$REPO" && "$OUT/gate" "$OMC" 0000000000000000000000000000000000000000000000000000000000000000 \
  "$R/gate-ctl.cases" "$OUT/gate-badsha" > "$OUT/gate-badsha.log" 2>&1)
rc=$?
grep -q '^OM2GATE INVALID' "$OUT/gate-badsha.log" && [ $rc -eq 3 ] && ok "wrong omc sha -> INVALID" \
  || bad "wrong omc sha rc=$rc"

# Denominator guard: a task with no candidate record is NOT_RUN and the judge exits 2, never a pass rate.
R2=$OUT/run-missing; mkdir -p "$R2/cand"
cp -r "$R/cand/ctl-fibo-ref" "$R2/cand/"
python3 "$BENCH/ombench.py" judge --run "$R2" --split ctl --arm a > "$OUT/judge-missing.txt" 2>&1
rc=$?
grep -q '^NOT_RUN 9 of 10' "$OUT/judge-missing.txt" && [ $rc -eq 2 ] && ! grep -q '^judged' "$OUT/judge-missing.txt" \
  && ok "missing candidates -> NOT_RUN 9 of 10, rc=2" || bad "denominator guard rc=$rc"

# Corpus guards: a task without cases or without a hidden output file is a CORPUS FAULT.
mkdir -p "$OUT/tasks-bad/ctl"; cp -r "$T/ctl-fibo-ref" "$OUT/tasks-bad/ctl/zz-nocases"
printf '{"cases":[]}\n' > "$OUT/tasks-bad/ctl/zz-nocases/meta.json"
OMB_TASKROOT=$OUT/tasks-bad python3 "$BENCH/ombench.py" judge --run "$R" --split ctl --arm a > "$OUT/fault1.txt" 2>&1
rc=$?
grep -q 'CORPUS FAULT \[zz-nocases\] no cases' "$OUT/fault1.txt" && [ $rc -eq 1 ] && ok "empty task -> CORPUS FAULT" \
  || bad "empty task guard rc=$rc"
rm -r "$OUT/tasks-bad/ctl/zz-nocases"; cp -r "$T/ctl-fibo-ref" "$OUT/tasks-bad/ctl/zz-noout"
rm "$OUT/tasks-bad/ctl/zz-noout/05.out"
OMB_TASKROOT=$OUT/tasks-bad python3 "$BENCH/ombench.py" judge --run "$R" --split ctl --arm a > "$OUT/fault2.txt" 2>&1
rc=$?
grep -q 'CORPUS FAULT \[zz-noout\] case 5 missing .out' "$OUT/fault2.txt" && [ $rc -eq 1 ] \
  && ok "missing expected output -> CORPUS FAULT" || bad "missing output guard rc=$rc"

# Python and C arms: correct references pass, a planted wrong program fails, a forbidden import is flagged.
T3=$OUT/tasks3/ctl; mkdir -p "$T3"
for n in ref wrong authz; do cp -r "$BENCH/pubtasks/dev/fibo.v1" "$T3/ctl-fibo-$n"; done
for L in py c; do
  R3=$OUT/run-$L; arm=f$L; R=$R3
  for n in ref wrong authz; do place ctl-fibo-$n "$HERE/fibo-$n.$L" $L; done
  OMB_TASKROOT=$OUT/tasks3 python3 "$BENCH/ombench.py" judge --run "$R3" --split ctl --arm $arm > "$OUT/judge-$L.txt" 2>&1
  expect "$R3" ctl-fibo-ref pass
  expect "$R3" ctl-fibo-wrong wrong_output
  expect "$R3" ctl-fibo-authz authz_violation
done

echo "CONTROLS FAILS=$FAILS"
[ $FAILS -eq 0 ]
