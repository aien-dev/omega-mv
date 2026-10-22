#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Audit every path changed by the frozen PRs and every canonical source-map blob.
set -euo pipefail
cudapp=$(realpath "${1:?CUDApp checkout}")
snapshots=$(realpath "${2:?PR snapshot directory}")
output=${3:?output directory}
omega=$(git rev-parse HEAD)
main=$(jq -er .omega_main "$snapshots/pins.json")
pin=$(cat cudapp.lock)
[[ $(git -C "$cudapp" rev-parse HEAD) = "$pin" ]]
scratch=$(mktemp -d "${TMPDIR:-/tmp}/carry-audit.XXXXXX")
trap 'rm -r -- "$scratch"' EXIT
mkdir -p "$output"
git -C "$cudapp" show "$pin:evidence/canonical-motor/source-carry.json" > "$scratch/prior.json"
: > "$scratch/rows"
: > "$scratch/canonical"
identity() {
    local repo=$1 ref=$2 path=$3
    if git -C "$repo" cat-file -e "$ref:$path" 2>/dev/null; then
        git -C "$repo" rev-parse "$ref:$path"
        git -C "$repo" show "$ref:$path" | sha256sum | cut -d' ' -f1
    else printf '\n\n'; fi
}
for pr in 614 615 616 617 618; do
    head=$(jq -r .headRefOid "$snapshots/pr-$pr.json")
    base=$(git merge-base "$main" "$head")
    while IFS=$'\t' read -r status path; do
        mapfile -t source < <(identity . "$head" "$path")
        mapfile -t baseline < <(identity . "$base" "$path")
        if grep -Fxq "$path" cudapp.files; then repo=$cudapp; ref=$pin; dest_repo=CUDApp
        else repo=.; ref=$omega; dest_repo=Omega; fi
        mapfile -t destination < <(identity "$repo" "$ref" "$path")
        [[ -n ${destination[0]} ]] || { echo "unaccounted destination PR$pr $path" >&2; exit 1; }
        if [[ ${source[0]} = "${destination[0]}" ]]; then classification=identical
        elif [[ $status = D && $dest_repo = CUDApp ]]; then classification=extracted-to-canonical
        elif [[ $dest_repo = CUDApp ]]; then classification=canonical-fixes-preserved
        else classification=omega-integrated; fi
        jq -nc --argjson pr "$pr" --arg head "$head" --arg base "$base" --arg path "$path" --arg status "$status" \
          --arg repo "$dest_repo" --arg ref "$ref" --arg classification "$classification" \
          --arg so "${source[0]}" --arg sh "${source[1]}" --arg bo "${baseline[0]}" --arg bh "${baseline[1]}" \
          --arg d_o "${destination[0]}" --arg dh "${destination[1]}" --slurpfile prior "$scratch/prior.json" \
          '{pr:$pr,head:$head,base:$base,path:$path,status:$status,destination_repo:$repo,destination_ref:$ref,classification:$classification,
          prior_carry_action:([$prior[0].files[]|select(.path==$path)|.action][0]//null),
          source:(if $so=="" then null else {git_blob:$so,sha256:$sh} end),
          baseline:(if $bo=="" then null else {git_blob:$bo,sha256:$bh} end),destination:{git_blob:$d_o,sha256:$dh}}' >> "$scratch/rows"
    done < <(git diff --name-status --no-renames "$base" "$head")
done
while IFS= read -r path; do
    case $path in ''|'#'*) continue;; esac
    mapfile -t pinned < <(identity "$cudapp" "$pin" "$path")
    working=$(sha256sum "$cudapp/$path" | cut -d' ' -f1)
    [[ -n ${pinned[0]} && $working = "${pinned[1]}" ]] || { echo "canonical byte mismatch $path" >&2; exit 1; }
    jq -nc --arg path "$path" --arg oid "${pinned[0]}" --arg hash "${pinned[1]}" \
      '{path:$path,git_blob:$oid,sha256:$hash,working_sha256:$hash}' >> "$scratch/canonical"
done < cudapp.files
prior_hash=$(sha256sum "$scratch/prior.json" | cut -d' ' -f1)
jq -n --arg omega "$omega" --arg cudapp "$pin" --arg prior_hash "$prior_hash" --slurpfile rows "$scratch/rows" --slurpfile canonical "$scratch/canonical" \
  '{scope:"Full changed-path carry audit; canonical divergences retained for review. Host evidence only.",omega:$omega,cudapp:$cudapp,
  prior_matrix:{path:"evidence/canonical-motor/source-carry.json",sha256:$prior_hash},
  prior_selected_audit:"CUDApp roadmap/program-20261022@11d485ade01ac304e9570b9306744f99f3445662:evidence/roadmap-program/latest-carry-identity.json",
  summary:($rows|group_by([.pr,.classification])|map({key:("PR"+(.[0].pr|tostring)+":"+.[0].classification),value:length})|from_entries),
  rows:$rows,canonical:$canonical}' > "$output/carry-matrix.json"
{ printf 'PR\tPath\tDestination\tDisposition\tPrior carry\tSource SHA256\tDestination SHA256\n'
  jq -r '.rows[]|[.pr,.path,.destination_repo,.classification,(.prior_carry_action//""),(.source.sha256//"DELETED"),.destination.sha256]|@tsv' "$output/carry-matrix.json"
} > "$output/carry-matrix.tsv"
jq '{omega,cudapp,changed_paths:(.rows|length),mapped:(.canonical|length),summary}' "$output/carry-matrix.json"
