#!/bin/sh
# load0.sh <seed0.hex> <out.bin> -- the one outside tool of the seed chain (named in seed/README.md).
# Used once, to turn the hand-written seed0.hex into seed0.bin before any seed program exists.
# POSIX shell and its printf only (no gcc, as, ld, xxd). Strict: after the comment ('#' to end of line)
# is cut, a line is empty or holds exactly one word of 8 hex digits; anything else stops the load.
# Each word goes out lowest byte first (little-endian), as seed0 itself writes it.
set -eu
[ $# -eq 2 ] || { echo "usage: load0.sh <seed0.hex> <out.bin>" >&2; exit 2; }
n=0
: > "$2"
while IFS= read -r line || [ -n "$line" ]; do
  n=$((n + 1))
  w=${line%%#*}
  w=$(printf '%s' "$w" | tr -d ' \t\r')
  [ -z "$w" ] && continue
  case $w in
    [0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F]) ;;
    *) echo "load0: line $n: not one 8-digit hex word: '$w'" >&2; exit 1 ;;
  esac
  v=$((0x$w))
  for s in 0 8 16 24; do
    printf "\\$(printf '%03o' $(( (v >> s) & 255 )))"
  done >> "$2"
done < "$1"
