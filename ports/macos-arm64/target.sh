#!/bin/sh
# Disclosed Linux host file orchestration. Refusals preserve existing output.
set -eu
if [ "$#" -ne 2 ]; then
    echo 'usage: target.sh INPUT.elf OUTPUT.macho' >&2
    exit 64
fi
adapter=$(CDPATH= cd -- "$(dirname "$0")" && pwd)/build/adapter
if [ -d "$2" ]; then
    echo 'target: output must not be a directory' >&2
    exit 73
fi
stage=$(mktemp "$2.L615.XXXXXX")
trap 'rm -f "$stage"' EXIT HUP INT TERM
"$adapter" < "$1" > "$stage"
test -s "$stage"
chmod 755 "$stage"
# GNU no-target-directory also fails closed if a directory appears after preflight.
mv -T -- "$stage" "$2"
