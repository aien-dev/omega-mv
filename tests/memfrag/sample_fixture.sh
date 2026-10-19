#!/bin/bash
# Fixture test for tools/memfrag_sample.sh (omega#528): one sample against a fake /proc tree,
# every field checked. Negative controls: a process without the driver node mapped is not
# counted, and a tree without pagetypeinfo gives NA block counts instead of guessing.
set -eu
here=$(cd "$(dirname "$0")/../.." && pwd)
t=$(mktemp -d /tmp/memfrag-sample-XXXXXX)
trap 'rm -rf "$t"' EXIT
p=$t/proc
mkdir -p "$p/123" "$p/456"
# Normal zone: 1024 free order-0 pages + 128 order-3 = 8 MiB of fragments; 3 order-9 + 1 order-10
# + 1 order-13 = 3 + 2 + 16 = 21 free 2 MiB blocks; 64 order-4 = 4 MiB of 64 KiB holes
cat >"$p/buddyinfo" <<'B'
Node 0, zone      DMA      2      4      4     32     21     42     11      7      3      6     10      3      1     12 
Node 0, zone   Normal   1024      0      0    128     64      0      0      0      0      3      1      0      0      1 
B
cat >"$p/pagetypeinfo" <<'B'
Page block order: 9
Pages per block:  512

Free pages count per migrate type at order       0      1      2      3      4      5      6      7      8      9     10     11     12     13 
Node    0, zone   Normal, type    Unmovable      9      9      9      9      0      0      0      0      0      0      0      0      0      0 

Number of blocks type     Unmovable      Movable  Reclaimable   HighAtomic          CMA      Isolate 
Node 0, zone      DMA           49          707           16            0           64            0 
Node 0, zone   Normal        24031        34393         1391            0         4697            0 
B
printf 'MemFree:  2048000 kB\nSlab:  1024000 kB\nSReclaimable:  512000 kB\nSUnreclaim:  512000 kB\nPageTables:  10240 kB\nCached:  4096000 kB\nAnonPages:  3072000 kB\n' >"$p/meminfo"
printf 'compact_stall 11\ncompact_fail 7\ncompact_success 4\npgmigrate_success 100\npgmigrate_fail 9\nnr_foll_pin_acquired 55\nnr_foll_pin_released 55\n' >"$p/vmstat"
printf '10000000-10040000 rw-s 00000000 00:06 7 /dev/nvidiactl\n' >"$p/123/maps"
printf 'Name:\tserver\nRssFile:\t35465776 kB\n' >"$p/123/status"
printf 'aien-serve-http\n' >"$p/123/comm"
printf '20000000-20001000 rw-p 00000000 00:00 0 [heap]\n' >"$p/456/maps"      # no driver node
printf 'RssFile:\t999999 kB\n' >"$p/456/status"
printf 'other\n' >"$p/456/comm"
printf '{"phase":"gate","pid":1,"repo":"omega","sha":"-","utc":"x"}' >"$t/hb"

MEMFRAG_PROC=$p MEMFRAG_HEARTBEAT=$t/hb "$here/tools/memfrag_sample.sh" "$t/out.tsv" 1 1
fail=0
check() { # name expected
    got=$(awk -F'\t' -v n="$1" 'NR == 1 { for (i = 1; i <= NF; i++) c[$i] = i } NR == 2 { print $(c[n]) }' "$t/out.tsv")
    if [ "$got" != "$2" ]; then echo "FAIL: $1 = '$got', expected '$2'"; fail=1; fi
}
[ "$(wc -l <"$t/out.tsv")" = 2 ] || { echo "FAIL: expected header + one sample"; fail=1; }
check free_2m 21
check free_o0_3_mb 8
check free_o4_8_mb 4
check blk_unmov 24031
check blk_mov 34393
check blk_recl 1391
check blk_cma 4697
check memfree_mb 2000
check slab_mb 1000
check sunrecl_mb 500
check compact_stall 11
check pgmigrate_fail 9
check foll_pin_acq 55
check gpu_map_mb 34634
check gpu_procs aien-serve-http:123:34634
check judge_phase gate

rm "$p/pagetypeinfo" "$t/out.tsv"
MEMFRAG_PROC=$p MEMFRAG_HEARTBEAT=$t/missing "$here/tools/memfrag_sample.sh" "$t/out.tsv" 1 1
check blk_unmov NA
check judge_phase NA
check free_2m 21

[ "$fail" = 0 ] || { echo "MEMFRAG_SAMPLE FAIL"; exit 1; }
echo "MEMFRAG_SAMPLE PASS"
