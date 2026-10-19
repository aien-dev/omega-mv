#!/bin/bash
# memfrag_sample.sh -- host memory fragmentation sampler (omega#528).
# Read-only: it reads /proc and the judge heartbeat, never drops caches,
# compacts, or writes a sysctl.
#
#   tools/memfrag_sample.sh <out.tsv> [interval_s] [count]
#
# One tab-separated line per sample (header written once):
#   utc            sample time
#   free_2m        free 2 MiB-or-larger blocks in the Normal zone, counted in
#                  2 MiB units (sum over orders 9.. of n << (order - 9))
#   free_o0_3_mb   free memory sitting in orders 0..3 (MiB), the fragments
#   free_o4_8_mb   free memory in orders 4..8 (64 KiB to 1 MiB pieces, MiB): the
#                  size of the holes freed 64 KiB GPU pieces leave behind
#   blk_unmov blk_mov blk_recl blk_cma   pageblock counts per migrate type
#                  (/proc/pagetypeinfo is root-only: read with `sudo -n cat`
#                  when allowed, "NA" otherwise)
#   memfree_mb slab_mb srecl_mb sunrecl_mb pgtables_mb cached_mb anon_mb
#   compact_stall compact_fail compact_success pgmigrate_success pgmigrate_fail
#   foll_pin_acq foll_pin_rel   /proc/vmstat counters (cumulative)
#   gpu_map_mb     RssFile of every process with /dev/nvidiactl mapped: on
#                  GB10 this is the driver's system pages mapped to the CPU
#   gpu_procs      name:pid:MiB for each such process, comma separated
#   judge_phase    phase from the local judge's worker.heartbeat
set -u
OUT=${1:?usage: memfrag_sample.sh <out.tsv> [interval_s] [count]}
IVL=${2:-10}
COUNT=${3:-0}                 # 0 = run until killed
PROC=${MEMFRAG_PROC:-/proc}   # tests point this at a fixture tree
HEARTBEAT=${MEMFRAG_HEARTBEAT:-$HOME/.local/state/aien-local-ci/worker.heartbeat}

# free_2m, free_o0_3_mb and free_o4_8_mb from buddyinfo (Normal zone; 4 KiB pages)
buddy_fields() {
    awk '$4 == "Normal" {
        for (o = 0; o + 5 <= NF; o++) {
            n = $(o + 5)
            if (o >= 9) big += n * 2 ^ (o - 9)
            if (o <= 3) small += n * 2 ^ o
            else if (o <= 8) mid += n * 2 ^ o
        }
    } END { printf "%d\t%d\t%d", big, small * 4 / 1024, mid * 4 / 1024 }' "$PROC/buddyinfo"
}

block_fields() {
    local pti
    if [ -r "$PROC/pagetypeinfo" ]; then
        pti=$(cat "$PROC/pagetypeinfo")
    elif [ "$PROC" = /proc ] && pti=$(sudo -n cat /proc/pagetypeinfo 2>/dev/null); then
        :
    else
        printf 'NA\tNA\tNA\tNA'; return
    fi
    printf '%s\n' "$pti" | awk '
        /^Number of blocks type/ {
            for (i = 5; i <= NF; i++) col[$i] = i
            hdr = 1; next
        }
        hdr && $4 == "Normal" {
            printf "%s\t%s\t%s\t%s", $(col["Unmovable"]), $(col["Movable"]),
                $(col["Reclaimable"]), $(col["CMA"]) == "" ? 0 : $(col["CMA"])
            found = 1
        }
        END { if (!found) printf "NA\tNA\tNA\tNA" }'
}

meminfo_fields() {
    awk '{ v[$1] = $2 } END {
        printf "%d\t%d\t%d\t%d\t%d\t%d\t%d",
            v["MemFree:"] / 1024, v["Slab:"] / 1024, v["SReclaimable:"] / 1024,
            v["SUnreclaim:"] / 1024, v["PageTables:"] / 1024, v["Cached:"] / 1024,
            v["AnonPages:"] / 1024 }' "$PROC/meminfo"
}

vmstat_fields() {
    awk '{ v[$1] = $2 } END {
        printf "%d\t%d\t%d\t%d\t%d\t%d\t%d",
            v["compact_stall"], v["compact_fail"], v["compact_success"],
            v["pgmigrate_success"], v["pgmigrate_fail"],
            v["nr_foll_pin_acquired"], v["nr_foll_pin_released"] }' "$PROC/vmstat"
}

# Processes with the driver's control node mapped. Only processes whose maps
# we may read are seen; the rest are skipped silently (ownership, not error).
gpu_fields() {
    local total=0 list="" d pid kb name
    for d in "$PROC"/[0-9]*; do
        pid=${d##*/}
        grep -q nvidiactl "$d/maps" 2>/dev/null || continue
        kb=$(awk '/^RssFile:/ { print $2 }' "$d/status" 2>/dev/null)
        [ -n "$kb" ] || continue
        name=$(cat "$d/comm" 2>/dev/null)
        total=$((total + kb))
        list="$list${list:+,}${name// /_}:$pid:$((kb / 1024))"
    done
    printf '%d\t%s' $((total / 1024)) "${list:--}"
}

judge_phase() {
    local p
    p=$(sed -n 's/.*"phase":"\([^"]*\)".*/\1/p' "$HEARTBEAT" 2>/dev/null)
    printf '%s' "${p:-NA}"
}

if [ ! -s "$OUT" ]; then
    printf 'utc\tfree_2m\tfree_o0_3_mb\tfree_o4_8_mb\tblk_unmov\tblk_mov\tblk_recl\tblk_cma\tmemfree_mb\tslab_mb\tsrecl_mb\tsunrecl_mb\tpgtables_mb\tcached_mb\tanon_mb\tcompact_stall\tcompact_fail\tcompact_success\tpgmigrate_success\tpgmigrate_fail\tfoll_pin_acq\tfoll_pin_rel\tgpu_map_mb\tgpu_procs\tjudge_phase\n' >"$OUT"
fi

n=0
while :; do
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
        "$(buddy_fields)" "$(block_fields)" "$(meminfo_fields)" \
        "$(vmstat_fields)" "$(gpu_fields)" "$(judge_phase)" >>"$OUT"
    n=$((n + 1))
    [ "$COUNT" -gt 0 ] && [ "$n" -ge "$COUNT" ] && break
    sleep "$IVL"
done
