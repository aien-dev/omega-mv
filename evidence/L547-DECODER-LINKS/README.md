# L547: static linked-input refusal correction

Parent personally fixes B1 from independent review L541 at `c2c31059865eff6073ffeae819d0199b2d0a55bb`. Base is exact L538 `9d8807ddc306e204faa20ce2faeb48e215cdb5af`.

The verifier previously checked only final path components and canonicalized its root before checking it. Correct Git blobs could therefore reside behind a symbolic-link ancestor outside the export. The original ordinary-file59-unit decoder replay remains valid; this was a containment/refusal claim defect, not wrong-byte acceptance or a decoder bug.

Changes: walk every component from the caller's export root through each declared corpus directory, loader and program; reject symbolic links before following the component. Preserve the unresolved root long enough to refuse a root symlink. Also refuse multiply-linked regular source leaves, so identical-byte hard links cannot silently evade the linked-input claim. Directory ancestors outside the caller-specified export root and malicious concurrent name replacement are not covered; this is a trusted caller-owned export, not hostile-tenant isolation.

Actual new13-case RED on old source:7 failures, including linked root/ancestors and hard-linked program/loader. Exact bytes were retained in the relocated targets. On corrected source:13/13 PASS; original67/67 PASS unchanged. Refusals require the link diagnostic and exit1, not a signal/timeout or unrelated missing input. The restored ordinary input must again verify59 programs. Neither source nor target data is altered by the verifier.

Fresh full historical PR499 replay exits0: plain and ASan each59 units,59 roundtrips,0 compile refusals; original refusal/oracle/mutation gates run unchanged. Manifest is checked before and after execution. Public raw outputs retain the distinct plain/sanitizer distributions. No production compiler/decoder source, original receipt or immutable pin changed.

Reproduce from this branch root, each output directory fresh and absolute:

```sh
python3 tests/compiler/test_decoder_path_links.py "$PWD" /absolute/fresh/link-tests
python3 tests/compiler/test_decoder_corpus_closure.py "$PWD" "$PWD/tests/compiler/verify_decoder_corpus.py" /absolute/fresh/old-tests
bash tests/compiler/reproduce_pr499_decoder.sh /absolute/fresh/full-replay
```

Independent corrective-head review remains pending. This does not qualify current v6, hardware, a live model or whole-repository integration, and does not close issue450. Source pin stays `1e4cdbe91b444efe51af4e745f366e07d54de030`.
