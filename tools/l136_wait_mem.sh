#!/bin/sh
# L136: wait until MemAvailable >= $1 GiB for 60 s in a row, then run the rest of the command line.
need=$(( $1 * 1048576 )); shift; ok=0
while [ $ok -lt 6 ]; do
    a=$(awk '/MemAvailable/ {print $2}' /proc/meminfo)
    if [ "$a" -ge "$need" ]; then ok=$((ok + 1)); else ok=0; fi
    sleep 10
done
echo "mem ok $(date -u +%FT%TZ) $(grep MemAvailable /proc/meminfo)"
exec "$@"
