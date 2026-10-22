#!/bin/bash
# Additive600 host packet. Existing campaign aggregator judges hashes + gates.
set -euo pipefail
cd "$(dirname "$0")/../.."
phase=${1:?checks|heldout|aggregate}; campaign=${2:?NEW_CAMPAIGN_DIR}
policy=research/exploration-600/policy.osc
target=${EX600_OUT:-$HOME/workspace/targets/omega-exploration-600-L492}
bin=$target/ex600
run() {
    local log=$1; shift
    set +e
    timeout 60 quiet-build "$@" > "$log" 2>&1
    local rc=$?
    set -e
    printf '%s\n' "$rc" > "$log.exit"
    return "$rc"
}
case $phase in
checks) id=run-001 ;;
heldout) id=run-002 ;;
aggregate) exec bash tools/m19r_campaign.sh --campaign-dir "$campaign" --runs 2 ;;
*) echo 'usage: run.sh checks|heldout|aggregate CAMPAIGN_DIR' >&2; exit 2 ;;
esac
mkdir -p "$campaign"
campaign=$(realpath "$campaign")
d=$campaign/$id
[[ ! -e $d ]] || { echo 'REFUSED existing run directory' >&2; exit 2; }
if [[ $phase == heldout ]]; then
    [[ $(jq -r .status "$campaign/run-001/verdict.json") == PASS ]] || exit 2
    (cd "$campaign/run-001" && sha256sum --quiet -c hashes.sha256)
    sha256sum --quiet -c "$campaign/run-001/sources.sha256"
    sha256sum --quiet -c "$campaign/run-001/binary.sha256"
fi
mkdir "$d"
trap 'rc=$?; if ((rc)); then printf "{\"status\":\"FAIL\",\"exit\":%s,\"scope\":\"host experiment qualification\"}\n" "$rc" > "$d/verdict.json"; fi' EXIT
jq -n --arg phase "$phase" --arg head "$(git rev-parse HEAD)" --arg host "$(hostname)" \
    --arg at "$(date -u +%FT%TZ)" --arg command "bash research/exploration-600/run.sh $phase $campaign" \
    '{phase:$phase,head:$head,host:$host,at:$at,command:$command,scope:"HOST_ONLY_SYNTHETIC",hardware:"NOT_RUN",model:"NOT_RUN",joules:"UNMEASURED"}' > "$d/run.json"
{
    git ls-files src/compiler src/sha256.c src/sha256.h src/runtime/rx_cortex.c src/runtime/rx_cortex.h
    printf '%s\n' "$policy" research/exploration-600/{FREEZE.md,AMENDMENTS.md,build.sh,run.sh} tests/research/ex600/{driver.c,report.awk} tools/m19r_campaign.sh
} | sort -u | xargs sha256sum > "$d/sources.sha256"
if [[ $phase == checks ]]; then
    run "$d/build.log" bash research/exploration-600/build.sh
    sha256sum "$bin" > "$d/binary.sha256"
    mkdir "$d/checks"
    run "$d/checks.log" "$bin" checks "$policy" "$d/checks"
    # Each mutation changes behavior, compiles successfully, and must produce a
    # failed assertion. A compiler failure/refusal is not a detected mutant.
    mkdir "$d/mutants"
    for m in greedy invalid free-failure budget seed context; do
        md=$d/mutants/$m; mkdir -p "$md/checks"
        mp=$md/policy.osc; cp "$policy" "$mp"
        mb=$bin
        case $m in
        greedy) sed -i 's/(s\[1\] != 0)/(s[1] != 0 \&\& s[1] != 2)/' "$mp" ;;
        invalid) sed -i 's/let valid: bool = (cost\[15\] \& (1 << node)) != 0;/let valid: bool = true;/' "$mp" ;;
        free-failure) sed -i '/let valid: bool =/a\    if !valid || cost[node] >= 100 { s[4] = s[4] - (7 + rent); }' "$mp" ;;
        budget) sed -i 's/if charge > s\[3\] - s\[4\]/if false/;s/7 + count(s\[8\]) > s\[3\] - s\[4\]/false/g' "$mp" ;;
        seed)
            sed 's/memcpy(s,expected,NW\*8);return 0;/memcpy(s,expected,NW*8);s[2]=ctx[21];return 0;/' tests/research/ex600/driver.c > "$md/driver.c"
            cmp -s tests/research/ex600/driver.c "$md/driver.c" && exit 1
            run "$md/build.log" env EX600_OUT="$target/mutant-$m" EX600_DRIVER="$md/driver.c" bash research/exploration-600/build.sh
            mb=$target/mutant-$m/ex600 ;;
        context)
            run "$md/build.log" env EX600_OUT="$target/mutant-$m" EX600_HOST_MUTANT=skip-context bash research/exploration-600/build.sh
            mb=$target/mutant-$m/ex600 ;;
        esac
        if [[ $mb == "$bin" ]]; then cmp -s "$policy" "$mp" && exit 1; fi
        if run "$md/result.log" "$mb" checks "$mp" "$md/checks"; then echo "SURVIVED $m" >&2; exit 1; fi
        [[ $(cat "$md/result.log.exit") == 1 ]]
        grep -qE '^checks=[0-9]+ failures=[1-9][0-9]* ' "$md/result.log"
        sha256sum "$mp" "$mb" > "$md/inputs.sha256"
        printf 'CAUGHT %s\n' "$m" | tee -a "$d/mutants.txt"
    done
    # Different processes: checkpoint boundary continuation is bit-identical.
    mkdir "$d/full" "$d/split"
    run "$d/full.log" "$bin" resume "$policy" "$d/full"
    run "$d/one.log" "$bin" one "$policy" "$d/split"
    run "$d/resume.log" "$bin" resume "$policy" "$d/split"
    cmp "$d/full/session.journal" "$d/split/session.journal"
    sha256sum "$d/split/session.journal" > "$d/repeat-before.sha256"
    run "$d/repeat.log" "$bin" repeat "$policy" "$d/split"
    sha256sum --quiet -c "$d/repeat-before.sha256"
    for cut in reserved evaluated; do
        mkdir "$d/cut-$cut"
        if run "$d/cut-$cut.log" env EX600_CUT="$cut" "$bin" one "$policy" "$d/cut-$cut"; then exit 1; fi
        [[ $(cat "$d/cut-$cut.log.exit") == 99 ]]
        run "$d/recover-$cut.log" "$bin" repeat "$policy" "$d/cut-$cut"
        grep -q 'trials=0 spent=9 records=3 interrupted=1 attempts=1' "$d/recover-$cut.log"
        run "$d/retry-$cut.log" "$bin" one "$policy" "$d/cut-$cut"
        grep -q 'trials=1 spent=18 records=5 interrupted=1 attempts=2' "$d/retry-$cut.log"
        run "$d/finish-$cut.log" "$bin" resume "$policy" "$d/cut-$cut"
    done
    # A source change is refused before another trial or append.
    cp "$policy" "$d/stale.osc"; printf '// stale artifact probe\n' >> "$d/stale.osc"
    if run "$d/stale.log" "$bin" resume "$d/stale.osc" "$d/split"; then exit 1; fi
    [[ $(cat "$d/stale.log.exit") == 2 ]]
    grep -q 'REFUSED session binding/history' "$d/stale.log"
    sha256sum --quiet -c "$d/repeat-before.sha256"
    printf 'PASS separate-process recovery, prepaid cuts, repeated completion, stale source\n' > "$d/process-checks.txt"
else
    sha256sum "$bin" > "$d/binary.sha256"
    mkdir "$d/heldout"
    run "$d/heldout.log" "$bin" heldout "$policy" "$d/heldout"
    [[ $(wc -l < "$d/heldout/rows.csv") == 301 ]]
    [[ $(wc -l < "$d/heldout/landscapes.csv") == 376 ]]
    awk -f tests/research/ex600/report.awk "$d/heldout/landscapes.csv" "$d/heldout/rows.csv" > "$d/arithmetic.txt"
    grep -q "predeclared_hypothesis=$(jq -r .hypothesis "$d/heldout/experiment.json")" "$d/arithmetic.txt"
fi
sha256sum --quiet -c "$d/sources.sha256"
printf '{"status":"PASS","scope":"host qualification only","experiment_verdict":"reported separately","hardware":"NOT_RUN"}\n' > "$d/verdict.json"
(cd "$d" && find . -type f ! -name hashes.sha256 -print0 | sort -z | xargs -0 sha256sum > hashes.sha256)
echo "PASS $phase ($d)"
