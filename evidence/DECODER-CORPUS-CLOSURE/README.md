# L538: complete, fail-closed PR499 decoder evidence replay

Parent-authored follow-on to independent review L533, on top of its exact corrected-corpus evidence. This changes host evidence tooling only; decoder/compiler production source and original receipts remain unchanged.

## Gap and correction

L519's earlier exact-source export omitted `src/osh` and `tests/physics0/osc`. The decoder test silently skips missing directories, so its55-unit PASS was not complete corpus coverage. L533 restored the four omitted programs and correctly ran59 units. That reviewer result is retained.

The new preflight goes further than testing existence of those four known files:

- Reads the four `load_dir` declarations from the exact pinned PR499 test source, not the mutable exported loader.
- Enumerates each immediate `.osc` source from that immutable Git tree, matching the loader's actual directory semantics.
- Requires exact program membership, ordinary files and exact Git blob content for all59 programs and the loader itself. Missing, extra, linked, changed or substituted inputs refuse before execution.
- Produces a per-file byte count, Git blob and SHA256 manifest. No source export is modified by verification.
- The reproduction command exports the complete build and corpus inputs, checks their manifest before and after the full gate, and requires both plain and sanitizer outputs to execute exactly the manifest's59 programs with59 successful round trips and0 compile refusals.

Source pin is fixed: `1e4cdbe91b444efe51af4e745f366e07d54de030`. This is historical PR499 host evidence, not current-main/v6 qualification or source attestation for arbitrary receipts. The caller owns the private export; this is not protection against a malicious concurrent writer changing and restoring bytes during execution.

## Actual red and green

Before new verifier implementation, the existing four-file existence checker failed64 of67 new boundary checks: it accepted missing compiler-program/slice inputs, changed source bytes, an extra program, a linked program and a changed loader. The three legitimate results were full input acceptance and rejection of missing files in the two formerly omitted directories. This is a regression gap in the preflight's coverage, not evidence of a decoder defect.

New verifier:67/67 checks PASS. Every one of59 pinned programs has its bytes independently changed in the test, with an explicit missing-file representative for each directory, plus extra-program, linked-program and changed-loader controls. The source directories are local synthetic test copies; no user corpus is deleted.

Full fresh pinned replay executes the original plain, sanitizer, refusal and mutation gate. Actual plain result:59 corpus units/59 round trips/0 compile refusals;22/22 refusal cases;5 oracle controls;10 decoder mutants;10,000 generated valid objects and202,675 malformed cases. Raw sanitizer distribution is retained without replacing it with the plain count. Both complete-corpus counts are checked by the wrapper. No original gate, warning, case or budget is weakened.

## Commands

From this branch's repository root on the Spark:

```sh
python3 tests/compiler/test_decoder_corpus_closure.py "$PWD" \
  "$PWD/tests/compiler/verify_decoder_corpus.py" /absolute/fresh/test-output
bash tests/compiler/reproduce_pr499_decoder.sh /absolute/fresh/full-output
```

The test and reproduction refuse existing output directories. Git is used only to read local pinned source objects; no network or service change. The host-only Python/sh tools do not become an Omega runtime/frontend. Full existing hardware/current-model/repository integration gates remain NOT_RUN. Independent review of this parent-authored harness remains pending; L533's actual independent complete-corpus result is a separate already-published remedy.

Desk should carry the corrected complete-corpus evidence and use the new preflight for future exact-source replays. The original55-unit logs remain immutable historical partial evidence; do not label them complete or close #450 solely from this host replay.
