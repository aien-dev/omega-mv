#!/bin/sh
# SPDX-License-Identifier: MIT
# Bind Omega GPU paths to one exact, clean CUDApp commit. Fail closed: no
# implicit download, local source fallback, disabled pin check or shadow GPU file.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
dir=${1:?usage: cudapp-link.sh CUDAPP_DIR}
case $dir in /*) ;; *) dir=$root/$dir ;; esac
fail() { printf 'cudapp-link: %s\n' "$*" >&2; exit 1; }
[ "${CUDAPP_LOCK_CHECK:-1}" = 1 ] || fail 'CUDAPP_LOCK_CHECK must be 1 for canonical builds'
[ -f "$root/cudapp.lock" ] && [ -f "$root/cudapp.files" ] || fail 'missing cudapp.lock or cudapp.files'
want=$(cat "$root/cudapp.lock")
[ "${#want}" = 40 ] || fail 'cudapp.lock must contain a full 40-hex commit'
case $want in *[!0-9a-f]*) fail 'cudapp.lock must contain a lowercase hex commit';; esac
[ -d "$dir/src" ] || fail "missing CUDApp checkout at $dir; prepare the pinned dependency explicitly"
dir=$(cd "$dir" && pwd -P)
got=$(git -C "$dir" rev-parse HEAD 2>/dev/null) || fail 'CUDApp is not a git checkout'
[ "$got" = "$want" ] || fail "CUDApp HEAD $got differs from pin $want"
dirty=$(git -C "$dir" status --porcelain --untracked-files=no)
[ -z "$dirty" ] || fail 'CUDApp has tracked local edits, including build metadata'
# Validate the complete map before creating any links.
count=0
while IFS= read -r p || [ -n "$p" ]; do
    case $p in ''|\#*) continue;; esac
    case $p in /*|../*|*/../*|*/..|./*|*/./*|*[!A-Za-z0-9_./-]*) fail "unsafe mapped path: $p";; esac
    [ -f "$dir/$p" ] && [ ! -L "$dir/$p" ] || fail "mapped source is missing or a symlink: $p"
    git -C "$dir" ls-files --error-unmatch -- "$p" >/dev/null 2>&1 || fail "mapped source is not committed: $p"
    resolved=$(realpath "$dir/$p")
    case $resolved in "$dir"/*) ;; *) fail "mapped source escapes CUDApp: $p";; esac
    dirty=$(git -C "$dir" status --porcelain --untracked-files=all -- "$p")
    [ -z "$dirty" ] || fail "mapped source has local edits: $p"
    [ ! -e "$root/$p" ] || [ -L "$root/$p" ] || fail "mapped path is a real file in Omega: $p"
    count=$((count+1))
done < "$root/cudapp.files"
[ "$count" -gt 0 ] || fail 'empty source map'
# No leftover real/alternate GPU implementation may enter Omega's source glob.
for f in "$root"/src/omega_gpu_* "$root"/src/omega_blackwell_* "$root"/src/omega_sm121_disasm.*; do
    [ -e "$f" ] || [ -L "$f" ] || continue
    p=${f#"$root"/}
    if grep -Fxq "$p" "$root/cudapp.files"; then continue; fi
    if [ -f "$root/cudapp.stay" ] && grep -Fxq "$p" "$root/cudapp.stay"; then continue; fi
    fail "unmapped GPU implementation in Omega: $p"
done
while IFS= read -r p || [ -n "$p" ]; do
    case $p in ''|\#*) continue;; esac
    mkdir -p "$root/$(dirname "$p")"
    target=$(realpath -m --relative-to="$root/$(dirname "$p")" "$dir/$p")
    ln -sfn "$target" "$root/$p"
done < "$root/cudapp.files"
printf 'cudapp-link: canonical %s (%s mapped files)\n' "$want" "$count" >&2
