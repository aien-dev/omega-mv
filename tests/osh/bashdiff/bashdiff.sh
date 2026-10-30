#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Drake Stapleton (AIEN / aien-dev)
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# bashdiff.sh: run each case under REAL GNU bash 5.2 and under osh and compare (omega#344; grown from the #340 review
# runner handoffs/osh-audit/L-diff.sh). Usage: bashdiff.sh OSH CASES [name-regex]
#
# Both shells run in a fresh scratch directory under the same pinned environment: env -i, LC_ALL=C, TZ=UTC,
# PATH=/usr/bin:/bin, HOME=<scratch>/home. osh gets `--caps` with every effect allowed under / and a private journal.
# Case file: blocks headed `### NAME | MODE | EXPECT | ARGS`, then the body lines.
#   MODE   c  SH -c BODY SH ARGS     f  SH s.sh ARGS (body in s.sh)     s  SH < s.sh
#   EXPECT match      stdout, exit status, files and stderr (prefix-normalized) byte for byte
#          class      stdout, exit status and files byte for byte; stderr only empty versus non-empty
#          refuse=T   osh must refuse by name: its stderr contains T, stdout is empty, status is nonzero, and the
#                     scratch directory is unchanged (no side effect); the bash result is shown, not compared
# A body line `%%` splits it: lines before it are setup, run by bash in the scratch directory before each shell; the
# "unchanged" check of refuse= compares against the state after setup. Lines starting `#` before the first case are
# comments. stderr normalization: a leading `SH: `, `bash: `, `osh: `, `s.sh: `, the bash or osh path, with an optional `line N: `, becomes `SH: `.
# BASHDIFF_NEGCTL=1: negative control. Every expectation is deliberately corrupted (bash stdout gets one extra byte;
# a refuse= text gets a suffix no shell prints); every case must then FAIL, and the run passes only if all did.
# Prints one line per case (PASS/REFUSED/FAIL) and `BASHDIFF PASS cases=N` or `BASHDIFF FAIL ...`; exit 0 only on PASS.
set -u
[ $# -ge 2 ] || { echo "usage: bashdiff.sh OSH CASES [name-regex]" >&2; exit 2; }
OSH=$(readlink -f "$1"); CF=$(readlink -f "$2"); ONLY=${3:-.}
[ -x "$OSH" ] || { echo "bashdiff: $1 is not executable" >&2; exit 2; }
BASH_REF=/usr/bin/bash
ver=$("$BASH_REF" -c 'echo $BASH_VERSION')
case $ver in 5.2.*) ;; *) echo "bashdiff: need GNU bash 5.2.x at $BASH_REF, have $ver" >&2; exit 2 ;; esac
NEG=${BASHDIFF_NEGCTL:-0}
W=$(mktemp -d "${TMPDIR:-/tmp}/bashdiff.XXXXXX")
trap 'chmod -R u+rwx "$W" 2>/dev/null; rm -rf "$W"' EXIT
printf 'principal 1\nallow spawn /\nallow read /\nallow write /\nallow chdir /\n' > "$W/policy"
mkdir -m 700 "$W/j"
echo "bashdiff: bash $ver ($BASH_REF), env -i LC_ALL=C TZ=UTC PATH=/usr/bin:/bin, osh $OSH"
pass=0; fail=0; refused=0; total=0
norm() { sed -E "s#^(SH|bash|osh|s\\.sh|$BASH_REF|$OSH)(: line [0-9]+)?: #SH: #"; }
snap() { ( cd "$1" && find . -path ./home -prune -o -print | LC_ALL=C sort | while IFS= read -r f; do
	if [ -L "$f" ]; then printf 'L %s -> %s\n' "$f" "$(readlink "$f")"
	elif [ -d "$f" ]; then printf 'D %s\n' "$f"
	else printf 'F %s %s\n' "$f" "$(od -An -c "$f" 2>/dev/null | tr -s ' \n' ' ')"; fi; done ); }
runone() { # $1 label $2 shell-kind (bash|osh) $3 mode, rest args; body in $W/body, setup in $W/setup
	local pre=$W/$1 kind=$2 mode=$3; shift 3
	local d=$W/run
	chmod -R u+rwx "$d" 2>/dev/null; rm -rf "$d"; mkdir -p "$d/home"
	( cd "$d" && env -i PATH=/usr/bin:/bin HOME="$d/home" LC_ALL=C TZ=UTC "$BASH_REF" "$W/setup" ) >/dev/null 2>&1
	cp "$W/body" "$d/s.sh"
	snap "$d" > "$pre.before"
	local -a sh
	if [ "$kind" = bash ]; then sh=("$BASH_REF"); else sh=("$OSH" --caps "$W/policy" --journal "$W/j/effects.journal"); fi
	( cd "$d" && case $mode in
		c) exec env -i PATH=/usr/bin:/bin HOME="$d/home" LC_ALL=C TZ=UTC "${sh[@]}" -c "$(cat s.sh)" SH "$@" ;;
		f) exec env -i PATH=/usr/bin:/bin HOME="$d/home" LC_ALL=C TZ=UTC "${sh[@]}" s.sh "$@" ;;
		s) exec env -i PATH=/usr/bin:/bin HOME="$d/home" LC_ALL=C TZ=UTC "${sh[@]}" < s.sh ;;
		*) echo "bashdiff: bad mode $mode" >&2; exit 99 ;;
	esac ) > "$pre.out" 2> "$pre.rawerr" < /dev/null
	echo $? > "$pre.rc"
	sed "s#$d#DIR#g" "$pre.rawerr" | norm > "$pre.err"
	sed -i "s#$d#DIR#g" "$pre.out"
	snap "$d" > "$pre.files"
}
show() { local k; for k in "$@"; do cmp -s "$W/b.$k" "$W/o.$k" && continue
	echo "  -- $k bash:"; head -c 600 "$W/b.$k" | sed 's/^/    /'; echo "  -- $k osh:"; head -c 600 "$W/o.$k" | sed 's/^/    /'; done; }
runcase() {
	local name=$1 mode=$2 expect=$3; shift 3
	total=$((total + 1))
	runone b bash "$mode" "$@"
	runone o osh "$mode" "$@"
	local ok=1
	case $expect in
	match|class)
		[ "$NEG" = 1 ] && printf 'X' >> "$W/b.out"
		cmp -s "$W/b.out" "$W/o.out" && cmp -s "$W/b.rc" "$W/o.rc" && cmp -s "$W/b.files" "$W/o.files" || ok=0
		if [ "$expect" = match ]; then cmp -s "$W/b.err" "$W/o.err" || ok=0
		else [ -s "$W/b.err" ] && [ -s "$W/o.err" ] || { [ ! -s "$W/b.err" ] && [ ! -s "$W/o.err" ]; } || ok=0; fi
		if [ $ok = 1 ]; then pass=$((pass + 1)); echo "PASS $name"
		else fail=$((fail + 1)); echo "FAIL $name ($expect)"; show out err rc files; fi ;;
	refuse=*)
		local t=${expect#refuse=}
		[ "$NEG" = 1 ] && t="$t<negctl>"
		grep -qF -- "$t" "$W/o.rawerr" || ok=0
		[ -s "$W/o.out" ] && ok=0
		[ "$(cat "$W/o.rc")" != 0 ] || ok=0
		cmp -s "$W/o.before" "$W/o.files" || ok=0
		if [ $ok = 1 ]; then refused=$((refused + 1)); pass=$((pass + 1)); echo "REFUSED $name :: $(head -c 160 "$W/o.rawerr" | tr '\n' '|')"
		else fail=$((fail + 1)); echo "FAIL $name (refuse=$t)"
			echo "  osh rc=$(cat "$W/o.rc") stdout=$(head -c 200 "$W/o.out" | tr '\n' '|') stderr=$(head -c 300 "$W/o.rawerr" | tr '\n' '|')"
			cmp -s "$W/o.before" "$W/o.files" || { echo "  osh changed the scratch directory:"; diff "$W/o.before" "$W/o.files" | head -20 | sed 's/^/    /'; }
			echo "  bash rc=$(cat "$W/b.rc") stdout=$(head -c 200 "$W/b.out" | tr '\n' '|')"; fi ;;
	*) echo "bashdiff: case $name: bad expectation '$expect'" >&2; exit 2 ;;
	esac
}
name=; mode=; expect=; args=()
flush() {
	[ -n "$name" ] || return 0
	[[ $name =~ $ONLY ]] || return 0
	if grep -qx '%%' "$W/raw"; then sed '/^%%$/,$d' "$W/raw" > "$W/setup"; sed '1,/^%%$/d' "$W/raw" > "$W/body"
	else : > "$W/setup"; cp "$W/raw" "$W/body"; fi
	runcase "$name" "$mode" "$expect" "${args[@]}"
}
: > "$W/raw"
while IFS= read -r line || [ -n "$line" ]; do
	if [[ $line == '### '* ]]; then
		flush
		IFS='|' read -r name mode expect rest <<< "${line#\#\#\# }"
		name=${name// /}; mode=${mode// /}; expect=${expect# }; expect=${expect% }
		eval "args=(${rest:-})"; : > "$W/raw"
	elif [ -n "$name" ]; then printf '%s\n' "$line" >> "$W/raw"
	fi
done < "$CF"
flush
if [ "$NEG" = 1 ]; then
	if [ "$total" -gt 0 ] && [ "$pass" -eq 0 ]; then echo "BASHDIFF NEGCTL PASS cases=$total detected=$fail"; exit 0; fi
	echo "BASHDIFF NEGCTL FAIL cases=$total undetected=$pass"; exit 1
fi
if [ "$total" -gt 0 ] && [ "$fail" -eq 0 ]; then echo "BASHDIFF PASS cases=$total refused=$refused"; exit 0; fi
echo "BASHDIFF FAIL cases=$total pass=$pass fail=$fail refused=$refused"; exit 1
