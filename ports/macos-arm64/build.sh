#!/bin/sh
# Disclosed Linux host orchestration. Production builder is pinned seeded Omega.
set -eu
cd "$(dirname "$0")"
compiler=/home/drakestapleton/handoffs/omega-developer-gaps/shared/omc-191b776e
printf '%s  %s\n' 191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9 "$compiler" | sha256sum -c - >&2
mkdir -p build
stage=$(mktemp build/adapter.XXXXXX)
trap 'rm -f "$stage"' EXIT HUP INT TERM
quiet-build timeout 60 "$compiler" < adapter.om > "$stage"
test -s "$stage"
chmod 755 "$stage"
mv "$stage" build/adapter
