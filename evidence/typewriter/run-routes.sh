#!/bin/sh
# L308 H1: routes of the 35B on 8 texts (4 domains x fit/held-out), host reference, CPU only.
# Not a timing run: niced onto the A725 cores so the X925 timing cores stay free.
set -e
T=$HOME/workspace/targets/sc-typewriter/release/typewriter
M=$HOME/models/qwen3.6-35b-a3b-fp8-95a723d0
E=$(cd "$(dirname "$0")" && pwd)
sha256sum "$T" > "$E/routes/typewriter.sha256"
for d in code math prose omega; do
  for s in fit held; do
    [ -s "$E/routes/$d-$s.json" ] && continue
    nice -n 19 taskset -c 0-4,10-14 "$T" routes "$M" "$E/texts/$d-$s.txt" 1024 "$E/routes/$d-$s.json" 2>> "$E/routes/routes.log"
  done
done
echo DONE >> "$E/routes/routes.log"
