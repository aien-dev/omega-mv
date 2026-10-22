# L531 bounded review corrections

Base85d8343fa04647b198e3ddda00ad8e448837f9ca. Reviewer990594198c1ecbb0b8c460e39b45caefc3ea28de. Only C1/C2/C3 converter and A1 host test coverage are assigned. Production allocator remains unchanged; no allocator policy, runtime or exploration change.

## C1/C2/C3 converter milestone

The converter captures the source pathname once into a private read-only snapshot. One jq slurp requires exactly one object, validates it and produces both rows in one invocation. SHA-256 is over the raw captured bytes, including whitespace; it is not a canonicalized JSON digest. Replacing the original pathname at the first jq invocation cannot change validation, fields or hash. Invalid-first/valid-last, reverse order, multi-valid, empty, array, null and malformed sources refuse without touching existing output.

Machine metadata must be a string of 1..64 ASCII characters matching `[A-Za-z0-9][A-Za-z0-9_.-]*`. The initial alphanumeric character prevents spreadsheet formula prefixes; embedded whitespace, quotes, commas and non-scalars refuse. Genuine aarch64/x86_64 remain byte-for-byte unchanged in CSV cells. Machine values outside this intentionally restricted uname-compatible subset refuse.

Counts and exported timings must use unsigned decimal integer JSON tokens with value <=9007199254740991 (2^53-1, binary64 exact integer maximum), also within producer uint64 representation. Fractional and exponent spellings refuse, including exponents whose mathematical result is an integer. Counts must additionally meet existing equality/positivity requirements; cognition time may be zero, end-to-end time must be positive. Literal preservation is checked before parsing receipts, so a jq build that rounds number literals refuses rather than silently accepting rounded fractions or underflow. Tested jq1.7. Producer uses `%llu` decimal nanoseconds (tests/runtime/rx_plan_reuse.c). The safe bound deliberately refuses larger producer-representable uint64 values to protect downstream binary64 consumers.

Output is a file pathname: directories, symbolic links (including dangling links), FIFOs/devices and trailing slashes refuse. Temporary output is in the destination directory and `mv -T` publishes atomically to the exact path only after validation/export completes; refusal preserves any previous output. This is a local trusted-directory utility, not protection from an adversary controlling its process UID or racing directory ancestors. Existing six tests now use a regular file target, so they still exercise receipt refusal rather than the new special-output refusal.

Commands (from repository root) and raw results in csv/:

- `quiet-build env BLOCK3_RED_ONLY=1 bash tests/m1/block3_csv_adversarial_test.sh build/L531/csv-red-actual`: exit1,133 assertions/43 failures on original converter. Device probe deliberately omitted here because original would replace /dev/null. Private directory/FIFO/link probes reproduced unsafe behavior.
- `quiet-build bash tests/m1/block3_csv_adversarial_test.sh build/L531/csv-rounding-red`: exit1,136 assertions/2 failures on first fix. Additional large-fraction and underflow literals exposed binary64 rounding. Fixed with producer token grammar plus literal-preservation guard.
- `quiet-build bash tests/m1/block3_csv_adversarial_test.sh 'build/L531/csv published'`: exit0,136 assertions/0 failures, after final converter changes. Includes /dev/null type/inode/size preservation, original-path mutation, genuine receipt byte comparison and maximum-safe-integer/zero boundaries.
- `quiet-build tests/m1/block3_csv_test.sh`: exit0, all original six refusals and original success assertions.

Two early setup attempts did not reach the converter (wrong fixture basename and a failed sed command); retained in build/L531/csv-red.log and csv-red-corrected.log. The recorded actual RED uses the correct stored fixture and reaches all assigned symptoms.

Trust boundary: candidate/run SHA equality, clean-tree flag and binary SHA are self-reported receipt fields. This converter does not independently authenticate source, signer, binary execution, machine or measurement. An attacker can forge equal valid-looking pins. Prior independent L520 rebuild corroborates its specific historical receipt only. Successful export never changes its pre-registered G1 FAIL or strict plan FAIL. Retained memory and lookup time remain UNKNOWN; wall energy UNMEASURED; calibrated timing and current35B/hardware qualification NOT_RUN. Old sealed evidence is preserved.
