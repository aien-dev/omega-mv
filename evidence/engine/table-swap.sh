#!/bin/bash
# L312: a 30b-swap-*/ folder -> the markdown table for ENGINE.md section 7 (one row per loader lead).
set -u
D=${1:?folder}
echo "| loader lead (tokens before the swap) | steady ms/token before | first token after swap: extra | total extra the user sees | hit rate before | hit rate after | first all-RAM token after swap | head B load | B load start vs swap |"
echo "|---|---|---|---|---|---|---|---|---|"
for f in "$D"/lead*/summary.json; do
  [ -f "$f" ] || continue
  l=$(basename "$(dirname "$f")"); l=${l#lead}
  jq -r --arg l "$l" 'def ms(x): if x == null then "-" else ((x*100|round)/100|tostring) + " ms" end;
    "| \($l) | \(ms(.steady_token_ms_before_swap)) | \(ms(.first_token_after_swap_extra_ms)) | \(ms(.user_visible_extra_ms_total)) | \((.hit_rate_before_swap*1000|round)/1000) | \((.hit_rate_after_swap*1000|round)/1000) | \(.first_all_hit_token_after_swap // "never") | \(ms(.head_b_load_ms)) | \(ms(.head_b_load_start_vs_swap_ms)) |"' "$f"
done
