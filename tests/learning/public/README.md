# Public development oracle fixtures

Owner L776, Omega #653, action `L776-653-public-oracle-02`. These are public
development fixtures, not protected evaluation data or a qualified curriculum.
The existing benchmark owner L775 consumes their observations; this directory
does not create another evaluator, process supervisor or compiler.

The four original self-check fixtures contain sixteen explicit assertions.
`observable.om` emits the same sixteen values as 128 little-endian bytes.
`oracle.om` independently encodes the fixed mathematical expectations as literals.
Both use the pinned seed compiler, a disclosed common dependency. Literal expected
bytes in `vectors.json` are independently checked with the host byte comparator;
this does not prove general native equivalence or compiler correctness.

The saturated-add observable control compiles and exits zero but changes value
four (byte offset 32) from zero to unsigned maximum. It must fail the byte
comparison. The earlier `negative/saturating-add.om` exits 105 due to checked
conversion to `int`; its historical result is a conversion trap and is retained
without claiming wrong-output qualification. The literal-overflow source remains
an intentional compiler refusal.

## Frozen source and resource boundary

Compiler repository: Omega Bridge `0fc7d8662cb799e7c206b17c18ea4c3ec2b8fea9`.
Compiler source SHA-256: `8608fa11f6a2cf1c50db22d871eef16b00d5bc719bc1a222cc3e456984ac5ebf`.
Compiler executable SHA-256: `191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9`.
Target: static AArch64 Linux ELF, verified before execution by the existing
Omega Bridge `tools/verify-elf.sh`. Existing host commands compile, invoke and
compare; delivered behavioral logic and tests are `.om`. No new shell harness.

Continuation budget: maximum 12 compiler invocations, maximum 12 native process
invocations, 5 seconds per invocation, one process at a time, 16 MiB fixture
scratch. No GPU, model, service changes or training. Compilation/native elapsed
time and peak memory are raw process-cost observations, not quiet-machine speed
measurements. Model/tokenizer/template costs are not applicable; energy and
original abandoned-run elapsed/peak costs are unmeasured. Original failed
attempts are preserved and excluded from any complete-lifecycle cost claim.

#654 remains NOT_RUN until L775 supplies a verified profile freeze and the
shared diagnostic contract. These fixtures neither fill a corpus quota nor
cross training/adaptive/final-test partitions. No superseded runtime or
dependency is removed by this additive public fixture slice.
