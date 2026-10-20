#!/bin/sh
# L308 H5 (the lossy typewriter): held-out text of each domain scored with routing restricted to
# a head (own domain head, the general-purpose head of equal size, a random head of equal size).
# OUTPUT DIFFERS FROM THE MODEL by design. Host reference, CPU only, niced onto the A725 cores.
# usage: run-masked.sh <heads.json> <tag>
set -e
T=$HOME/workspace/targets/sc-typewriter-b/release/typewriter
M=$HOME/models/qwen3.6-35b-a3b-fp8-95a723d0
E=$(cd "$(dirname "$0")" && pwd)
H=$1; TAG=$2
sha256sum "$T" > "$E/masked/typewriter-$TAG.sha256"
for d in code math prose omega; do
  for kind in "$d" "general-for-$d" "random-for-$d"; do
    out="$E/masked/$TAG-$d-held-with-$kind.json"
    [ -s "$out" ] && continue
    jq --arg k "$kind" '{layers: .heads[$k].layers}' "$H" > "/tmp/l308/head-$TAG-$kind.json"
    nice -n 19 taskset -c 0-4,10-14 "$T" routes "$M" "$E/texts/$d-held.txt" 1024 "$out" "/tmp/l308/head-$TAG-$kind.json" 2>> "$E/masked/masked-$TAG.log"
  done
done
echo DONE >> "$E/masked/masked-$TAG.log"
