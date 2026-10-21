#!/bin/bash
# L321: turn CELL lines (run.log of cpu_piston_share_test) into one markdown table per metric:
# rows = buffer size, columns = arm@buffer (median; chase in ns per line, seq and gpu in GB/s). No Python (Drake's rule).
#   bash table.sh RUN.LOG [METRIC]   METRIC = chase_ns | seq_gbs | gpu_gbs(llrd=ns) (default: all three)
set -u
LOG=${1:?run.log}; ONLY=${2:-}
awk -v only="$ONLY" '
/^CELL / { delete f; for (i = 2; i <= NF; i++) { split($i, kv, "="); f[kv[1]] = substr($i, length(kv[1]) + 2) }
  m = f["metric"]; if (only != "" && m != only) next
  col = f["arm"] "@" f["buf"]; key = m SUBSEP f["size"] SUBSEP col
  v[key] = f["med"]; if (!(m SUBSEP col in seen)) { seen[m SUBSEP col] = 1; cols[m] = cols[m] " " col }
  if (!(m SUBSEP f["size"] in rs)) { rs[m SUBSEP f["size"]] = 1; rows[m] = rows[m] " " f["size"] }
  ms[m] = 1 }
END { for (m in ms) { printf "\n### %s\n\n| size |", m; nc = split(cols[m], C, " "); for (c = 1; c <= nc; c++) printf " %s |", C[c]; printf "\n|---|"; for (c = 1; c <= nc; c++) printf "---|"; printf "\n"
    nr = split(rows[m], R, " "); for (r = 1; r <= nr; r++) { s = R[r]; printf "| %s |", (s >= 1048576 ? s / 1048576 " MB" : s / 1024 " KB")
      for (c = 1; c <= nc; c++) { k = m SUBSEP s SUBSEP C[c]; printf " %s |", (k in v ? v[k] : "") } printf "\n" } } }' "$LOG"
