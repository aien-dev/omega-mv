#!/usr/bin/env bash
# tools/exp445/nowords/test_nowords.sh -- checks of the no-words spellings (evidence/NO-WORDS). Needs build/omegese and
# build/omega_emit (make omegese omega-emit). NOWORDS_TOKCOUNT + NOWORDS_TOKENIZER (tokcount and the 27B
# tokenizer.json) add the one-token gate of the symbols spelling.
#   1 the omegese render of the template is arms/omegese.md byte for byte (the arms differ only in marked words)
#   2 both maps hold exactly the words of `omegese words`, in that order, each spelling distinct, no spelling
#     equal to a fixed word, an IR type name or a plain identifier
#   3 every worked example, in every spelling, through the real reader (reader.sh reader): check OK
#   4 refusals, each with a control that is accepted: an English word in a no-words spelling, an unmarked number
#     in the numbers spelling, an unknown symbol, a reserved internal name; a refusal message names the arm's
#     spelling, not the internal name
#   5 (with a tokenizer) every symbol spelling, word or name, is one token with its leading space
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
A="$ROOT/tools/exp445/arms" NW="$HERE/nowords.sh"
X="$ROOT/build/omega_emit/x445" EMIT="$ROOT/build/omega_emit/omega_emit" G="$ROOT/build/omegese/omegese"
for f in "$X" "$EMIT" "$G"; do [ -x "$f" ] || { echo "test_nowords: build first (make omegese omega-emit): $f"; exit 2; }; done
. "$ROOT/tools/exp445/reader.sh"
T="$(mktemp -d "${TMPDIR:-/tmp}/nowords-test.XXXXXX")"; trap 'rm -rf "$T"' EXIT
fails=0; pass() { echo "PASS $*"; }; fail() { echo "FAIL $*"; fails=$((fails + 1)); }

# 1
if "$NW" render omegese | cmp -s - "$A/omegese.md"; then pass "render omegese = arms/omegese.md"; else fail "render omegese differs from arms/omegese.md"; fi

# 2
"$G" words > "$T/words"
for arm in symbols numbers; do
  m="$HERE/$arm.map"
  if awk '$1 == "w" { print $2 }' "$m" | cmp -s - "$T/words"; then pass "$arm map: the words of omegese words, in order"; else fail "$arm map: words differ from omegese words"; fi
  d="$(awk '$1 == "w" || $1 == "n" { print $3 }' "$m" | LC_ALL=C sort | uniq -d | head -n 1)"
  [ -z "$d" ] && pass "$arm map: every spelling distinct" || fail "$arm map: spelling $d used twice"
  c="$(awk '$1 == "w" || $1 == "n" { print $3 }' "$m" | LC_ALL=C grep -E '^[A-Za-z_][A-Za-z0-9_]*$|^#|^=$|[()]' | head -n 1 || true)"
  [ -z "$c" ] && pass "$arm map: no spelling is an identifier, '#', '=' or a bracket" || fail "$arm map: spelling '$c' collides with names or punctuation"
done

# 3
for arm in omegese symbols numbers; do
  n=0 ok=0
  while IFS= read -r e; do
    [ -n "$e" ] || continue; n=$((n + 1)); E="$A/examples/$e"
    if [ "$arm" = omegese ]; then cp "$E/program.omegese" "$T/prog"; else "$NW" encode "$arm" "$E/requirement" < "$E/program.omegese" > "$T/prog"; fi
    printf 'Here it is.\n\n```\n%s\n```\n' "$(cat "$T/prog")" > "$T/content"
    reader "$X" "$EMIT" "$arm" "$T/content" "$T/r" "$E/requirement"
    if [ "$(cat "$T/r.check")" = OK ]; then ok=$((ok + 1)); else echo "  $arm $e: $(cat "$T/r.check")"; fi
  done < <(cat "$A/examples/many.list" "$A/examples/one.list")
  [ "$ok" = "$n" ] && pass "$arm: $ok/$n worked examples accepted by the reader" || fail "$arm: $ok/$n worked examples accepted"
done

# 4
E="$A/examples/power_mod"
"$NW" encode symbols "$E/requirement" < "$E/program.omegese" > "$T/sym"
"$NW" encode numbers "$E/requirement" < "$E/program.omegese" > "$T/num"
neg() { # neg <name> <arm> <file> <expected refusal substring>
  printf '```\n%s\n```\n' "$(cat "$3")" > "$T/content"; reader "$X" "$EMIT" "$2" "$T/content" "$T/n" "$E/requirement"
  if grep -qF -- "$4" "$T/n.check"; then pass "refused: $1"; else fail "$1: expected '$4', got: $(cat "$T/n.check")"; fi
}
ctl() { printf '```\n%s\n```\n' "$(cat "$3")" > "$T/content"; reader "$X" "$EMIT" "$2" "$T/content" "$T/n" "$E/requirement"
  [ "$(cat "$T/n.check")" = OK ] && pass "control accepted: $1" || fail "control $1: $(cat "$T/n.check")"; }
ctl "symbols power_mod" symbols "$T/sym"; ctl "numbers power_mod" numbers "$T/num"
ret_s="$(awk '$2 == "ret" { print $3 }' "$HERE/symbols.map")"
sed "0,/^$ret_s /s//ret /" "$T/sym" > "$T/sym.en"; neg "an English word in the symbols spelling" symbols "$T/sym.en" "'ret' is not a word of this form"
sed '0,/#0/s//0/' "$T/num" > "$T/num.bare"; neg "an unmarked number in the numbers spelling" numbers "$T/num.bare" "'0' is not a word (1 to 48) or a name"
sed '0,/ = /s// ⨁ /' "$T/sym" > "$T/sym.unk"; neg "an unknown symbol" symbols "$T/sym.unk" "'⨁' is not a word or a name of this form"
sed '0,/^\(.*\) = /s//_s3 = /' "$T/num" > "$T/num.res"; neg "a reserved internal name" numbers "$T/num.res" "'_s3' is not a name of this form"
# a written-but-unread name: the message must name the arm's spelling (99), not the internal name _s2
awk 'NR == 2 { print "99 = 51" } { print }' "$T/num" > "$T/num.unread"; neg "an unread name, in the arm's spelling" numbers "$T/num.unread" "'99' is written but never read"
grep -q "_s" "$T/n.check" && fail "the refusal leaks an internal name: $(cat "$T/n.check")" || pass "no internal name in the refusal"

# 5
if [ -n "${NOWORDS_TOKCOUNT:-}" ] && [ -n "${NOWORDS_TOKENIZER:-}" ]; then
  multi="$("$NOWORDS_TOKCOUNT" "$NOWORDS_TOKENIZER" words $("$NW" vocab symbols) | awk '$1 != 1 { print $3 }')"
  [ -z "$multi" ] && pass "symbols: every word and name spelling is one token with its leading space" || fail "symbols: more than one token: $multi"
else echo "SKIP one-token gate (set NOWORDS_TOKCOUNT and NOWORDS_TOKENIZER)"; fi

[ "$fails" = 0 ] && { echo "test_nowords: ALL PASS"; exit 0; } || { echo "test_nowords: $fails FAILED"; exit 1; }
