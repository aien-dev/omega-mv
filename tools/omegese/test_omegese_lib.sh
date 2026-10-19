#!/usr/bin/env bash
# test_omegese_lib.sh <omegese> <test_omegese_lib> <scratch-dir>
# The library build of omegese.c (omegese_lib.c) against the command:
#   1. the reset list names every mutable file-scope variable of omegese.c (a new one would let one
#      query see the last one's state);
#   2. test_omegese_lib: every prefix of the dev, nested, trial2 and worked-example programs, in order
#      and shuffled, library answer == command answer (next --machine, and complete on every 8th).
set -euo pipefail
bin=$1 drv=$2 dir=$3
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
mkdir -p "$dir"

# 1. file-scope `static` variables (not const, not functions), one name per declarator
names=$(awk '
  /^static / && !/^static const/ {
    line = $0
    if (line ~ /^static [^=]*\(/) next            # a function (its parameter list comes before any =)
    sub(/\/\*.*$/, "", line)
    if (line ~ /}/) { sub(/^.*}/, "", line) } else { sub(/^static +[A-Za-z_][A-Za-z_0-9]* +/, "", line) }
    sub(/;.*$/, "", line)
    n = split(line, part, ",")
    for (i = 1; i <= n; i++) {
      v = part[i]; sub(/=.*$/, "", v); gsub(/[*\[\] ]/, " ", v); gsub(/\[[^]]*\]/, "", v)
      split(v, w, " "); if (w[1] != "") print w[1]
    }
  }' "$here/omegese.c" | sort -u)
reset_body=$(sed -n "/^static void reset(void) {/,/^}/p" "$here/omegese_lib.c")
missing=0
for v in $names; do
  if ! grep -qw -- "$v" <<<"$reset_body"; then echo "FAIL reset list: omegese.c variable '$v' is not reset in omegese_lib.c"; missing=1; fi
done
[ "$missing" = 0 ] || exit 1
echo "PASS reset list: $(echo "$names" | wc -w) file-scope variables of omegese.c, each reset"

# 2. library == command
files=$(ls "$root"/tests/one_layer/omegese/dev/*.omegese "$root"/tests/one_layer/omegese/nested/*.omegese \
           "$root"/tests/one_layer/omegese/trial2/*.omegese "$root"/tools/exp445/arms/examples/*/program.omegese 2>/dev/null)
"$drv" "$bin" "$dir" $files
