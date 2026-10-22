#!/bin/sh
# Reproduce the root seed only. Run on AArch64 Linux with a pinned seed checkout.
# This is test/bootstrap glue, not source compiled into the seed.
set -eu
[ "$#" -eq 1 ] || { printf 'usage: sh verify-root.sh /path/to/pinned/omega\n' >&2; exit 64; }
src=$1
[ "$(uname -m)" = aarch64 ] || { printf 'NOT_RUN: requires AArch64 Linux\n' >&2; exit 3; }
[ "$(uname -s)" = Linux ] || exit 3
base=${TMPDIR:-"$HOME/.cache"}
mkdir -p "$base"
work=$(mktemp -d "$base/omega-root-proof.XXXXXX")
trap 'rm -f "$work/root" "$work/self"; rmdir "$work"' EXIT HUP INT TERM
sh "$src/seed/rung0/load0.sh" "$src/seed/rung0/seed0.hex" "$work/root"
[ -s "$work/root" ]
chmod +x "$work/root"
"$work/root" < "$src/seed/rung0/seed0.hex" > "$work/self"
cmp "$work/root" "$work/self"
cmp "$work/root" "$src/seed/rung0/seed0.bin"
size=$(wc -c < "$work/root" | tr -d ' ')
[ "$size" = 408 ]
actual=$(sha256sum "$work/root"); actual=${actual%% *}
[ "$actual" = aca00cf3677236809f0f223437398c6bb429e4c65f13d58762bcf96d13799474 ]
printf 'root_bytes=%s\nroot_sha256=%s\nroot_self_rebuild=IDENTICAL\ncommitted_root=IDENTICAL\nRC=0\n' "$size" "$actual"
