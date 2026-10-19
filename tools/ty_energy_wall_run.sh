#!/bin/sh
# TY-4/TY-5 wall amendment (docs/turing/TURING_YIELD_ENERGY_PROTOCOL_V0.md,
# amendment 2): stage 2 only, S6 then S2, 3 rounds each, 30 s windows, with
# the unchanged window tool and workload. Before each window it records the
# epoch/boottime offset so tools/ty_energy_wall.c can place the window's
# monotonic GO time on the sensor log. Creates no quiet flag (run it under
# quiet-measure). usage: tools/ty_energy_wall_run.sh OUTDIR [ROUNDS]
set -u
OUT=$1; ROUNDS=${2:-3}
BIN=${TYE_BIN:-build/ty}
WIN=$BIN/ty_energy_window; WL=$BIN/ty_workload
[ -x "$WIN" ] && [ -x "$WL" ] || { echo "missing $WIN or $WL (make ty-energy-tools)" >&2; exit 2; }
mkdir -p "$OUT/raw"
{
	echo "date_utc $(date -u +%FT%TZ)"
	echo "git_head $(git rev-parse HEAD 2>/dev/null)"
	for b in "$WIN" "$WL"; do echo "bin $(sha256sum "$b")"; done
	echo "kernel $(uname -r)"
	echo "hwmon_spbm $(grep -l '^aien_spbm$' /sys/class/hwmon/*/name 2>/dev/null | head -1)"
	echo "seeds S6 0x7459520001 S2 0x7459520002 rounds $ROUNDS"
} > "$OUT/manifest.txt"
run_cfg() {
	cfg=$1; seed=$2; sa=$3; sb=$4
	"$WIN" --plan "$seed" "$ROUNDS" > "$OUT/raw/plan_$cfg.txt"
	while read -r r p c; do
		f=$(printf '%s/raw/s2_%s_r%02d_p%d_%s.jsonl' "$OUT" "$cfg" "$r" "$p" "$c")
		up=$(cut -d' ' -f1 /proc/uptime); ep=$(date +%s%N)
		echo "$(basename "$f") $ep $up" >> "$OUT/offsets.txt"
		"$WIN" --out "$f" --run tyw-s2 --config "$cfg" --cond "$c" --round "$r" --pos "$p" \
			--secs 30 --workload "$WL" --a "$sa" --b "$sb" --tool-cpu 0 \
			|| echo "window $f rc=$? (meter absent is expected; kept)" >> "$OUT/run.log"
		sleep 3
	done < "$OUT/raw/plan_$cfg.txt"
}
run_cfg S6 0x7459520001 R1_plain:5,6,7,8,9 R2c_crumb:15,16,17,18,19
run_cfg S2 0x7459520002 R1_plain:5 R2c_crumb:15
(cd "$OUT/raw" && sha256sum *.jsonl > SHA256SUMS)
echo "done $(date -u +%FT%TZ)" >> "$OUT/run.log"
