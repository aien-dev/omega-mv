#!/bin/bash
# graph.sh -- the module dependency graph of this repository (L396, ADR 0039 one thing per repo).
#
# Every tracked file is put in one module group by tools/modules/groups.tsv (first match wins).
# Edges come from what the files themselves say, read with git grep over the tracked tree:
#   include  a C/C++ #include "x" resolved like the build does (same directory, then -Isrc, then
#            a unique path suffix); headers found only in the physics checkout count as ext:physics
#   build    a path named in Makefile or mk/*.mk (sources linked, gates run, dirs read)
#   code     a path named inside a .c/.h file (a gate opening evidence/, a tool path)
#   script   a path named inside a shell script
#   doc      a path named inside Markdown, text or JSON outside evidence/
# evidence/ files are receipts, so their own contents never create edges (they only receive them).
#
# Usage: tools/modules/graph.sh [out-dir]          (default build/modules; repo = git toplevel)
# Environment: PHYSICS_DIR physics checkout used to recognise physics headers (default ../physics).
# Output files in out-dir:
#   files.tsv   path, group
#   refs.tsv    from-path, to-path, kind           (cross-group references only)
#   edges.tsv   from-group, to-group, kind, count, one example "from-path -> to-path"
#   order.txt   every code group, leaf-first (cut order); loop: lines are groups tied in a cycle
#   sccs.txt    the loop: lines only (groups that depend on each other through include/build edges)
# The summary (files per group, edges per pair, loops) goes to stdout.
set -euo pipefail

root=$(git rev-parse --show-toplevel)
cd "$root"
out=${1:-build/modules}
mkdir -p "$out"
rules=tools/modules/groups.tsv
physics=${PHYSICS_DIR:-../physics}

git ls-files > "$out/all.txt"

# 1. Group per file.
awk -F'\t' -v rules="$rules" '
BEGIN {
    n = 0
    while ((getline line < rules) > 0) {
        if (line ~ /^#/ || line ~ /^[ \t]*$/) continue
        if (line ~ /^@/) {
            name = substr(line, 2)
            g[++n] = name; re[n] = "^(src|tests|tools|mk)/" name "([/_.-]|$)"
            continue
        }
        split(line, f, "\t"); g[++n] = f[1]; re[n] = f[2]
    }
}
{ for (i = 1; i <= n; i++) if ($0 ~ re[i]) { print $0 "\t" g[i]; next } print $0 "\tmeta" }
' "$out/all.txt" > "$out/files.tsv"

# Physics headers by basename and by path (ext:physics when an include resolves only there).
: > "$out/physics.txt"
if git -C "$physics" rev-parse --git-dir > /dev/null 2>&1; then
    git -C "$physics" ls-files -- '*.h' > "$out/physics.txt"
fi

# 2. Raw references: #include lines and path-shaped strings in every tracked non-evidence file.
git grep -I -n --no-color -E '^[[:space:]]*#[[:space:]]*include[[:space:]]*"' \
    -- '*.c' '*.h' '*.cc' '*.cpp' '*.cu' '*.S' '*.inc' ':!evidence' 2> /dev/null |
    sed -E 's/^([^:]+):[0-9]+:[[:space:]]*#[[:space:]]*include[[:space:]]*"([^"]+)".*/\1\t\2\tinclude/' \
    > "$out/raw.tsv" || true
pathre='(src|tests|tools|bench|mk|evidence|research|calibration|spec|docs|doctrine)/[A-Za-z0-9_.+/-]+'
# Comment lines in Makefile and mk/*.mk name paths without building them, so they are skipped there
# (a build edge must be a real one); everywhere else every mention counts at its own kind.
git grep -I -n --no-color -E "$pathre" -- ':!evidence' 2> /dev/null |
    awk -v re="$pathre" '{
        f = $0; sub(/:.*/, "", f); line = $0; sub(/^[^:]+:[0-9]+:/, "", line)
        if ((f == "Makefile" || f ~ /^mk\//) && line ~ /^[ \t]*#/) next
        while (match(line, re)) { print f "\t" substr(line, RSTART, RLENGTH) "\ttext"; line = substr(line, RSTART + RLENGTH) }
    }' >> "$out/raw.tsv" || true

# 3. Resolve references to tracked files (or directories, or physics) and keep cross-group ones.
awk -F'\t' -v out="$out" '
function dirname(p) { if (p !~ /\//) return ""; sub(/\/[^\/]*$/, "", p); return p }
function base(p) { sub(/.*\//, "", p); return p }
function kind_of(f, k) {
    if (k == "include") return "include"
    if (f == "Makefile" || f ~ /^mk\//) return "build"
    if (f ~ /\.(c|h|cc|cpp|cu|S|inc|rs)$/) return "code"
    if (f ~ /\.sh$/) return "script"
    return "doc"
}
FILENAME == out "/files.tsv" {
    grp[$1] = $2; nb[base($1)]++; bypath[base($1)] = $1
    # directory -> group (the group holding most files below it)
    d = dirname($1)
    while (d != "") { dc[d SUBSEP $2]++; dirs[d] = 1; d = dirname(d) }
    next
}
FILENAME == out "/physics.txt" { phys[$1] = 1; pb[base($1)] = 1; next }
FNR == 1 && !dirdone {
    for (k in dc) { split(k, a, SUBSEP); if (dc[k] > best[a[1]]) { best[a[1]] = dc[k]; dgrp[a[1]] = a[2] } }
    dirdone = 1
}
{
    from = $1; ref = $2; k = kind_of(from, $3)
    if (!(from in grp)) next
    to = ""
    if ($3 == "include") {
        d = dirname(from)
        if ((d "/" ref) in grp) to = d "/" ref
        else if (("src/" ref) in grp) to = "src/" ref
        else if (("tests/" ref) in grp) to = "tests/" ref
        else if (ref in grp) to = ref
        else if (nb[base(ref)] == 1 && bypath[base(ref)] ~ ("(^|/)" ref "$")) to = bypath[base(ref)]
        else if (ref in phys || base(ref) in pb || ref ~ /^(m16|nvrm)\//) { tg = "ext:physics"; to = "physics:" ref }
        else next
        if (to !~ /^physics:/) { sub(/^\.\//, "", to); tg = grp[to] }
    } else {
        sub(/[.\/]+$/, "", ref)
        if (ref in grp) { to = ref; tg = grp[ref] }
        else if (ref in dirs) { to = ref "/"; tg = dgrp[ref] }
        else next
    }
    fg = grp[from]
    if (tg == fg || to == from) next
    print from "\t" to "\t" k > (out "/refs.tsv")
    key = fg "\t" tg "\t" k
    cnt[key]++
    if (!(key in ex)) ex[key] = from " -> " to
}
END { for (key in cnt) print key "\t" cnt[key] "\t" ex[key] > (out "/edges.unsorted") }
' "$out/files.tsv" "$out/physics.txt" "$out/raw.tsv"
sort -t$'\t' -k1,1 -k2,2 -k3,3 "$out/edges.unsorted" > "$out/edges.tsv"
sort -u "$out/refs.tsv" -o "$out/refs.tsv"
rm -f "$out/edges.unsorted"

# 4. Cut order and loops between code modules (Tarjan) over hard edges: include, build.
glue='^(build|tests|tools|bench|docs|meta|evidence|research|calibration|ext:physics)$'
awk -F'\t' -v glue="$glue" '
$3 ~ /^(include|build)$/ && $1 !~ glue && $2 !~ glue {
    if (!(($1, $2) in seen)) { seen[$1, $2] = 1; adj[$1] = adj[$1] " " $2 }
    node[$1] = 1; node[$2] = 1
}
function strong(v,   i, n, w, ws, comp) {
    idx[v] = low[v] = ++counter; stack[++sp] = v; on[v] = 1
    n = split(adj[v], ws, " ")
    for (i = 1; i <= n; i++) {
        w = ws[i]
        if (!(w in idx)) { strong(w); if (low[w] < low[v]) low[v] = low[w] }
        else if (on[w] && idx[w] < low[v]) low[v] = idx[w]
    }
    if (low[v] == idx[v]) {
        comp = ""; size = 0
        do { w = stack[sp--]; on[w] = 0; comp = comp " " w; size++ } while (w != v)
        print (size > 1 ? "loop:" : "node:") comp
    }
}
END {
    # Visit in name order so the output is stable; Tarjan emits a group only after every
    # group it depends on, so the lines come out leaf-first: the order to cut them.
    nn = 0; for (v in node) names[++nn] = v
    for (i = 2; i <= nn; i++) { t = names[i]; for (j = i - 1; j >= 1 && names[j] > t; j--) names[j + 1] = names[j]; names[j + 1] = t }
    for (i = 1; i <= nn; i++) if (!(names[i] in idx)) strong(names[i])
}
' "$out/edges.tsv" > "$out/order.txt"
grep '^loop:' "$out/order.txt" > "$out/sccs.txt" || true

# 5. Summary.
echo "== files per group"
cut -f2 "$out/files.tsv" | sort | uniq -c | sort -rn | awk '{ printf "  %-14s %6d\n", $2, $1 }'
echo "== edges between code modules (from -> to: include/build/code/script/doc)"
awk -F'\t' -v glue="$glue" '$1 !~ glue && ($2 !~ glue || $2 == "ext:physics" || $2 == "evidence") {
    k = $1 " -> " $2; c[k, $3] += $4; keys[k] = 1 }
END { for (k in keys) printf "  %-30s %5d %5d %5d %5d %5d\n", k, c[k, "include"], c[k, "build"], c[k, "code"], c[k, "script"], c[k, "doc"] }' \
    "$out/edges.tsv" | sort
echo "== cut order, leaf-first (include/build edges)"
sed 's/^/  /' "$out/order.txt"
echo "== loops (must be cut by an interface first)"
if [ -s "$out/sccs.txt" ]; then sed 's/^/  /' "$out/sccs.txt"; else echo "  none"; fi
