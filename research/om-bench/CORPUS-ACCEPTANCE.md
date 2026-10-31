# Training-example acceptance rules v1 (for OmegaCorpus; omega-mv #653, #654)

A record enters the Omega training corpus only if every field below is present and checks out. A record that
fails any rule is rejected with the rule number; it is never repaired silently.

1. Contract version: `contract: om-corpus-accept/v1`. A record written under another version is not mixed in.
2. Source identity: repo, commit sha (40 hex), path, sha256 of the exact .om bytes, licence, and repo
   visibility (public / private). Private-repo records go to a local-only split and appear publicly only as
   `private-01..NN`. UPSTREAM-FORK repos are skipped. AGPL sources are eval-only, never training.
3. Compiler identity: omega-mv commit and `seed/out/omc` sha256 the record was checked with.
4. Producer identity: `human`, `model:<name>@<weights sha256>` with decoding settings, or `tool:<name>@<commit>`.
   Model-written code is labelled as such and is never presented as human-written.
5. Observed result, not a claim: compile exit code and message as observed; for runnable records, the inputs
   used, the observed stdout sha256 and exit code, and the date and host. "Compiles" alone makes a record a
   syntax example only (tag `evidence: compile-only`).
6. Checker identity: the program that decided the result (for example gate.om at a given commit and sha256,
   or a named test file at a commit). The checker must not be the producer: a lane does not certify its own
   output.
7. Evidence scope: one of `compile-only`, `ran-on-listed-inputs`, `exhaustive-over-domain`,
   `solver-checked`. An optimizer rewrite is "test-equivalent" unless exhaustive or solver-checked.
8. Negative control: each batch includes at least one planted wrong record (wrong output, or a program the
   compiler should refuse) and the batch log shows the checker rejecting it with its diagnostic text. A batch
   whose control passes is rejected entirely.
9. Split hygiene: no record may contain, paraphrase or solve a task from the om-bench sealed splits. Check
   every record against `handoffs/OmegaEval/heldout-sha256.txt` (exact) and with `leakcheck.py` against the
   statements (near-duplicate); a hit is rejected and reported to OmegaEval.
10. Denominator: the batch log states records offered, accepted and rejected by rule number; offered =
    accepted + rejected.
