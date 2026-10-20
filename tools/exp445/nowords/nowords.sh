#!/usr/bin/env bash
# tools/exp445/nowords/nowords.sh -- the spelling tables of the no-words experiment (evidence/NO-WORDS,
# docs/one_layer/OMEGESE_MATH.md section 5). Three spellings of one language: omegese (the words as they are),
# symbols (every fixed word and every chosen name is one non-word token) and numbers (every fixed word is its
# number in `omegese words` order, every chosen name a number 50..99, a number value is written #N).
# The map files (<arm>.map: "w <word> <spelling>" and "n <k> <spelling>") are the whole difference;
# omegese.c is not changed: an answer is turned back into Omegese words here and then read by `omegese emit`.
#
#   nowords.sh render <arm>                       the arm document (omegese.md.tmpl); omegese gives arms/omegese.md
#   nowords.sh encode <arm> <requirement> < prog  an Omegese program in the arm's spelling (worked examples): every
#                                                 fixed word mapped, every name except the task's entry and the IR
#                                                 type names u8..i64 numbered by first appearance
#   nowords.sh decode <arm> <in> <out>            an answer in the arm's spelling back to Omegese words; exit 1 with
#                                                 one line "REFUSED reader: ..." when a word is not of this spelling
#   nowords.sh msg <arm> < line                   a refusal of `omegese emit` (on the decoded program) in the arm's
#                                                 spelling: names, quoted program text, ops, types and the first word
#                                                 after "line N:"; the English prose stays (it is English in every arm)
#   nowords.sh vocab <arm>                        every word and name spelling, one per line (token checks)
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
die() { echo "nowords.sh: $*" >&2; exit 2; }
arm="${2:-}"
case "$arm" in omegese|symbols|numbers) ;; *) die "arm must be omegese, symbols or numbers" ;; esac
MAP=/dev/null; [ "$arm" = omegese ] || MAP="$HERE/$arm.map"
[ "$MAP" = /dev/null ] || [ -f "$MAP" ] || die "no map $MAP"
export LC_ALL=C
# The awk prelude: W[word]=spelling, R[spelling]=word, N[k]=name spelling, RN[spelling]=k, IRT[irname]=1
PRE='
function load(m,   a) {
  while ((getline l < m) > 0) { split(l, a, " ")
    if (a[1] == "w") { W[a[2]] = a[3]; R[a[3]] = a[2] } else if (a[1] == "n") { N[a[2]] = a[3]; RN[a[3]] = a[2]; NN++ } }
}
function sp(w) { return (ARM == "omegese" || !(w in W)) ? w : W[w] }
function nm(k, orig) { return ARM == "omegese" ? orig : N[k] }
function lit(v) { return ARM == "numbers" ? "#" v : v }
BEGIN { load(MAP); split("bool u8 u16 u32 u64 i8 i16 i32 i64", t, " "); for (i in t) IRT[t[i]] = 1
  split("fn to void end else if while for break ret trap put free true false bound add sub mul div rem and or xor shl shr eq ne lt le gt ge neg not get new bool uchar ushort uint ulong byte short int long own edit view", f, " ")
  for (i in f) FIXED[f[i]] = 1; for (i in f) ORD[f[i]] = i
  split("add sub mul div rem xor shl shr eq ne lt le gt ge neg get bool uchar ushort uint ulong byte short int long", f, " "); for (i in f) FREEW[f[i]] = 1 }
'
case "$1" in
render)
  awk -v ARM="$arm" -v MAP="$MAP" "$PRE"'
    function names(   s, k) {
      if (ARM == "numbers") return N[1] " to " N[NN]
      for (k = 1; k <= NN; k++) s = s (k > 1 ? " " : "") N[k]; return s }
    {
      line = $0
      if (match(line, /^\{\{only:[a-z]+\}\}/)) { a = substr(line, 8, RLENGTH - 9); if (a != ARM) next; line = substr(line, RLENGTH + 1) }
      out = ""
      while (match(line, /\{\{[^}]*\}\}/)) {
        tok = substr(line, RSTART + 2, RLENGTH - 4); out = out substr(line, 1, RSTART - 1); line = substr(line, RSTART + RLENGTH)
        if (tok == "arm") rep = ARM == "omegese" ? "" : "-" ARM
        else if (tok == "names") rep = names()
        else if (tok ~ /^w:/) rep = sp(substr(tok, 3))
        else if (tok ~ /^c:/) { rep = sp(substr(tok, 3)); while (length(rep) < 8) rep = rep " " }
        else if (tok ~ /^k:/) rep = lit(substr(tok, 3))
        else if (tok ~ /^n:/) { split(tok, q, ":"); rep = nm(q[2], q[3]) }
        else { print "nowords.sh: bad marker " tok > "/dev/stderr"; exit 2 }
        out = out rep
      }
      print out line
    }' "$HERE/omegese.md.tmpl" ;;
encode)
  req="${3:?requirement}"; [ -f "$req" ] || die "no requirement $req"
  entry="$(awk '$1 == "entry" { print $2 }' "$req")"
  awk -v ARM="$arm" -v MAP="$MAP" -v ENTRY="$entry" "$PRE"'
    {
      n = split($0, w, /[ \t]+/); out = ""
      for (i = 1; i <= n; i++) { x = w[i]; if (x == "") continue
        pre = ""; post = ""
        while (substr(x, 1, 1) == "(") { pre = pre "("; x = substr(x, 2) }
        while (length(x) > 0 && substr(x, length(x)) == ")") { post = post ")"; x = substr(x, 1, length(x) - 1) }
        if (x == "" || x == "=") y = x
        else if (x in FIXED) y = sp(x)
        else if (x ~ /^-?[0-9]+$/) y = lit(x)
        else if (x == ENTRY || x in IRT) y = x
        else { if (!(x in SEEN)) { SEEN[x] = ++ns; if (ARM != "omegese" && ns > NN) { print "nowords.sh: more names than the map holds" > "/dev/stderr"; exit 2 } }
               y = nm(SEEN[x], x) }
        out = out (out == "" ? "" : " ") pre y post }
      print out
    }' ;;
decode)
  in="${3:?in}" out="${4:?out}"; rm -f "$out" "$out.tmp"
  awk -v ARM="$arm" -v MAP="$MAP" -v OUT="$out.tmp" "$PRE"'
    function bad(m) { print "REFUSED reader: line " NR ": " m; err = 1; exit 1 }
    {
      n = split($0, w, /[ \t\r]+/); o = ""
      for (i = 1; i <= n; i++) { x = w[i]; if (x == "") continue
        pre = ""; post = ""
        while (substr(x, 1, 1) == "(") { pre = pre "("; x = substr(x, 2) }
        while (length(x) > 0 && substr(x, length(x)) == ")") { post = post ")"; x = substr(x, 1, length(x) - 1) }
        if (ARM == "omegese" || x == "" || x == "=") y = x
        else if (x in R) y = R[x]
        else if (x in RN) y = "_s" RN[x]
        else if (x ~ /^_s[0-9]+$/) bad("'"'"'" x "'"'"' is not a name of this form")
        else if (x in IRT) y = x
        else if (x in FIXED) bad("'"'"'" x "'"'"' is not a word of this form (the word card lists every word)")
        else if (ARM == "numbers" && x ~ /^#-?[0-9]+$/) y = substr(x, 2)
        else if (ARM == "numbers" && x ~ /^-?[0-9]+$/) bad("'"'"'" x "'"'"' is not a word (1 to 48) or a name; a number value is written #" x)
        else if (ARM == "symbols" && x ~ /^-?[0-9]+$/) y = x
        else if (x ~ /^[A-Za-z_][A-Za-z0-9_]*$/) y = x
        else bad("'"'"'" x "'"'"' is not a word or a name of this form")
        o = o (o == "" ? "" : " ") pre y post }
      print o > OUT
    }
    END { if (!err) { printf "" >> OUT; close(OUT) } }' "$in" || { rm -f "$out.tmp"; exit 1; }
  mv "$out.tmp" "$out" ;;
msg)
  awk -v ARM="$arm" -v MAP="$MAP" "$PRE"'
    function one(x, inq, first,   pre, post, k) {
      pre = ""; post = ""
      while (x != "" && index("(\x27", substr(x, 1, 1))) { pre = pre substr(x, 1, 1); x = substr(x, 2) }
      while (x != "" && index(")\x27,:;.", substr(x, length(x)))) { post = substr(x, length(x)) post; x = substr(x, 1, length(x) - 1) }
      if (x ~ /^_s[0-9]+$/) { k = substr(x, 3) + 0; if (k in N) x = N[k] }
      else if (x in FIXED && (inq || first || x in FREEW)) x = sp(x)
      else if (inq && ARM == "numbers" && x ~ /^-?[0-9]+$/) x = "#" x
      return pre x post }
    {
      if (ARM == "omegese") { print; next }
      n = split($0, w, / /); o = ""; inq = 0; first = 0
      for (i = 1; i <= n; i++) {
        x = w[i]; sq = substr(x, 1, 1) == "\x27"
        if (sq) inq = 1
        y = one(x, inq, first); first = (x ~ /^[0-9]+:$/ && i > 1 && w[i - 1] == "line")
        if (inq && x ~ /\x27[,:;.)]*$/ && (length(x) > 1 || !sq)) inq = 0
        o = o (i > 1 ? " " : "") y }
      print o
    }' ;;
vocab)
  awk '$1 == "w" || $1 == "n" { print $3 }' "$MAP" ;;
*) die "usage: nowords.sh render|encode|decode|msg|vocab <arm> ..." ;;
esac
