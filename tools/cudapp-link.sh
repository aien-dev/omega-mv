#!/bin/sh
# cudapp-link.sh CUDAPP_DIR: make Omega whole again from the pinned CUDA++ checkout.
# The GPU toolchain, runtime and libraries moved to CUDA++ (aien-dev/CUDAplusplus, MIT) on
# 2026-10-21. Omega keeps building them from the same paths: every path in cudapp.files becomes a
# symbolic link to the same path in CUDAPP_DIR, so every Makefile rule and script works unchanged.
# Checks first (unless CUDAPP_LOCK_CHECK=0): CUDAPP_DIR is a git checkout at the commit in cudapp.lock
# and has no local edits under the linked paths. Make runs this on every invocation (Makefile top).
#
# No CUDA++ (CUDAPP_DIR=/nonexistent, like PHYSICS_DIR=/nonexistent, or offline on first use): a note,
# nothing linked, exit 0; host lanes that need no GPU file still build, the others fail on the
# missing file. Exit non-zero only for a wrong commit, local edits, or a real file in a linked path.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
dir=${1:?usage: cudapp-link.sh CUDAPP_DIR}
case $dir in /*) ;; *) dir=$root/$dir ;; esac
want=$(tr -d '[:space:]' < "$root/cudapp.lock")

if [ ! -d "$dir/src" ] && [ "$dir" = "$root/build/cudapp" ]; then
	# first use from a fresh omega checkout: fetch CUDA++ at the pinned commit (https, then ssh while the repo is private)
	mkdir -p "$root/build"
	if git clone -q "${CUDAPP_URL:-https://github.com/aien-dev/CUDAplusplus}" "$dir" 2>/dev/null ||
		git clone -q git@github.com:aien-dev/CUDAplusplus.git "$dir" 2>/dev/null; then
		git -C "$dir" checkout -q "$want" || { echo "cudapp-link: CUDA++ has no commit $want" >&2; exit 1; }
		echo "cudapp-link: fetched CUDA++ $want into $dir" >&2
	else
		rm -rf "$dir"
	fi
fi
if [ ! -d "$dir/src" ]; then
	echo "cudapp-link: note: no CUDA++ at CUDAPP_DIR=$1; GPU files not linked (clone git@github.com:aien-dev/CUDAplusplus.git next to omega for GPU targets)" >&2
	exit 0
fi

if [ "${CUDAPP_LOCK_CHECK:-1}" != 0 ]; then
	got=$(git -C "$dir" rev-parse HEAD 2>/dev/null) || { echo "cudapp-link: $dir is not a git checkout" >&2; exit 1; }
	[ "$got" = "$want" ] || { echo "cudapp-link: CUDAPP_DIR=$dir is at $got but cudapp.lock pins $want (git -C $dir checkout $want, or CUDAPP_LOCK_CHECK=0)" >&2; exit 1; }
	dirty=$(git -C "$dir" status --porcelain -- $(grep -v '^#' "$root/cudapp.files") | head -3)
	[ -z "$dirty" ] || { echo "cudapp-link: CUDAPP_DIR has local edits in linked files (commit them in CUDA++ and move cudapp.lock):" >&2; echo "$dirty" >&2; exit 1; }
fi

grep -v '^#' "$root/cudapp.files" | while IFS= read -r p; do
	[ -n "$p" ] || continue
	[ -e "$dir/$p" ] || { echo "cudapp-link: $p missing in CUDAPP_DIR" >&2; exit 1; }
	target=$(realpath -m --relative-to="$root/$(dirname "$p")" "$dir/$p")
	if [ -L "$root/$p" ] && [ "$(readlink "$root/$p")" = "$target" ]; then continue; fi
	[ ! -e "$root/$p" ] || [ -L "$root/$p" ] || { echo "cudapp-link: $p is a real file in omega; it belongs to CUDA++ (move the change there)" >&2; exit 1; }
	ln -sfn "$target" "$root/$p"
done
