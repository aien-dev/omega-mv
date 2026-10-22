#!/bin/sh
# omega_tree.sh <dir>: make <dir> a checkout of Omega at the commit in omega.lock, with every file of this
# repo (except its own README, LICENSE, AUDIT, Makefile, lock and this script) laid over it at the same
# path, so Omega's Makefile and its late fragments (mk/late/*.mk) build and test OSH exactly as
# they did inside Omega. The commit is looked for in $OMEGA_REPO, ../omega, ../omega-mv, then fetched from
# the "# repo:" URL into build/pins/omega.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
dir=${1:?usage: omega_tree.sh <dir>}
sha=$(awk 'NF && $1 !~ /^#/ {print $1; exit}' "$here/omega.lock")
url=$(sed -n 's/^# repo: *//p' "$here/omega.lock" | head -1)
case "$sha" in *[!0-9a-f]* | "") echo "omega_tree: omega.lock line 1 is not a commit id" >&2; exit 2 ;; esac
[ ${#sha} -eq 40 ] || { echo "omega_tree: omega.lock needs the full 40-hex commit" >&2; exit 2; }
repo=""
for d in "${OMEGA_REPO:-}" "$here/../omega" "$here/../omega-mv" "$here/build/pins/omega"; do
    [ -n "$d" ] && [ -d "$d" ] && git -C "$d" cat-file -e "$sha^{commit}" 2>/dev/null && { repo=$d; break; }
done
if [ -z "$repo" ]; then
    repo=$here/build/pins/omega
    [ -d "$repo" ] || git init -q --bare "$repo"
    git -C "$repo" fetch -q "$url" "$sha" 2>/dev/null || git -C "$repo" fetch -q "$url" '+refs/heads/*:refs/heads/*'
    git -C "$repo" cat-file -e "$sha^{commit}" || { echo "omega_tree: $url has no commit $sha" >&2; exit 2; }
fi
if [ ! -d "$dir/.git" ] && [ ! -f "$dir/.git" ]; then
    rm -rf "$dir"; git clone -q --shared --no-checkout "$repo" "$dir"
fi
git -C "$dir" -c advice.detachedHead=false checkout -q --force --detach "$sha"
git -C "$dir" clean -q -fdx -e build/
(cd "$here" && git ls-files) | grep -v -x -E 'README\.md|LICENSE|AUDIT\.md|Makefile|omega\.lock|\.gitignore|tools/omega_tree\.sh' |
while read -r f; do
    mkdir -p "$dir/$(dirname "$f")"; cp -p "$here/$f" "$dir/$f"
done
# Commit the overlay locally so the tree is clean: some programs refuse a dirty tree before writing a receipt.
git -C "$dir" add -A
git -C "$dir" -c user.name=omega_tree -c user.email=omega_tree@localhost commit -q --allow-empty -m "omega-host overlay"
echo "omega_tree: $dir = omega $sha + omega-host $(cd "$here" && git rev-parse --short=12 HEAD 2>/dev/null || echo worktree)"
