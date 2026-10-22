#!/usr/bin/env bash
# tools/c2om/inventory.sh: count the C still live in our repos and group it into artifacts (ledger L418).
#
# Reads each repo of tools/c2om/repos.tsv at its GitHub default branch (git fetch, then the remote HEAD
# ref; never the local checkout's branch), every tracked .c and .h outside evidence/ and third_party/.
# Writes:
#   evidence/C2OM/inventory/files.tsv      repo file lines artifact wave needs
#   evidence/C2OM/inventory/artifacts.tsv  repo artifact files lines wave needs oracle what
#   evidence/C2OM/inventory/repos.tsv      repo ref sha files lines
# and rewrites the generated part of docs/C_TO_OMEGA.md (between the GENERATED markers).
# Usage: tools/c2om/inventory.sh [--no-fetch] [--branch-ref REF]   (REF: count omega at REF too, e.g. HEAD)
# bash, git, awk only.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$ROOT/evidence/C2OM/inventory
DOC=$ROOT/docs/C_TO_OMEGA.md
RULES=$ROOT/tools/c2om/artifacts.tsv
FETCH=1; BREF=
while [ $# -gt 0 ]; do
  case $1 in --no-fetch) FETCH=0;; --branch-ref) BREF=$2; shift;; *) echo "usage: $0 [--no-fetch] [--branch-ref REF]" >&2; exit 2;; esac
  shift
done
mkdir -p "$OUT"
TMP=$(mktemp -d /tmp/c2om-inv.XXXXXX)
trap 'rm -r "$TMP"' EXIT

# wave 1 artifacts (brief L418 step 3)
is_wave1() { case $1 in omega:tools/omegese|omega:tools/omega_emit|omega:src/compiler|omega:src/checker2|omega:tools/omegatool.c) return 0;; esac; return 1; }

# artifact of one file: tests/X/.. goes with src/X when src/X exists; top-level tools/ and src/ files
# are grouped by name (tools/<file>, src/omega_<word>*, src/<word>*)
artifact_of() { # repo path srcdirs-file
  local repo=$1 p=$2 d
  d=$(dirname "$p")
  case $p in
    tools/*/*) d=tools/$(echo "$p" | cut -d/ -f2);;
    tools/*) d=$(echo "$p" | sed -E 's/\.(c|h)$//; s/_dev$//; s/^tools\/turing_cal_.*/tools\/turing_cal/').c;;
    src/*/*) d=src/$(echo "$p" | cut -d/ -f2);;
    src/omega_*|src/omega.*) d=src/omega_$(echo "$p" | sed -E 's|src/omega_?||; s/[._].*//');;
    src/*) d=src/$(echo "$p" | sed -E 's|src/||; s/[._].*//');;
    tests/*/*) local s; s=$(echo "$p" | cut -d/ -f2)
      if grep -qx "src/$s" "$3"; then d=src/$s; else d=tests/$s; fi;;
    tests/*) d=tests/$(echo "$p" | sed -E 's|tests/(test_)?||; s/[._].*//');;
  esac
  [ "$repo" = omega ] || d=$(echo "$d" | cut -d/ -f1-2)
  echo "$d"
}

# needs of one file, from its text: feature requests in ~/handoffs/omega-feature-requests.md
needs_of() { # file-with-text
  local n=
  # comments and string literals do not count ("double-check", "/dev/null" in a message)
  awk '{ s=$0; out="";
         while (length(s)) {
           if (inc) { p=index(s,"*/"); if (!p) { s=""; break } s=substr(s,p+2); inc=0; continue }
           if (match(s, /\/\*|\/\/|"/)) {
             t=substr(s,RSTART,RLENGTH); out=out substr(s,1,RSTART-1); s=substr(s,RSTART+RLENGTH)
             if (t=="//") { s=""; break }
             if (t=="/*") { inc=1; continue }
             q=match(s, /^([^"\\]|\\.)*"/); if (q) s=substr(s,RLENGTH+1); else s=""; out=out "\"\""
           } else { out=out s; s="" } }
         print out }' "$1" > "$1.code"
  set -- "$1.code"
  grep -qE '\b(fopen|fread|fwrite|opendir|readdir|fstat|lseek|open|openat)\s*\(' "$1" && n="$n FR-1"
  grep -qE '\bstruct\b|\btypedef\b' "$1" && n="$n FR-2"
  grep -qE '#include +"' "$1" && n="$n FR-3"
  grep -qE '\b(float|double)\b' "$1" && n="$n FR-4"
  grep -qE '\b(ioctl|mmap|munmap)\s*\(|\bcu[A-Z][A-Za-z]+\s*\(|\bnvrm_' "$1" && n="$n FR-5"
  grep -qE 'pthread|_Atomic|stdatomic|__atomic_|__sync_' "$1" && n="$n FR-6"
  grep -qE '\b(asm|__asm__|volatile)\b' "$1" && n="$n FR-8"
  grep -qE '\bargv\b|\bgetenv\s*\(' "$1" && n="$n FR-13"
  grep -qE '\b(popen|system|fork|execv|execvp|execl|waitpid|posix_spawn)\s*\(' "$1" && n="$n FR-14"
  grep -qE '\b(clock_gettime|gettimeofday|time)\s*\(' "$1" && n="$n FR-15"
  grep -qE '\bunion\b|va_list|\.\.\.\s*\)|setjmp|longjmp|\bgoto\b' "$1" && n="$n HAND"
  echo "${n# }"
}

: > "$TMP/files.tsv"; : > "$TMP/repos.tsv"
while IFS=$'\t' read -r repo dir rwave; do
  case $repo in ''|\#*) continue;; esac
  dir=${dir/#\~/$HOME}
  [ -d "$dir/.git" ] || [ -f "$dir/.git" ] || { echo "missing clone $dir" >&2; exit 1; }
  [ $FETCH = 1 ] && git -C "$dir" fetch -q origin
  refs="origin/$(git -C "$dir" ls-remote --symref origin HEAD | awk '/^ref:/{sub("refs/heads/","",$2); print $2}')"
  [ "$repo" = omega ] && [ -n "$BREF" ] && refs="$refs $BREF"
  for ref in $refs; do
    tag=$repo; [ "$ref" = "${refs%% *}" ] || tag="$repo@$ref"
    sha=$(git -C "$dir" rev-parse "$ref")
    git -C "$dir" ls-tree -r "$ref" | awk -F'\t' '{split($1,a," "); print a[3] "\t" $2}' \
      | awk -F'\t' '$2 ~ /\.(c|h)$/ && $2 !~ /^evidence\// && $2 !~ /third_party\//' > "$TMP/objs"
    git -C "$dir" ls-tree -d -r --name-only "$ref" src 2>/dev/null > "$TMP/srcdirs" || : > "$TMP/srcdirs"
    nf=0; nl=0
    while IFS=$'\t' read -r obj path; do
      git -C "$dir" cat-file -p "$obj" > "$TMP/f"
      l=$(wc -l < "$TMP/f")
      a=$(artifact_of "$repo" "$path" "$TMP/srcdirs")
      if [ "$rwave" != 0 ]; then w=$rwave
      elif is_wave1 "$repo:$a"; then w=1
      else w=2; fi
      nd=$(needs_of "$TMP/f")
      printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$tag" "$path" "$l" "$a" "$w" "$nd" >> "$TMP/files.tsv"
      nf=$((nf+1)); nl=$((nl+l))
    done < "$TMP/objs"
    printf '%s\t%s\t%s\t%s\t%s\n' "$tag" "$ref" "$sha" "$nf" "$nl" >> "$TMP/repos.tsv"
    # make targets per artifact (oracle column): test targets whose name or recipe names the artifact
    { git -C "$dir" show "$ref:Makefile" 2>/dev/null || true
      for mk in $(git -C "$dir" ls-tree --name-only "$ref" mk/ 2>/dev/null | grep '\.mk$'); do git -C "$dir" show "$ref:$mk"; done
    } > "$TMP/make.$tag"
  done
done < "$ROOT/tools/c2om/repos.tsv"

# omega artifacts: wave 3 when any file needs floats, devices, threads (FR-4/5/6) or is the GPU runtime
awk -F'\t' 'BEGIN{OFS="\t"}
  {k=$1 SUBSEP $4; row[NR]=$0; key[NR]=k; if ($6 ~ /FR-4|FR-5|FR-6/) hard[k]=1;
   if ($4 ~ /^src\/(runtime|omega_gpu|omega_blackwell|omega_bw|omega_accelerator|omega_numeric)/) hard[k]=1}
  END{for (i=1;i<=NR;i++){split(row[i],f,"\t"); if (f[1] ~ /^omega/ && f[5]==2 && hard[key[i]]) f[5]=3;
      print f[1],f[2],f[3],f[4],f[5],f[6]}}' "$TMP/files.tsv" > "$OUT/files.tsv"
cp "$TMP/repos.tsv" "$OUT/repos.tsv"

# artifact table: one row per repo+artifact
awk -F'\t' 'BEGIN{OFS="\t"}
  {k=$1 "\t" $4; n[k]++; l[k]+=$3; if (!(k in w) || $5>w[k]) w[k]=$5;
   m=split($6,a," "); for (i=1;i<=m;i++) {need[k, a[i]]=1; seen[a[i]]=1}}
  END{for (k in n){s=""; for (x in seen) if ((k, x) in need) s=s " " x; print k, n[k], l[k], w[k], s}}' "$OUT/files.tsv" \
  | while IFS=$'\t' read -r repo art nf nl w nd; do
      nd=$(echo $nd | tr ' ' '\n' | sort -t- -k2 -n | tr '\n' ' '); nd=${nd% }
      base=$(basename "$art" .c); base=${base#omega_}
      mf="$TMP/make.${repo}"
      orc=$(awk -v art="$art" -v base="$base" '
          /^[A-Za-z0-9_.-]+:/ {split($0,t,":"); cur=t[1]; if (cur ~ /^test/ && index(cur, base)) hit[cur]=1}
          /^[A-Za-z0-9_.-]+:/ || /^\t/ {if (cur ~ /^test/ && index($0, art "/")) hit[cur]=1}
          END{for (h in hit) print h}' "$mf" 2>/dev/null | sort | head -4 | sed 's/^/make /' | paste -sd, - | sed 's/,/, /g')
      [ -n "$orc" ] || orc="none found"
      rule=$(awk -F'\t' -v r="$repo" -v a="$art" '$1==r && $2==a {print; exit}' "$RULES" 2>/dev/null || true)
      what=; if [ -n "$rule" ]; then what=$(echo "$rule" | cut -f3); o2=$(echo "$rule" | cut -f4); [ -z "$o2" ] || orc=$o2; fi
      if [ -z "$what" ]; then
        big=$(awk -F'\t' -v r="$repo" -v a="$art" '$1==r && $4==a && $2 ~ /\.c$/ {print $3 "\t" $2}' "$OUT/files.tsv" | sort -rn | head -1 | cut -f2)
        [ -n "$big" ] || big=$(awk -F'\t' -v r="$repo" -v a="$art" '$1==r && $4==a {print $2; exit}' "$OUT/files.tsv")
        clone=$(awk -F'\t' -v r="${repo%%@*}" '$1==r {print $2}' "$ROOT/tools/c2om/repos.tsv"); clone=${clone/#\~/$HOME}
        ref=$(awk -F'\t' -v r="$repo" '$1==r {print $2}' "$OUT/repos.tsv")
        what=$(git -C "$clone" show "$ref:$big" 2>/dev/null | grep -m1 -E '^\s*(/\*+|//+|\*)\s*[A-Za-z]' \
               | sed -E 's#^\s*(/\*+|//+|\*)\s*##; s#\*/\s*$##; s/\|/\//g' | cut -c1-100 || true)
        [ -n "$what" ] || what="(no comment)"
      fi
      printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$repo" "$art" "$nf" "$nl" "$w" "$nd" "$orc" "$what"
    done | sort -t$'\t' -k1,1 -k5,5n -k4,4nr > "$OUT/artifacts.tsv"

# the generated part of the scoreboard
{
  echo "<!-- GENERATED by tools/c2om/inventory.sh $(date -u +%FT%H:%MZ); do not edit by hand below this line -->"
  echo
  echo "## C lines remaining, per repo"
  echo
  echo "Counted on each repo's GitHub main branch (tracked .c and .h files; evidence/ snapshots and third_party/ are not counted)."
  echo
  echo "| repo | branch | C files | C lines | wave 1 | wave 2 | wave 3 | wave 4 |"
  echo "|---|---|---:|---:|---:|---:|---:|---:|"
  while IFS=$'\t' read -r repo ref sha nf nl; do
    ws=$(awk -F'\t' -v r="$repo" '$1==r {s[$5]+=$3} END{printf "%d\t%d\t%d\t%d", s[1], s[2], s[3], s[4]}' "$OUT/files.tsv")
    IFS=$'\t' read -r w1 w2 w3 w4 <<< "$ws"
    echo "| $repo | ${ref} @ ${sha:0:8} | $nf | $nl | $w1 | $w2 | $w3 | $w4 |"
  done < "$OUT/repos.tsv"
  tot=$(awk -F'\t' '$1 !~ /@/ {f+=$4; l+=$5} END{print f "\t" l}' "$OUT/repos.tsv")
  echo "| **all** | | ${tot%%$'\t'*} | ${tot##*$'\t'} | | | | |"
  echo
  echo "## Artifacts"
  echo
  echo "One row per program or library. \"Needs\" lists the language features it waits for (FR numbers in"
  echo "~/handoffs/omega-feature-requests.md; HAND = parts the translator refuses and a person rewrites)."
  echo "\"Checked by\" is the existing test that must give identical results with the Omega build."
  for w in 1 2 3 4; do
    echo
    case $w in
      1) echo "### Wave 1: the Omega toolchain (this item, L418)";;
      2) echo "### Wave 2: the rest of omega without floats, devices or threads (this item, L418)";;
      3) echo "### Wave 3: floats, devices, threads, GPU runtime, CUDApp (L419; not started here)";;
      4) echo "### Wave 4: kernel, physics, atlas, benchmarks, interplane (L420; not started here)";;
    esac
    echo
    echo "| repo | artifact | files | C lines | what it does | checked by | needs | status |"
    echo "|---|---|---:|---:|---|---|---|---|"
    awk -F'\t' -v w=$w 'NR==FNR {st[$1 "\t" $2]=$3; next}
      $1 !~ /@/ && $5==w {s=st[$1 "\t" $2]; if (s=="") s="C, not started";
        printf "| %s | %s | %s | %s | %s | %s | %s | %s |\n", $1, $2, $3, $4, $8, $7, $6, s}' \
      "$ROOT/tools/c2om/status.tsv" "$OUT/artifacts.tsv"
  done
} > "$TMP/gen.md"
if [ -f "$DOC" ] && grep -q '^<!-- GENERATED' "$DOC"; then
  sed '/^<!-- GENERATED/,$d' "$DOC" > "$TMP/head.md"
else
  echo "# C to Omega" > "$TMP/head.md"; echo >> "$TMP/head.md"
fi
cat "$TMP/head.md" "$TMP/gen.md" > "$DOC"
awk -F'\t' '{printf "%-24s %6s files %8s lines\n", $1, $4, $5}' "$OUT/repos.tsv"
