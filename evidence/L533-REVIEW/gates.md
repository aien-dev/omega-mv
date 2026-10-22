# L533 publication gates

All host checks and corrected complete decoder gate have completed with the
exits in README.md. Plain and sanitizer full decoder legs each cover59 corpus
units and202,675 malformed cases (valid generation10,000 and2,000 respectively).
No original evidence was rewritten; the public review is an additive correction.

Pinned crumb d0f0b95d344b4c6628c3d868346932fd247ce168: initial verify exited1
because root metadata was stale after new evidence. Normal compile exited0;
verify exited0,157 current. These logs are the pre-final-index check. Final
staged-index/post-commit verify and evidence-immutable results are recorded in
the external L533 handoff and own build/L533; logs added here remain immutable.
No gate weakening, retries of unchanged failing tests, hardware reservation or
independent-review substitution occurred. Initial reviewer wrapper compile
failure is retained separately and never counted as a killed mutant.

Initial staged diff-whitespace check exited2 for spaces in raw compiler
caret diagnostics and a worktree-list trailing blank. Public diagnostic text
has those trailing spaces normalized; exact original bytes are preserved in
visibility-initial-build-fail.raw.b64. Worktree separator blank trimmed. No
source/assertion/result changed. Final staged whitespace check follows normally.
