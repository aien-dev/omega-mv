#!/bin/bash
# pin.sh -- use a module repo at the one commit its lock file pins (L396, ADR 0039 one thing per repo).
#
# A module that left omega is pinned by <name>.lock at the repo root:
#   line 1      the full 40-hex commit (the proof; a branch name or short id is refused)
#   # repo: <git url>                         where to fetch it when no local copy holds the commit
#   # map: <module path> <omega path>         (optional, repeatable) files or directories materialised
#                                             into omega's tree at their old paths, gitignored
# The commit object is the proof of identity: every byte read is read from that commit with
# GIT_NO_REPLACE_OBJECTS set, never from a working tree. Same rule as tools/aienos_lock_source.sh.
#
# Usage:
#   pin.sh ensure <name>                  materialise every map entry (only missing or wrong files are
#                                         rewritten), then prove each one equals the locked blob
#   pin.sh verify <name>                  prove the map entries in the tree equal the locked blobs
#   pin.sh show <name> <path>             print <path> at the locked commit
#   pin.sh moved <name> <base-ref> <dir>  every file under omega <dir>/ that HEAD deleted since <base-ref>
#                                         must exist byte-identical in the module at <path-without-dir>
#   pin.sh repo <name>                    print the git dir that holds the locked commit
# Where the commit is looked for, in order: $PIN_<NAME>_REPO, ../<repo name>, build/pins/<repo name>
# (fetched from the "# repo:" URL when missing). Exit: 0 proven; 1 mismatch or missing; 2 unavailable.
set -euo pipefail
export GIT_NO_REPLACE_OBJECTS=1

# Outside a git checkout (a test driving a private copy of the Makefile) the current directory is the root.
root=$(git rev-parse --show-toplevel 2> /dev/null || pwd -P)
cmd=${1:?usage: pin.sh ensure|verify|show|moved|repo <name> ...}
name=${2:?usage: pin.sh $cmd <name>}
lockf="$root/$name.lock"
[ -f "$lockf" ] || { echo "pin.sh: $name.lock not found" >&2; exit 2; }
sha=$(awk 'NF && $1 !~ /^#/ {print $1; exit}' "$lockf")
[[ "$sha" =~ ^[0-9a-f]{40}$ ]] || { echo "pin.sh: $name.lock line 1 is not a full commit id" >&2; exit 2; }
url=$(sed -n 's/^# repo: *//p' "$lockf" | head -1)
rname=$(basename "${url:-$name}" .git)

holds() { git -C "$1" cat-file -e "$sha^{commit}" 2> /dev/null; }
find_repo() {
    local var="PIN_$(printf '%s' "$name" | tr 'a-z-' 'A-Z_')_REPO" d
    for d in "${!var:-}" "$root/../$rname" "$root/build/pins/$rname"; do
        [ -n "$d" ] && [ -d "$d" ] && holds "$d" && { git -C "$d" rev-parse --absolute-git-dir; return 0; }
    done
    [ -n "$url" ] || return 1
    d="$root/build/pins/$rname"
    mkdir -p "$root/build/pins"
    (
        flock -x 9
        [ -d "$d" ] || git init -q --bare "$d"
        holds "$d" || git -C "$d" fetch -q "$url" "$sha" 2> /dev/null || git -C "$d" fetch -q "$url" '+refs/heads/*:refs/heads/*'
    ) 9> "$root/build/pins/.$rname.lock"
    holds "$d" && git -C "$d" rev-parse --absolute-git-dir
}
repo=$(find_repo) || { echo "pin.sh: no repository holds $name commit $sha (looked in ../$rname, build/pins/$rname, ${url:-no url})" >&2; exit 2; }
g() { git --git-dir="$repo" "$@"; }

# map entries expanded to file pairs: "<blob> <mode> <module file> <omega file>"
pairs() {
    sed -n 's/^# map: *//p' "$lockf" | while read -r src dst; do
        g ls-tree -r "$sha" -- "$src" | while read -r mode type blob path; do
            [ "$type" = blob ] || continue
            rel=${path#"$src"}; echo "$blob $mode $path $dst$rel"
        done
    done
}
blob_of() { git hash-object --no-filters -- "$1"; }

case "$cmd" in
repo) echo "$repo" ;;
show) g cat-file blob "$sha:${3:?path}" ;;
ensure | verify)
    bad=0
    while read -r blob mode src dst; do
        f="$root/$dst"
        if [ -f "$f" ] && [ "$(blob_of "$f")" = "$blob" ]; then continue; fi
        if [ "$cmd" = verify ]; then echo "pin.sh: $dst differs from $name@$sha:$src" >&2; bad=1; continue; fi
        mkdir -p "$(dirname "$f")"
        tmp=$(mktemp "$f.pin.XXXXXX")
        g cat-file blob "$blob" > "$tmp"
        [ "$mode" = 100755 ] && chmod 755 "$tmp" || chmod 644 "$tmp"
        mv -f "$tmp" "$f"
        [ "$(blob_of "$f")" = "$blob" ] || { echo "pin.sh: $dst still differs after writing" >&2; bad=1; }
    done < <(pairs)
    exit $bad
    ;;
moved)
    base=${3:?base ref}; dir=${4:?dir}; dir=${dir%/}
    missing=0; n=0
    while IFS=$'\t' read -r st path; do
        [ "$st" = D ] || continue
        n=$((n + 1))
        want=$(git rev-parse "$base:$path")
        have=$(g rev-parse -q --verify "$sha:${path#"$dir"/}" 2> /dev/null || true)
        if [ "$want" != "$have" ]; then echo "pin.sh: $path is not in $name@$sha with the same bytes" >&2; missing=$((missing + 1)); fi
    done < <(git diff --no-renames --name-status "$base...HEAD" -- "$dir/")
    echo "pin.sh: $n file(s) removed from $dir/, $((n - missing)) proven in $name@${sha:0:12}"
    [ "$missing" -eq 0 ]
    ;;
*) echo "pin.sh: unknown command $cmd" >&2; exit 2 ;;
esac
