#!/usr/bin/env bash
# tools/gen/abstract/test_gen_abstract.sh <build dir> -- tests of GEN-3 (aien-spark/omega#478):
#   1 recheck: the schema v1 fixtures pass; every format mutant is refused with its reason
#   2 corpus: files and prompt bytes, the C oracle reproduces every expected file, FAMSIG rule, the harness
#     (tests/one_layer/acceptance/ola.sh experiment) accepts every solution and every reference
#   3 extract: two runs are byte-identical and equal the committed record set; the expected rejections
#   4 verify: every committed record is VERIFIED
#   5 transfer probes (tools/gen/abstract/probe): every positive and negative probe passes
#   6 verify mutants: each must be BLOCKED with its reason
# <build dir> holds gen_abstract/{recheck,gabs,oracle,famsig}, omegese, omega_emit, checker2, one_layer.
# Every check has a negative control or a mutant that must fail it. CPU only; no model, no GPU.
set -euo pipefail
export LC_ALL=C
B="${1:?usage: test_gen_abstract.sh <build dir>}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
RC="$B/gen_abstract/recheck"
FX="$HERE/fixtures"
T="$(mktemp -d "${TMPDIR:-/tmp}/gen-abstract-test.XXXXXXXX")"
trap 'rm -rf "$T"' EXIT
fails=0 oks=0
ok()  { echo "ok   $*"; oks=$((oks + 1)); }
bad() { echo "FAIL $*"; fails=$((fails + 1)); }

# ---- 1. recheck ------------------------------------------------------------------------------------------
if "$RC" --root "$ROOT" --receipts "$FX/receipts" "$FX/abs-fixture.rec" "$FX/raw-fixture.rec" > "$T/ok.out"; then
  ok "recheck: both schema v1 fixtures (abstraction, raw) accepted with --root and --receipts"
else bad "recheck fixtures: $(cat "$T/ok.out")"; fi
# mutant <name> <fixture> <expected reason substring> <recheck options...> -- reads the mutated record from $T/m.rec
mut() {
  local name="$1" want="$2"; shift 2
  local out; out="$("$RC" "$@" "$T/m.rec" 2>&1 || true)"
  case "$out" in REFUSED*"$want"*) ok "  mutant refused: $name";; *) bad "mutant $name not refused as expected (want '$want'): $out";; esac
}
A="$FX/abs-fixture.rec" W="$FX/raw-fixture.rec"
sed '1s/.*/gen-record 2/' "$A" > "$T/m.rec"; mut "wrong header" "line 1 must be"
sed 's/^id abs-0123456789abcdef/id abs-0123456789abcdee/' "$A" > "$T/m.rec"; mut "id not from fingerprint" "first 16 hex of the fingerprint"
sed 's/^kind abstraction/kind helper/' "$A" > "$T/m.rec"; mut "unknown kind" "kind raw"
sed 's/^split corpus/split probe/' "$A" > "$T/m.rec"; mut "abstraction from a probe" "split corpus"
sed 's/^family \(gs1-[0-9a-f]*\) \(gs1-[0-9a-f]*\)$/family \2 \1/' "$A" > "$T/m.rec"; mut "family not ascending" "family"
sed 's/^params u32 u32 u32/params u32 u32 i32/' "$A" > "$T/m.rec"; mut "wrong param type" "params differ"
sed 's/^ret u32/ret u64/' "$A" > "$T/m.rec"; mut "wrong return type" "ret differs"
sed 's/^entry clampv/entry clamp/' "$A" > "$T/m.rec"; mut "entry not the signature name" "entry differs"
sed 's/^signature fn clampv x uint lo uint hi uint to uint/signature fn clampv x uint lo uint hi int to uint/' "$A" > "$T/m.rec"; mut "signature types differ from body" "params differ"
sed '0,/^fn clampv x uint lo uint hi uint to uint/{s/^fn clampv x uint lo uint hi uint to uint/fn clampv x uint lo uint hi uint to ulong/}' "$A" | sed 's/^signature .*/signature fn clampv x uint lo uint hi uint to uint/' > "$T/m.rec"
  sed -i '/^body$/,$ s/^fn clampv x uint lo uint hi uint to uint$/fn clampv x uint lo uint hi uint to ulong/' "$T/m.rec"; mut "body fn line differs from signature" "signature differs from the body"
sed 's/^pre STATED+TESTED/pre PROVED/' "$A" > "$T/m.rec"; mut "precondition claimed PROVED" "pre must be tagged STATED+TESTED"
sed 's/^post STATED+TESTED .*/post STATED+TESTED/' "$A" > "$T/m.rec"; mut "empty postcondition" "post"
sed 's/^example 12 3 9 -> 9/example 12 3 -> 9/' "$A" > "$T/m.rec"; mut "example arity" "wrong number of arguments"
sed 's/^counter 5 9 3 -> 9/counter 5 9 3 -> TRAP 15/' "$A" > "$T/m.rec"; mut "counterexample trap code" "TRAP <1..14>"
sed '/^limit /d' "$A" > "$T/m.rec"; mut "no limit" "at least one 'limit"
sed '/^ancestry fx_b /d' "$A" > "$T/m.rec"; mut "single-source helper" ">= 2 distinct corpus solutions"
sed "s/^ancestry fx_b .*/ancestry fx_b $(sed -n 's/^ancestry fx_a //p' "$A")/" "$A" > "$T/m.rec"; mut "two ancestors, one solution digest" ">= 2 distinct corpus solutions"
fp="$(sed -n 's/^fingerprint //p' "$A")"
sed "s/^differs fx_b .*/differs fx_b $fp/" "$A" > "$T/m.rec"; mut "helper identical to a whole corpus solution" "equals the whole corpus solution"
sed '/^differs fx_b /d' "$A" > "$T/m.rec"; mut "one differs line" "differs"
sed 's/^source \(.*\) [0-9a-f]*$/source \1 '"$(printf stale | sha256sum | cut -c1-64)"'/' "$A" > "$T/m.rec"; mut "stale source digest" "stale source digest" --root "$ROOT"
sed 's|^source tools/gen/abstract/fixtures/source.txt|source ../x/source.txt|' "$A" > "$T/m.rec"; mut "source path with .." "relative path"
fake="$(printf 'gen-receipt 1\nrecord abs-0123456789abcdef\nfixture forged\n' | sha256sum | cut -c1-64)"
sed "s/^receipt .*/receipt $fake/" "$A" > "$T/m.rec"; mut "receipt id with no receipt file" "receipt file" --receipts "$FX/receipts"
mkdir -p "$T/rc"; cp "$FX/receipts/"* "$T/rc/"; r="$(sed -n 's/^receipt //p' "$A")"; printf 'extra\n' >> "$T/rc/$r"; cp "$A" "$T/m.rec"; mut "edited receipt file" "does not hash to its id" --receipts "$T/rc"
rw="$(sed -n 's/^receipt //p' "$W")"; sed "s/^receipt .*/receipt $rw/" "$A" > "$T/m.rec"; mut "another record's receipt" "not this record's" --receipts "$FX/receipts"
sed 's/^validity unverified/validity trusted/' "$A" > "$T/m.rec"; mut "unknown validity" "validity"
printf '\n' | cat "$A" - > "$T/m.rec"; mut "trailing empty line" "empty line"
sed 's/^desc /desc  /' "$A" > "$T/m.rec"; mut "two spaces" "two spaces"
sed 's/^desc \(.*\)$/desc \1\r/' "$A" > "$T/m.rec"; mut "CR byte" "outside ASCII"
sed '$d' "$A" > "$T/m.rec"; mut "body not closed by end" "must end with 'end'"
sed '/^body$/,$ s/^ret x$/ret x\nend\nfn other a uint to uint\nret a/' "$A" > "$T/m.rec"; mut "two functions in an abstraction body" "exactly one function"
sed 's/^fn clampv x uint lo uint hi uint to uint$/fn clampv res uint lo uint hi uint to uint/; s/^signature fn clampv x /signature fn clampv res /' "$A" > "$T/m.rec"; mut "parameter named res" "res is reserved"
sed 's/^kind raw/kind abstraction/' "$W" > "$T/m.rec"; mut "raw record relabelled abstraction" "abstraction id"
sed 's/^ancestry fx_clamp /ancestry fx_other /' "$W" > "$T/m.rec"; mut "raw ancestry not its own task" "its own task"
sed 's/^signature fn fx_clamp a uint to uint/signature fn clampv x uint lo uint hi uint to uint/; s/^entry fx_clamp/entry clampv/; s/^params u32$/params u32 u32 u32/' "$W" > "$T/m.rec"; mut "raw signature not the entry (last fn)" "last fn line"
grep -v '^ancestry ' "$A" | sed 's/^validity unverified/validity unverified\nancestry fx_a '"$(printf a | sha256sum | cut -c1-64)"'/' > "$T/m.rec"; mut "single ancestry line" ">= 2 distinct"
"$RC" > /dev/null 2>&1 && bad "recheck with no record must be a usage error" || { [ $? = 2 ] && ok "  usage error (exit 2) without a record" || bad "recheck usage exit code"; }


# ---- 2. corpus ------------------------------------------------------------------------------------------
CO="$ROOT/tests/one_layer/generalization/corpus"
RS="$ROOT/tests/one_layer/generalization/records"
recof() { local f; for f in "$RS"/abstraction/*.rec; do if grep -qx "entry $1" "$f"; then echo "$f"; fi; done; }
ntask="$(ls "$CO/tasks" | wc -l)"
[ "$ntask" -ge 48 ] && ok "corpus: $ntask tasks" || bad "corpus has $ntask tasks (want >= 48)"
cbad=""
for d in "$CO"/tasks/*/; do
  t="$(basename "$d")"
  for f in requirement prompt desc inputs expected solution.omegese reference.osc; do [ -s "$d/$f" ] || cbad+=" $t/$f"; done
  LC_ALL=C grep -q '[^ -~]' "$d/prompt" && cbad+=" $t/prompt-bytes"
  [ "$(wc -c < "$d/prompt")" -le 4096 ] || cbad+=" $t/prompt-size"
  [ "$(wc -l < "$d/inputs")" -ge 8 ] || cbad+=" $t/inputs<8"
  grep -q '^kind task$' "$d/requirement" && grep -q "^entry $t\$" "$d/requirement" || cbad+=" $t/requirement"
  "$B/gen_abstract/oracle" "$t" < "$d/inputs" | cmp -s - "$d/expected" || cbad+=" $t/expected-not-oracle"
done
[ -z "$cbad" ] && ok "corpus: every task has its files, prompt bytes, >= 8 inputs, and expected == C oracle" || bad "corpus:$cbad"
sed '1s/^\(.*\)$/9999999/' "$CO/tasks/clamp_sum/inputs" | "$B/gen_abstract/oracle" clamp_sum | cmp -s - "$CO/tasks/clamp_sum/expected" \
  && bad "oracle negative control: a changed input still matched" || ok "  negative control: one changed input changes the oracle output"
FSRC="$B/gen_abstract/famsig-src"; PIN="$HERE/FAMSIG.pin"
cp "$FSRC/tests/one_layer/generalization/dev/META" "$T/devMETA"
FS="$B/gen_abstract/famsig"
[ "$(sed -n 's/^commit //p' "$PIN")" = 23c9c7e93040f6a71cd51e17f8b9c5ef8d475ad4 ] \
  && grep -qx 'file tests/one_layer/generalization/families/FAMSIG_V1.md 5ebbc8efe6e125863b17fb8b73a3b56a191d18d8a9be730dd2b4ea7133714092' "$PIN" \
  && [ "$(cat "$B/gen_abstract/famsig.built")" = "$(cat "$PIN"; echo "binary $(sha256sum < "$FS" | cut -c1-64)")" ] \
  && (while read -r kw p s; do [ "$kw" != file ] || [ "$(sha256sum < "$FSRC/$p" | cut -c1-64)" = "$s" ] || exit 1; done < "$PIN") \
  && ok "famsig built from the FAMSIG.pin commit 23c9c7e9 (FAMSIG_V1.md sha256 5ebbc8ef..., every pinned file's sha256 checked, binary recorded)" || bad "famsig pin: the binary or its sources are not the FAMSIG.pin ones"
# famsig_build never reuses a stale binary; in a copy of the build dir
FB="$T/fb"; mkdir -p "$FB/gen_abstract"; cp "$FS" "$B/gen_abstract/famsig.built" "$FB/gen_abstract/"
bash "$HERE/famsig_build" "$FB" > "$T/fb.out" 2>&1 && ! grep -q 'built famsig' "$T/fb.out" && ok "famsig_build: an up-to-date binary is kept" || bad "famsig_build rebuilt an up-to-date binary: $(cat "$T/fb.out")"
printf 'stale\n' > "$FB/gen_abstract/famsig"; chmod +x "$FB/gen_abstract/famsig"
bash "$HERE/famsig_build" "$FB" > "$T/fb.out" 2>&1 && grep -q 'built famsig' "$T/fb.out" && "$FB/gen_abstract/famsig" canon arith > /dev/null \
  && ok "  stale control: a replaced famsig binary is rebuilt" || bad "famsig_build kept a replaced binary"
sed -i 's/^commit .*/commit 8255a59031146e9f6de535425d591e0fdcee5b8d/' "$FB/gen_abstract/famsig.built"
bash "$HERE/famsig_build" "$FB" > "$T/fb.out" 2>&1 && grep -q 'built famsig' "$T/fb.out" && cmp -s "$FB/gen_abstract/famsig.built" <(cat "$PIN"; echo "binary $(sha256sum < "$FB/gen_abstract/famsig" | cut -c1-64)") \
  && ok "  stale control: a binary built from another pin (8255a590) is rebuilt from FAMSIG.pin" || bad "famsig_build kept a binary built from another pin"
# shallow clone: a repo without the pinned commit fetches it from origin, or fails with a clear message
SH="$T/sh"; mkdir -p "$SH/tools/gen/abstract" "$SH/src"; cp "$HERE/famsig_build" "$PIN" "$SH/tools/gen/abstract/"; cp "$ROOT/src/sha256.c" "$ROOT/src/sha256.h" "$SH/src/"
git -C "$SH" init -q && git -C "$SH" remote add origin "$ROOT"
bash "$SH/tools/gen/abstract/famsig_build" "$SH/build" > "$T/sh.out" 2>&1 && grep -q 'fetching it from origin' "$T/sh.out" && [ -x "$SH/build/gen_abstract/famsig" ] \
  && ok "famsig_build in a clone without the pinned commit fetches it from origin and builds" || bad "famsig_build shallow fetch: $(tail -n 1 "$T/sh.out")"
git -C "$SH" remote set-url origin "$T/nonexistent"; rm -rf "$SH/build"; rm -rf "$SH/.git"; git -C "$SH" init -q; git -C "$SH" remote add origin "$T/nonexistent"
! bash "$SH/tools/gen/abstract/famsig_build" "$SH/build" > "$T/sh.out" 2>&1 && grep -q "cannot get pinned commit .* run 'git fetch origin" "$T/sh.out" && [ ! -e "$SH/build/gen_abstract/famsig" ] \
  && ok "  negative control: without any way to get the commit it fails with a clear message and builds nothing" || bad "famsig_build without the commit: $(tail -n 1 "$T/sh.out")"
"$FS" rule "$T/devMETA" "$CO/META" > "$T/rule.out" 2>&1 && tail -n 1 "$T/rule.out" | grep -qx 'RULE PASS' \
  && ok "famsig rule dev/META corpus/META: RULE PASS, R1-R8 incl. coarse R6 and R8 ($(grep -c . "$CO/META") META lines, $(sed -n 's/^split corpus .*families=//p' "$T/rule.out") corpus families)" \
  || bad "famsig rule: $(tail -n 2 "$T/rule.out")"
prims=""; for p in arith branch loop array call nested; do grep -v '^#' "$CO/META" | grep -q "$p" || prims+=" $p"; done
[ -z "$prims" ] && ok "corpus META covers all six primitives" || bad "corpus META misses:$prims"
{ cat "$CO/META"; echo "zz_reserved corpus 1 1 2 0 0 0 loop(loop(branch(arith)))"; } > "$T/badMETA"
"$FS" rule "$T/devMETA" "$T/badMETA" > "$T/rule5.out" 2>&1 || ! grep -q "^FAIL R3 .*zz_reserved" "$T/rule5.out" && bad "famsig rule did not refuse a reserved dev shape in the corpus with R3" || ok "  negative control: a reserved dev shape in corpus META fails the rule"
{ cat "$CO/META"; echo "zz_coarse corpus 1 0 1 0 1 1 seq(arith,loop(call(nested)))"; } > "$T/badMETA6"
"$FS" rule "$T/devMETA" "$T/badMETA6" > "$T/rule6.out" 2>&1 || ! grep -q "^FAIL R6 .*zz_coarse" "$T/rule6.out" && bad "famsig rule did not refuse a corpus shape sharing a dev coarse signature with R6" || ok "  negative control: a corpus shape whose coarse signature is a dev one (gc1:loop(call(arith))) fails R6"
cmp -s <(grep -v '^#' "$CO/META" | tail -n +2 | cut -d' ' -f1) <(ls "$CO/tasks") && ok "corpus META has exactly one line per task" || bad "corpus META lines differ from the task list"
# the harness, called (not copied): every solution IR as a candidate, plus a one-task mutant set
H="$T/ola"; mkdir -p "$H/tasks/cases" "$H/cands/solutions" "$H/cands/mutant/wrap8" "$H/ev"
for d in "$CO"/tasks/*/; do
  t="$(basename "$d")"; mkdir -p "$H/tasks/cases/$t/hidden" "$H/cands/solutions/$t"
  cp "$d/requirement" "$d/prompt" "$H/tasks/cases/$t/"; cp "$d/inputs" "$d/expected" "$d/reference.osc" "$H/tasks/cases/$t/hidden/"
  "$B/omegese/omegese" emit "$d/solution.omegese" "$T/s.json" > /dev/null && "$B/omega_emit/omega_emit" emit "$T/s.json" "$H/cands/solutions/$t/candidate.oscir" > /dev/null
done
sed '1,/^body$/d' "$(recof sat8)" > "$T/m.omegese"
cat "$CO/tasks/wrap8/misuse.sat8.omegese" >> "$T/m.omegese"
"$B/omegese/omegese" emit "$T/m.omegese" "$T/m.json" > /dev/null && "$B/omega_emit/omega_emit" emit "$T/m.json" "$H/cands/mutant/wrap8/candidate.oscir" > /dev/null
(cd "$H" && tar --format=ustar --sort=name --owner=0 --group=0 --numeric-owner --mtime=@0 -cf tasks.tar tasks && sha256sum tasks.tar > TASKS.sha256)
OLA_ALLOW_DIRTY=1 OLA_EVIDENCE_ROOT="$H/ev" bash "$ROOT/tests/one_layer/acceptance/ola.sh" experiment "$H/tasks.tar" "$H/TASKS.sha256" "$H/cands" > "$H/out" 2> "$H/err" || true
grep -q "^set=solutions accepted=$ntask/$ntask rejected=0 errors=0" "$H/out" && ok "harness: every Omegese solution accepted ($ntask/$ntask)" || bad "harness solutions: $(grep '^set=solutions' "$H/out")"
grep -q "^references_accepted=$ntask/$ntask$" "$H/out" && ok "harness: every OSC reference accepted ($ntask/$ntask)" || bad "harness references: $(grep '^references' "$H/out")"
mv="$(find "$H/ev" -path '*/sets/mutant/verdicts.txt' | head -n 1)"
grep -q "^set=mutant accepted=0/$ntask " "$H/out" && grep -q '^wrap8 REJECT wrong_result' "$mv" && ok "  negative control: the harness rejects the wrap8 candidate built from a misused record (wrong_result)" || bad "harness mutant: $(grep '^set=mutant' "$H/out") $(grep '^wrap8' "$mv")"

# ---- 3. extract -----------------------------------------------------------------------------------------
X="$HERE/extract"
bash "$X" "$B" "$T/r1" > "$T/x1.out" 2>&1 && bash "$X" "$B" "$T/r2" > /dev/null 2>&1 || bad "extract failed: $(tail -n 2 "$T/x1.out")"
diff -r "$T/r1" "$T/r2" > /dev/null && ok "extract: two runs are byte-identical ($(cat "$T/x1.out"))" || bad "extract is not deterministic"
diff -r "$T/r1" "$RS" > /dev/null && ok "extract: output equals the committed record set (reconstructible from pinned sources)" || bad "committed records differ from a fresh extract: $(diff -rq "$T/r1" "$RS" | head -n 3)"
for want in "^cube .* single-source: occurs in 1 extraction task$" "^dist .* single-source: occurs in 1 extraction task$" \
            "^ds2 - .* calls digsum: v1 extracts leaf helpers only$" "^gcd .* identical to the whole corpus solution of gcd_only$"; do
  grep -q "$want" "$RS/REJECTIONS" && ok "  rejection logged: $(grep "$want" "$RS/REJECTIONS" | cut -d' ' -f1,4-)" || bad "REJECTIONS lacks /$want/"
done
for n in clamp absdiff sat8 digsum digcount popcnt sum4 max4 avg2; do
  grep -qx "entry $n" "$RS"/abstraction/*.rec && ok "  abstraction record: $n" || bad "no abstraction record for $n"
done
"$RC" --root "$ROOT" --receipts "$RS/receipts" "$RS"/raw/*.rec "$RS"/abstraction/*.rec > "$T/rc.out" 2>&1 && ok "recheck: all $(grep -c '^OK' "$T/rc.out") committed records" || bad "recheck committed: $(grep -v '^OK' "$T/rc.out" | head -n 2)"
(cd "$RS" && sha256sum --quiet -c MANIFEST && sha256sum --quiet -c SET.sha256) && ok "MANIFEST and SET.sha256 match the record files" || bad "MANIFEST or SET.sha256 stale"
# C/D candidate pools (GEN_PROTOCOL_V1.md section 3), recomputed here independently of extract
grep -v '^#' "$CO/META" | tail -n +2 | while read -r t _ _ _ _ _ _ _ s; do echo "$("$FS" canon "$s") $t"; done | sort > "$T/fo"
declare -A taken=(); pre=()
for r in 0 1 2 3; do
  for f in $(cut -d' ' -f1 "$T/fo" | uniq); do
    [ "${#pre[@]}" -lt 14 ] || break
    t="$(awk -v f="$f" '$1 == f { print $2 }' "$T/fo" | sed -n "$((r + 1))p")"
    if [ -n "$t" ] && [ -z "${taken[$t]:-}" ]; then pre+=("$t"); taken[$t]=1; fi
  done
done
[ "$(printf '%s\n' "${pre[@]}")" = "$(cat "$RS/PREFIX")" ] && ok "PREFIX: the 14 B-prefix ids by the protocol pick rule ($(paste -sd' ' "$RS/PREFIX"))" || bad "PREFIX differs from the recomputed pick"
# dpool <prefix file>: abstraction MANIFEST lines with at least one ancestry task outside the prefix
dpool() { grep ' abstraction/' "$RS/MANIFEST" | while read -r h f; do
  if sed -n '/^body$/q; s/^ancestry \([^ ]*\) .*/\1/p' "$RS/$f" | grep -qvxF -f "$1"; then echo "$h  $f"; fi; done; }
probes_list="$(grep -v '^#' "$CO/PROBES" | cut -d' ' -f1 | sort -u)"
cpool() { grep ' raw/' "$RS/MANIFEST" | while read -r h f; do t="${f#raw/raw-}"; t="${t%.rec}"
  if ! printf '%s\n' "$probes_list" | grep -qxF "$t" && ! grep -qxF "$t" "$RS/PREFIX"; then echo "$h  $f"; fi; done; }
cmp -s <(cpool) "$RS/C.MANIFEST" \
  && ok "C.MANIFEST: the $(wc -l < "$RS/C.MANIFEST") raw records of extraction tasks outside the prefix (sha256 $(sha256sum < "$RS/C.MANIFEST" | cut -c1-16)...)" || bad "C.MANIFEST is not MANIFEST raw lines minus probes and the prefix"
pl=""; for f in $(awk '{ print $2 }' "$RS/C.MANIFEST" "$RS/D.MANIFEST"); do
  if grep -qx 'split probe' "$RS/$f" || sed -n '/^body$/q; s/^ancestry \([^ ]*\) .*/\1/p' "$RS/$f" | grep -qxF -f <(printf '%s\n' $probes_list); then pl+=" $f"; fi
done
[ -z "$pl" ] && ok "no probe task (split probe, or a PROBES task in the ancestry) in C.MANIFEST or D.MANIFEST" || bad "probe records in a retrieval pool:$pl"
cmp -s <(dpool "$RS/PREFIX") "$RS/D.MANIFEST" \
  && ok "D.MANIFEST: the $(wc -l < "$RS/D.MANIFEST") abstraction records with ancestry outside the prefix (sha256 $(sha256sum < "$RS/D.MANIFEST" | cut -c1-16)...)" || bad "D.MANIFEST is not the abstraction lines minus prefix-only ancestry"
{ cat "$RS/PREFIX"; echo avg_inc; } > "$T/pre+"
dpool "$T/pre+" | grep -q "$(grep -l '^entry avg2$' "$RS"/abstraction/*.rec | xargs basename)" \
  && bad "D rule kept avg2 although its whole ancestry (avg_double, avg_inc) is in the prefix" || ok "  negative control: with avg_inc added to the prefix, avg2 (ancestry avg_double, avg_inc) leaves D"
(cd "$RS" && sha256sum --quiet -c C.MANIFEST && sha256sum --quiet -c D.MANIFEST) && "$RC" --receipts "$RS/receipts" $(awk -v d="$RS" '{ print d "/" $2 }' "$RS/C.MANIFEST" "$RS/D.MANIFEST") > /dev/null 2>&1 \
  && ok "C and D manifests: every listed record matches its digest and passes recheck --receipts (what genret runs)" || bad "C or D manifest lists a bad record"

# family rule gate: FAMRULE.receipt is a fresh famsig rule run, and a failing rule leaves no records or manifests
cmp -s "$T/r1/FAMRULE.receipt" "$RS/FAMRULE.receipt" && tail -n 1 "$RS/FAMRULE.receipt" | grep -qx 'RULE PASS' \
  && grep -qx "famsig_commit $(sed -n 's/^commit //p' "$HERE/FAMSIG.pin")" "$RS/FAMRULE.receipt" \
  && grep -qx "corpus_meta tests/one_layer/generalization/corpus/META $(sha256sum < "$CO/META" | cut -c1-64)" "$RS/FAMRULE.receipt" \
  && ok "FAMRULE.receipt equals a fresh run: pinned famsig commit, pinned file digests, corpus META digest, RULE PASS" || bad "FAMRULE.receipt is stale or not a RULE PASS"
FR="$T/frroot"; rm -rf "$FR"; mkdir -p "$FR/tests/one_layer/generalization"; cp -R "$CO" "$FR/tests/one_layer/generalization/"
echo "zz_coarse corpus 1 0 1 0 1 1 seq(arith,loop(call(nested)))" >> "$FR/tests/one_layer/generalization/corpus/META"
cp -R "$RS" "$T/frout"
if bash "$X" --root "$FR" "$B" "$T/frout" > "$T/fr.out" 2>&1; then bad "extract wrote a record set although the family rule fails"
else left="$(cd "$T/frout" && ls MANIFEST SET.sha256 C.MANIFEST D.MANIFEST FAMRULE.receipt PREFIX raw abstraction 2>/dev/null | head -n 3 || true)"
  grep -q 'famsig rule dev/META corpus/META: FAIL R6' "$T/fr.out" && [ -z "$left" ] \
    && ok "  mutant: a corpus META line in a dev coarse family (R6) stops extract; no manifest, SET.sha256, receipt or record is left" \
    || bad "family rule mutant: $(tail -n 1 "$T/fr.out") left: $left"
fi
# generated-input check: sat_sum with sat8 saturating at 200 instead of 255 is refused
SM="$T/satmut"; mkdir -p "$SM"; cp -R "$CO/tasks/sat_sum" "$SM/"
sed -i 's/^if gt x 255$/if gt x 200/; s/^ret 255$/ret 200/' "$SM/sat_sum/solution.omegese"
if ! grep -q 'gt x 200' "$SM/sat_sum/solution.omegese"; then bad "sat_sum mutant: the edit did not apply"
else
  gm="$( . "$HERE/genabs.sh"; ga_init "$B" "$ROOT" "$T/gm" && ga_chain "$SM/sat_sum/solution.omegese" "$T/gm.s" && GA_RCPT="" \
         && { ga_gen_check "$T/gm.s.ir" "$SM/sat_sum" "$T/gm.g" && echo "ACCEPTED"; } || echo "REFUSED $GA_ERR" )"
  case "$gm" in "REFUSED generated seed=478001 n=200 check failed: REJECT"*) ok "  mutant: sat_sum saturating at 200 is refused by the generated-input check (${gm:0:90}...)";;
    *) bad "sat_sum at-200 mutant not refused by the generated-input check: $gm";; esac
fi
gc="$(grep -l '^generated seed=478001 n=200 inputs=[0-9a-f]* expected=[0-9a-f]* ACCEPT n=200$' "$RS"/receipts/* | wc -l)"
[ "$gc" = 49 ] && ok "every raw receipt (49) records the generated-input check: seed 478001, 200 cases, ACCEPT" || bad "only $gc raw receipts carry the generated-input check"
# ---- 4. verify ------------------------------------------------------------------------------------------
V="$HERE/verify"
bash "$V" "$B" "$RS"/raw/*.rec "$RS"/abstraction/*.rec > "$T/v.out" 2>&1 \
  && ok "verify: $(grep -c '^VERIFIED' "$T/v.out") records VERIFIED" || bad "verify: $(grep -v '^VERIFIED' "$T/v.out" | head -n 3)"

# ---- 5. probes ------------------------------------------------------------------------------------------
bash "$HERE/probe" "$B" "$RS" > "$T/p.out" 2>&1 && ok "probes: $(tail -n 1 "$T/p.out") ($(grep -c ' positive .* PASS' "$T/p.out") positive, $(grep -c ' negative .* PASS' "$T/p.out") negative)" \
  || bad "probes: $(grep -v ' PASS ' "$T/p.out" | head -n 3)"
for n in clamp absdiff sat8 digsum digcount popcnt sum4 max4 avg2; do
  grep -q " positive $n PASS" "$T/p.out" && grep -q " negative $n PASS" "$T/p.out" || bad "record $n lacks a passing positive and negative probe"
done

# ---- 6. verify mutants ----------------------------------------------------------------------------------
CL="$(recof clamp)" RW="$RS/raw/raw-clamp_sum.rec"
# vmut <name> <want> <record> [verify options] -- the record must be BLOCKED with <want> in the reason
vmut() {
  local name="$1" want="$2" rec="$3"; shift 3
  local out; out="$(bash "$V" "$@" "$B" "$rec" 2>&1 || true)"
  case "$out" in "BLOCKED "*"$want"*) ok "  mutant blocked: $name";; *) bad "mutant $name not blocked as expected (want '$want'): $out";; esac
}
cp "$CL" "$T/v.rec"; out="$(bash "$V" --receipts "$RS/receipts" "$B" "$T/v.rec" || true)"; [ "$out" = "VERIFIED $(sed -n 's/^id //p' "$CL")" ] && ok "  control: a copy of the clamp record verifies" || bad "control copy: $out"
sed 's/^pre STATED+TESTED .*/pre STATED+TESTED true/' "$CL" > "$T/v.rec"; vmut "broken precondition (record only)" "pre differs from the spec file" "$T/v.rec" --receipts "$RS/receipts"
sed 's/^ret x$/ret y/' "$CL" > "$T/v.rec"; vmut "invalid IR (unknown name in the body)" "invalid IR" "$T/v.rec" --receipts "$RS/receipts"
sed 's/^params u32 u32 u32$/params u64 u64 u64/; s/^ret u32$/ret u64/; s/ uint/ ulong/g' "$CL" > "$T/v.rec"; vmut "wrong types (signature, params, ret and body all u64)" "fingerprint does not match the body" "$T/v.rec" --receipts "$RS/receipts"
sed 's/^params u32 u32 u32$/params u32 u32 u64/' "$CL" > "$T/v.rec"; vmut "wrong type in params only" "params differ" "$T/v.rec" --receipts "$RS/receipts"
sed '0,/^source /s/^source \(.*\) [0-9a-f]*$/source \1 '"$(printf stale | sha256sum | cut -c1-64)"'/' "$CL" > "$T/v.rec"; vmut "stale source digest" "stale source digest" "$T/v.rec" --receipts "$RS/receipts"
sed 's/^receipt .*/receipt '"$(printf none | sha256sum | cut -c1-64)"'/' "$CL" > "$T/v.rec"; vmut "fake receipt id (no such receipt)" "receipt file" "$T/v.rec" --receipts "$RS/receipts"
mkdir -p "$T/frc"; cp "$RS/receipts/"* "$T/frc/"
{ sed -n '1,2p' "$RS/receipts/$(sed -n 's/^receipt //p' "$CL")"; echo "property inputs=forged expected=forged ACCEPT n=256"; } > "$T/forged"
fid="$(sha256sum "$T/forged" | cut -c1-64)"; cp "$T/forged" "$T/frc/$fid"
sed "s/^receipt .*/receipt $fid/" "$CL" > "$T/v.rec"; vmut "forged receipt (hashes to its name, not from the checks)" "receipt does not match the re-run checks" "$T/v.rec" --receipts "$T/frc"
sed "s/^receipt .*/receipt $(sed -n 's/^receipt //p' "$RW")/" "$CL" > "$T/v.rec"; vmut "another record's receipt" "not this record's" "$T/v.rec" --receipts "$RS/receipts"
sed "s/^ancestry clamp_triple .*/ancestry digsum_mod9 $(sha256sum "$CO/tasks/digsum_mod9/solution.omegese" | cut -c1-64)/" "$CL" > "$T/v.rec"
vmut "single-source helper (forged second ancestor)" "does not contain this helper" "$T/v.rec" --receipts "$RS/receipts"
sed "s/^ancestry clamp_triple .*/ancestry gap_half $(sha256sum "$CO/tasks/gap_half/solution.omegese" | cut -c1-64)/; /^ancestry sum_clamped /d" "$CL" > "$T/v.rec"
vmut "single-source helper (one real ancestor)" "does not contain this helper" "$T/v.rec" --receipts "$RS/receipts"
sed '/^family /s/ gs1-[0-9a-f]*$//' "$CL" > "$T/v.rec"; vmut "family list missing an ancestor family" "family" "$T/v.rec" --receipts "$RS/receipts"
sed '/^body$/,$ s/^ret clamp add a b 10 100$/ret clamp add a b 10 101/' "$RW" > "$T/v.rec"
vmut "raw record body edited" "body is not the task's solution" "$T/v.rec" --receipts "$RS/receipts"
sed 's/^split corpus$/split probe/' "$RW" > "$T/v.rec"; vmut "raw record split relabelled" "split is not the task's" "$T/v.rec" --receipts "$RS/receipts"
# consistent forgeries in a copy of the corpus: the spec file and the record agree, so only the checks can tell
mkroot() { rm -rf "$T/root"; mkdir -p "$T/root/tests/one_layer/generalization"; cp -R "$CO" "$T/root/tests/one_layer/generalization/"; }
respec() { # <spec name> <sed expr for spec and record> <record> -- edit both and re-pin the spec digest in the record
  local sp="$T/root/tests/one_layer/generalization/corpus/specs/$1.spec"
  sed -i "$2" "$sp"; sed "$2" "$3" | sed "s|^\(source tests/one_layer/generalization/corpus/specs/$1.spec\) .*|\1 $(sha256sum "$sp" | cut -c1-64)|" > "$T/v.rec"
}
mkroot; respec clamp 's/^\(pre\( STATED+TESTED\)\{0,1\}\) le lo hi$/\1 true/' "$CL"
vmut "broken precondition (spec and record agree, pre true)" "property check failed" "$T/v.rec" --root "$T/root" --receipts "$RS/receipts"
mkroot; respec clamp 's/^counter 5 9 3 -> 9$/counter 5 3 9 -> 5/' "$CL"
vmut "counterexample that does not violate pre" "counter1-pre-false check failed" "$T/v.rec" --root "$T/root" --receipts "$RS/receipts"
mkroot; respec clamp 's/^counter 5 9 3 -> 9$/counter 5 9 3 -> 7/' "$CL"
vmut "counterexample with a wrong stated result" "counter1-result check failed" "$T/v.rec" --root "$T/root" --receipts "$RS/receipts"
mkroot; respec clamp 's/^example 12 3 9 -> 9$/example 12 3 9 -> 12/' "$CL"
vmut "wrong example" "example check failed" "$T/v.rec" --root "$T/root" --receipts "$RS/receipts"
mkroot; mkdir -p "$T/root/tests/one_layer/generalization/corpus/tasks/clamp_only"
sed '1,/^body$/d; s/^fn clamp /fn clamp_only /' "$CL" > "$T/root/tests/one_layer/generalization/corpus/tasks/clamp_only/solution.omegese"
vmut "helper identical to a whole corpus solution" "identical to the whole corpus solution of clamp_only" "$CL" --root "$T/root" --receipts "$RS/receipts"
mkroot; sed -i 's/^ret clamp add a b 10 100$/ret clamp add a b 10 99/' "$T/root/tests/one_layer/generalization/corpus/tasks/clamp_sum/solution.omegese"
vmut "stale corpus (an ancestor solution changed)" "stale source digest" "$CL" --root "$T/root" --receipts "$RS/receipts"

echo "test_gen_abstract: $oks ok, $fails failed"
[ "$fails" = 0 ] || exit 1
echo "test-gen-abstract: PASS"
