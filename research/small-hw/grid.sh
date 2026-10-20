#!/usr/bin/env bash
# Prints the target x model x format grid of docs/speed/SMALL_HARDWARE.md section 4b with an aien-calc receipt per cell.
# Rule: fits if whole + overhead <= memory (overhead 1.7 GB: 1 GB system, 0.4 GB KV, 0.3 GB scratch; the 5070 card
# 0.9 GB: no system, 0.5 GB scratch). Fits -> ceiling = memory speed / active bytes. Dense model that does not fit ->
# every word reads the part that stays on disk again: time = active / memory speed + (active - room) / disk speed.
# MoE that does not fit -> expert cache, see section 5 (W4 only has a measured hit rate).
set -u
calc() { aien-calc eval "$1" | awk '/^decimal/{d=$2} /^receipt/{r=substr($2,1,13)} END{printf "%.3g (%s)", d, r}'; }
# name mem_GB bw_GB/s disk_GB/s overhead_GB
targets="Pi5-8GB:8:17.068:0.45:1.7 Pi5-16GB:16:17.068:0.45:1.7 M2Air-8GB:8:100:1.45:1.7 M2Air-24GB:24:100:2.9:1.7 RTX5070-12GB:12:672:63:0.9 Laptop-8GB:8:51.2:3:1.7"
# name whole_GB active_GB moe
models="27B-BF16:51.2027968:51.2027968:0 27B-FP8:26.89:26.89:0 27B-W4:13.65:13.65:0 27B-W2:7.5182429:7.5182429:0
30B-BF16:61.06382336:6.083313664:1 30B-FP8:31.170363392:3.365404672:1 30B-W4:16.69558272:1.634361344:1 30B-W2:9.221332992:0.954884096:1
8B-BF16:16.380854272:15.13619456:0 8B-FP8:9.435086848:8.190427136:0 8B-W4:5.265211392:4.02055168:0 8B-W2:3.528769536:2.284109824:0"
printf "| Model | %s |\n" "$(for t in $targets; do printf "%s | " "${t%%:*}"; done | sed 's/ | $//')"
printf "|---|%s\n" "$(for t in $targets; do printf -- "---|"; done)"
for m in $models; do
  IFS=: read -r mn whole act moe <<<"$m"
  row="| $mn |"
  for t in $targets; do
    IFS=: read -r tn mem bw disk ov <<<"$t"
    room=$(aien-calc eval "$mem - $ov" | awk '/^decimal/{print $2}')
    fits=$(awk -v w="$whole" -v r="$room" 'BEGIN{print (w<=r)?1:0}')
    if [ "$fits" = 1 ]; then
      row="$row fits: $(calc "$bw GB/s / $act GB/tok -> tok/s") |"
    elif [ "$moe" = 1 ]; then
      row="$row streams experts (section 5) |"
    else
      ondisk=$(awk -v a="$act" -v r="$room" 'BEGIN{d=a-r; if(d<0)d=0; printf "%.6f", d}')
      row="$row streams $ondisk GB/word: $(calc "1 tok / ($act GB / $bw GB/s + $ondisk GB / $disk GB/s) -> tok/s") |"
    fi
  done
  echo "$row"
done
