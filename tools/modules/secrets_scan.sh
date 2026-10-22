#!/bin/bash
# secrets_scan.sh -- look for secrets in every version of every file a repository's history holds (L396).
#
# Built for the module split (ADR 0039): before a repo cut out of omega goes public, every blob reachable
# from its refs is read once (git cat-file --batch, one process) and matched against the patterns below.
# Plain bash + git + gawk; no gitleaks or trufflehog on the box, no Python.
#
# Usage: tools/modules/secrets_scan.sh <repo-dir> [allow-file]
#   allow-file: lines "<blob-id> <pattern-name>" for findings a human checked and cleared (for example the
#   public RFC 8032 test-vector secret keys); a cleared finding is printed as "cleared", not counted.
# Output: one line per finding: <verdict> <pattern> <blob> <path> <redacted match>; then a summary.
# Exit: 0 no uncleared finding; 1 at least one; 2 usage or git error.
# Matches are redacted to their first 6 characters and length, so the report itself never leaks a secret.
set -euo pipefail

repo=${1:?usage: secrets_scan.sh <repo-dir> [allow-file]}
allow=${2:-/dev/null}
git -C "$repo" rev-parse --git-dir > /dev/null 2>&1 || { echo "secrets_scan: $repo is not a git repository" >&2; exit 2; }
max=$((8 * 1024 * 1024))   # larger blobs are reported as skipped, not read

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Every blob reachable from any ref, with the first path it appeared under.
git -C "$repo" rev-list --objects --all > "$tmp/objects"
git -C "$repo" cat-file --batch-check='%(objecttype) %(objectname) %(objectsize) %(rest)' < "$tmp/objects" |
    awk -v max="$max" '$1 == "blob" { if ($3 > max) print "SKIP " $2 " " $3 " " $4 > "/dev/stderr"; else print $2 " " $4 }' \
    > "$tmp/blobs" 2> "$tmp/skipped"
cut -d' ' -f1 "$tmp/blobs" > "$tmp/ids"

git -C "$repo" cat-file --batch < "$tmp/ids" | LC_ALL=C gawk -v pathsf="$tmp/blobs" -v allowf="$allow" '
BEGIN {
    while ((getline l < pathsf) > 0) { i = index(l, " "); path[substr(l, 1, i - 1)] = substr(l, i + 1) }
    while ((getline l < allowf) > 0) { split(l, a, " "); if (a[1] != "" && a[1] !~ /^#/) cleared[a[1], a[2]] = 1 }
    n = 0
    name[++n] = "private-key";    re[n] = "-----BEGIN ([A-Z0-9]+ )*PRIVATE KEY( BLOCK)?-----"
    name[++n] = "github-token";   re[n] = "(gh[pousr]_[A-Za-z0-9]{36,}|github_pat_[A-Za-z0-9_]{50,})"
    name[++n] = "aws-key";        re[n] = "(AKIA|ASIA)[0-9A-Z]{16}"
    name[++n] = "slack-token";    re[n] = "xox[abprs]-[A-Za-z0-9-]{10,}"
    name[++n] = "api-key-sk";     re[n] = "sk-(ant-|proj-)?[A-Za-z0-9_-]{32,}"
    name[++n] = "google-key";     re[n] = "AIza[0-9A-Za-z_-]{35}"
    name[++n] = "jwt";            re[n] = "eyJ[A-Za-z0-9_-]{10,}\\.eyJ[A-Za-z0-9_-]{10,}\\.[A-Za-z0-9_-]{10,}"
    name[++n] = "tailscale-key";  re[n] = "tskey-[a-z]+-[A-Za-z0-9]{10,}"
    name[++n] = "url-password";   re[n] = "[a-z][a-z0-9+.-]*://[^/ :@\"]+:[^/ @\"]{6,}@"
    name[++n] = "assigned-secret"; re[n] = "(password|passwd|secret|token|api[_-]?key|access[_-]?key)[\"]?[ \t]*[:=][ \t]*[\"][^\" \t]{12,}[\"]"
    found = 0; clear = 0
}
# Header line of each blob: "<id> blob <size>"; then exactly <size> bytes of content follow.
remaining <= 0 && $2 == "blob" && $1 ~ /^[0-9a-f]{40}$/ { blob = $1; remaining = $3 + 1; next }
{
    remaining -= length($0) + 1
    line = $0
    for (k = 1; k <= n; k++) {
        lc = (name[k] == "assigned-secret") ? tolower(line) : line
        if (match(lc, re[k])) {
            m = substr(line, RSTART, RLENGTH)
            if (name[k] == "assigned-secret" && m ~ /(\$\{?|getenv|<[a-z_-]+>|example|xxxx|redacted|\*\*\*)/) continue
            v = ((blob, name[k]) in cleared) ? "cleared" : "FOUND"
            if (seen[blob, name[k], m]++) continue
            printf "%s %s %s %s %s...(%d chars)\n", v, name[k], blob, path[blob], substr(m, 1, 6), length(m)
            if (v == "FOUND") found++; else clear++
        }
    }
}
END {
    printf "secrets_scan: %d blobs read, %d finding(s), %d cleared\n", length(path), found, clear
    exit (found > 0)
}'
rc=$?
if [ -s "$tmp/skipped" ]; then echo "secrets_scan: blobs over $max bytes not read:"; sed 's/^/  /' "$tmp/skipped"; fi
exit $rc
