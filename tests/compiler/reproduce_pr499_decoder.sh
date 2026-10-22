#!/usr/bin/env bash
# Historical exact-source evidence replay, not a current Omega integration gate.
set -euo pipefail
repo=$(git rev-parse --show-toplevel)
out=${1:?usage: reproduce_pr499_decoder.sh ABSOLUTE_FRESH_OUTPUT}
case "$out" in /*) ;; *) echo 'absolute output required' >&2; exit 2;; esac
[[ ! -e "$out" ]] || { echo 'refusing existing output' >&2; exit 2; }
mkdir -p "$out"
pin=1e4cdbe91b444efe51af4e745f366e07d54de030
git archive "$pin" Makefile mk src/compiler src/sha256.c src/sha256.h \
 tests/compiler tools/quietlock src/osh tests/physics0/osc | tar -xf - -C "$out"
python3 "$repo/tests/compiler/verify_decoder_corpus.py" "$out" "$repo" > "$out/corpus-manifest.json"
quiet-build make -C "$out" OUT_DIR=build/closure test-osc-ir-decode > "$out/full-gate.log" 2>&1
python3 "$repo/tests/compiler/verify_decoder_corpus.py" "$out" "$repo" > "$out/corpus-manifest-post.json"
cmp "$out/corpus-manifest.json" "$out/corpus-manifest-post.json"
# A passing binary must also have executed the complete pinned input closure.
python3 - "$out" <<'PY'
import json,pathlib,re,sys
p=pathlib.Path(sys.argv[1]);m=json.loads((p/'corpus-manifest.json').read_text());expected=m['corpus_programs']
for name in ('ir_decode.out','ir_decode_asan.out'):
    text=(p/'build/closure/compiler'/name).read_text()
    counts=re.search(r'corpus: units=(\d+) round_trip_ok=(\d+) compile_refused=(\d+)',text)
    assert counts and tuple(map(int,counts.groups()))==(expected,expected,0),(name,counts.groups() if counts else None)
print(json.dumps({'corpus':expected,'plain_and_sanitizer_complete':True,'qualification':'EXACT_PR499_HOST_ONLY'}))
PY
