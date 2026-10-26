#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Self-test of the independent OSC IR v7 oracle (seed/wasm/v7/oracle.js).
#
# 1. writer.js emits every fixture unit into $BUILD/v7-oracle/units.
# 2. oracle.js check / run results are compared with the hand-written
#    expectations in seed/wasm/v7/expect/ (check.txt, run/*.script + .out).
# 3. Usage/exit-code controls and a source-independence check.
# 4. Mutants: patched copies of oracle.js in $BUILD/v7-oracle/mutants/ (each
#    replaces the one line tagged `// M:<marker>`); every mutant must produce
#    at least one mismatch against the same expectations.
#
# Exit 0 PASS, 1 FAIL.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${BUILD:-$root/build}/v7-oracle
units=$out/units
expect=$here/expect
fail=0
note() { printf '%s\n' "$*"; }
bad() { note "FAIL: $*"; fail=1; }

rm -rf "$out"
mkdir -p "$units" "$out/runs"
node "$here/writer.js" "$units" >/dev/null

# suite ORACLE STOP_AT_FIRST -> prints mismatches to stdout, returns count via $mism
suite() {
  local oracle=$1 first=$2 name want got rc wantrc u s base
  mism=0
  checks=0
  runs=0
  while IFS=$'\t' read -r name want; do
    case $name in ''|'#'*) continue ;; esac
    checks=$((checks + 1))
    set +e
    got=$(node "$oracle" check "$units/$name.unit" 2>&1)
    rc=$?
    set -e
    wantrc=1
    [ "${want%% *}" = ACCEPT ] && wantrc=0
    if [ "$(printf '%s' "$got" | cut -d' ' -f1-2)" != "$want" ] || [ "$rc" != "$wantrc" ]; then
      note "  mismatch check $name: want '$want' rc $wantrc, got '$got' rc $rc"
      mism=$((mism + 1))
      [ "$first" = 1 ] && return 0
    fi
  done < "$expect/check.txt"
  for s in "$expect"/run/*.script; do
    base=$(basename "$s" .script)
    u=$units/$base.unit
    runs=$((runs + 1))
    set +e
    node "$oracle" run --codes "$u" "$s" > "$out/runs/$base.got" 2> "$out/runs/$base.err"
    rc=$?
    set -e
    if [ "$rc" != 0 ] || ! cmp -s "$out/runs/$base.got" "$expect/run/$base.out"; then
      note "  mismatch run $base (rc $rc)"
      mism=$((mism + 1))
      [ "$first" = 1 ] && return 0
    fi
    if [ -f "$expect/run/$base.plain" ]; then
      runs=$((runs + 1))
      set +e
      node "$oracle" run "$u" "$s" > "$out/runs/$base.plain.got" 2>&1
      rc=$?
      set -e
      if [ "$rc" != 0 ] || ! cmp -s "$out/runs/$base.plain.got" "$expect/run/$base.plain"; then
        note "  mismatch run (plain) $base (rc $rc)"
        mism=$((mism + 1))
        [ "$first" = 1 ] && return 0
      fi
    fi
  done
  return 0
}

# --- fixture set == expectation set (no silently skipped unit)
comm -3 <(cd "$units" && ls -- *.unit | sed 's/\.unit$//' | sort) \
        <(grep -v '^#' "$expect/check.txt" | cut -f1 | sed '/^$/d' | sort) > "$out/setdiff"
if [ -s "$out/setdiff" ]; then bad "fixture set differs from expect/check.txt:"; cat "$out/setdiff"; fi
for s in "$expect"/run/*.script; do
  [ -f "$units/$(basename "$s" .script).unit" ] || bad "run script without unit: $s"
  [ -f "${s%.script}.out" ] || bad "run script without .out: $s"
done

# --- unpatched oracle: zero mismatches
suite "$here/oracle.js" 0
[ "$mism" = 0 ] || bad "oracle: $mism mismatches"
nchecks=$checks nruns=$runs
note "oracle: checks=$nchecks runs=$nruns mismatches=$mism"

# --- usage and exit-code controls
expect_rc() { # WANT_RC DESCRIPTION CMD...
  local want=$1 what=$2 rc
  shift 2
  set +e
  "$@" > "$out/ctl.out" 2> "$out/ctl.err"
  rc=$?
  set -e
  [ "$rc" = "$want" ] || bad "control '$what': rc $rc, want $want"
}
ctl=$out/ctl
mkdir -p "$ctl"
printf 'call nosuch\n' > "$ctl/unknown.s"
printf 'call get\n' > "$ctl/arity.s"
printf 'call set 0 256\n' > "$ctl/range.s"
printf 'call set 0 -1\n' > "$ctl/neg.s"
printf 'call set 0x1 0x3f8000\n' > "$ctl/floatarg.s"
printf 'jump get 0\n' > "$ctl/verb.s"
printf 'region nosuch\n' > "$ctl/region.s"
printf 'call get 0\n' > "$ctl/ok.s"
O=$here/oracle.js
expect_rc 2 'check without unit' node "$O" check
expect_rc 2 'unknown command' node "$O" frob "$units/rt_u8.unit"
expect_rc 2 'run without script' node "$O" run --codes "$units/rt_u8.unit"
expect_rc 2 'missing unit file' node "$O" check "$units/does-not-exist.unit"
expect_rc 2 'unknown function' node "$O" run "$units/rt_u8.unit" "$ctl/unknown.s"
expect_rc 2 'wrong arity' node "$O" run "$units/rt_u8.unit" "$ctl/arity.s"
expect_rc 2 'u8 argument 256' node "$O" run "$units/rt_u8.unit" "$ctl/range.s"
expect_rc 2 'u8 argument -1' node "$O" run "$units/rt_u8.unit" "$ctl/neg.s"
expect_rc 2 'short float argument' node "$O" run "$units/rt_f32.unit" "$ctl/floatarg.s"
expect_rc 2 'unknown script verb' node "$O" run "$units/rt_u8.unit" "$ctl/verb.s"
expect_rc 2 'unknown region' node "$O" run "$units/rt_u8.unit" "$ctl/region.s"
expect_rc 1 'run of refused unit' node "$O" run "$units/inv_missing_init.unit" "$ctl/ok.s"
[ -s "$out/ctl.out" ] && bad 'run of refused unit wrote stdout'
grep -q '^REFUSE INVALID ' "$out/ctl.err" || bad 'run of refused unit: no REFUSE on stderr'
expect_rc 0 '--codes before verb' node "$O" --codes run "$units/rt_u8.unit" "$ctl/ok.s"
[ "$(cat "$out/ctl.out")" = 'get = 0' ] || bad "--codes before verb printed '$(cat "$out/ctl.out")'"

# --- independence: neither file loads the other or any repo producer/backend
for f in oracle.js writer.js; do
  reqs=$(grep -v '^ *//' "$here/$f" | grep -o "require([^)]*)" | sort -u | tr '\n' ' ')
  case $f in
    oracle.js) [ "$reqs" = "require('fs') " ] || bad "$f requires: $reqs" ;;
    writer.js) [ "$reqs" = "require('fs') require('path') " ] || bad "$f requires: $reqs" ;;
  esac
done
# Outside comments, neither file names a sibling implementation.
for pair in 'oracle.js:writer\.js' 'writer.js:oracle\.js'; do
  f=${pair%%:*}
  if grep -v '^ *//' "$here/$f" | grep -q "irwasm\|omc\.om\|osc_ir\|${pair#*:}"; then
    bad "$f names a sibling implementation outside comments"
  fi
done

# A mutant "caught" against a failing baseline proves nothing.
if [ "$fail" != 0 ]; then
  note "V7ORACLE FAIL checks=$nchecks runs=$nruns mutants=NOT_RUN (baseline failed)"
  exit 1
fi

# --- mutants
mutants=$(cat <<'EOF'
bounds	    if (i < 0n) trap(3);
bounds:off1	    if (i < 0n || i > BigInt(this.rg.count)) trap(3);
initcheck	        if (false) {
meet	      for (let k = 0; k < n; k++) st[k] |= o[k];
satnan	    if (Number.isNaN(x)) return intMax(dt);
satlow	    if (t < intMin(dt)) return 0n;
sathigh	    if (t > intMax(dt)) return wrapInt(dt, t);
owner	    void rg;
textro	        void rg;
callsite	          const r = new Instance(u).call(fi, vals);
callsite:rollback	          const snap = inst.module.map((g) => g && g.mem.slice()); let r; try { r = inst.call(fi, vals); } catch (e) { inst.module.forEach((g, k) => { if (g) g.mem.set(snap[k]); }); throw e; }
gratuitous	  void 0;
f32upper	  if (t === T_F32) return true;
i2f32	    x = Number(m);
bcsign	  return bits;
export	  const exp = () => {};
trailing	  void 0;
fneg	              if (t === T_F32) regs[ins.dst] = f32bits(-f32val(a));
cast5	    if (false) trap(5);
EOF
)
nm=0 caught=0
mdir=$out/mutants
while IFS=$'\t' read -r tag repl; do
  marker=${tag%%:*}
  mname=${tag/:/_}
  nm=$((nm + 1))
  hits=$(grep -c "// M:$marker\$" "$O" || true)
  if [ "$hits" != 1 ]; then bad "mutant $mname: marker M:$marker found $hits times"; continue; fi
  mkdir -p "$mdir/$mname"
  MREPL=$repl MMARK="// M:$marker" node -e '
    const fs = require("fs");
    const lines = fs.readFileSync(process.argv[1], "utf8").split("\n");
    const i = lines.findIndex((l) => l.endsWith(process.env.MMARK));
    lines[i] = process.env.MREPL;
    fs.writeFileSync(process.argv[2], lines.join("\n"));
  ' "$O" "$mdir/$mname/oracle.js"
  if cmp -s "$O" "$mdir/$mname/oracle.js"; then bad "mutant $mname: patch changed nothing"; continue; fi
  suite "$mdir/$mname/oracle.js" 1 > "$mdir/$mname/log"
  if [ "$mism" -gt 0 ]; then
    caught=$((caught + 1))
    note "mutant $mname: caught ($(sed 's/^  mismatch //' "$mdir/$mname/log" | head -n1))"
  else
    bad "mutant $mname: SURVIVED"
  fi
done <<< "$mutants"

if [ "$fail" = 0 ] && [ "$caught" = "$nm" ]; then
  note "V7ORACLE PASS checks=$nchecks runs=$nruns mutants=$caught/$nm"
  exit 0
fi
note "V7ORACLE FAIL checks=$nchecks runs=$nruns mutants=$caught/$nm"
exit 1
