# Omega Stable Diagnostics Specification (v1)

Defined under milestone A of issue #655 and essay priorities 1/2.
Replaces unstructured human-only compiler errors and ad-hoc JSON with a deterministic, line-oriented plain-text diagnostic stream emitted directly by `omc.om` (the single authoritative front end).

## 1. Wire Format and Syntax

The diagnostics stream is UTF-8 plain text consisting of:
1. Exactly one version line as the first non-comment line.
2. Zero or more diagnostic record lines (one record per line).

Comments begin with `#` or `note ` and are ignored.
Blank lines (containing only ASCII whitespace) are ignored.

### 1.1 Version Line

The stream MUST begin with:
```text
omega-diag v1
```

A valid program that passes all checks produces zero diagnostic records:
```text
omega-diag v1
```
(Exits with returncode 0.)

### 1.2 Record Line Format

Each diagnostic record occupies exactly one line with whitespace-separated fields:

```text
<profile> <category> <span=start-end> <finality> <repairable> [<constraints>...]
```

Example:
```text
om1 TYPE span=42-47 FINAL REPAIRABLE code=7 expect=uint found=short
om2 SYNTAX span=108-112 INCOMPLETE REPAIRABLE code=15 expect=end
om1 NAME span=15-20 UNKNOWN REPAIRABLE code=3 name=helper
```

## 2. Field Definitions and Ordering

Fields MUST appear in this exact order:

1. **`profile`**:
   The language profile: `om1` or `om2`.
   Example: `om1`, `om2`.

2. **`category`**:
   The coarse error class, uppercase ASCII:
   - `SYNTAX`: Malformed grammar, misplaced token, unexpected delimiter (omc codes 4, 5, 6, 9, 15, 16, 22).
   - `TYPE`: Type mismatch, invalid conversion, sign error (omc codes 7, 11, 19).
   - `NAME`: Unknown identifier, duplicate name, reserved word reuse (omc codes 3, 10, 13).
   - `CAPACITY`: Resource pool exhaustion, register spill limit, memory table bounds (omc codes 12, 14, 23, 24).
   - `RANGE`: Value out of representable bounds, shift count overflow (omc codes 8, 21).
   - `FLOW`: Unreachable code, missing return, control flow violation (omc codes 17, 18).
   - `CONTRACT`: Precondition/postcondition violation, invariant, layout constraint (omc codes 27, 28, 29, 30).

3. **`source_byte_span`**:
   Formatted exactly as `span=<start>-<end>` where `<start>` and `<end>` are 0-indexed decimal byte offsets into the original source file.
   `<start>` is inclusive; `<end>` is exclusive (`start <= end`).
   Byte offsets are calculated against the raw input bytes prior to any compiler-prepended mode directives.

4. **`finality`**:
   Certainty of the defect given incomplete prefixes:
   - `FINAL`: Irreparable defect in completed statements (e.g. type conflict, invalid literal, malformed expression). No continuation can make this prefix valid without changing earlier tokens.
   - `INCOMPLETE`: Error caused by premature termination or missing closure (e.g. unclosed `if`/`while`/`fn` block, cut token at EOF). A continuation can resolve this error by supplying the missing tokens.
   - `UNKNOWN`: Undeclared identifier (e.g. call to an unparsed function or forward struct). A future declaration in the same compilation unit may resolve it.

5. **`repairable`**:
   Indicates whether code editing can resolve the issue:
   - `REPAIRABLE`: Can be repaired by modifying or appending Omega code.
   - `UNREPAIRABLE`: Hardware or architectural limit reached (compiler capacity, pool limit).

6. **`constraints`** (Optional):
   Trailing whitespace-delimited key-value pairs or token descriptions providing additional structured context:
   - `code=<N>`: Exact omc error code (e.g. `code=7`).
   - `expect=<cls>`: Expected token class or type (e.g. `expect=type`, `expect=end`).
   - `found=<tok>`: Encountered token or identifier (e.g. `found=short`).
   - `name=<id>`: Relevant identifier name.

## 3. Rejection of Malformed Input

A parser or downstream agent reading this format MUST reject malformed input with a non-zero exit status. Specifically:

1. **`MALFORMED_VERSION`**: The first non-comment non-empty line does not match `^omega-diag v1$`.
2. **`MALFORMED_FIELD_COUNT`**: A diagnostic record line contains fewer than 5 whitespace-separated fields.
3. **`MALFORMED_PROFILE`**: Field 1 is not one of `om1`, `om2`.
4. **`MALFORMED_CATEGORY`**: Field 2 is not one of the 7 specified uppercase category strings (`SYNTAX`, `TYPE`, `NAME`, `CAPACITY`, `RANGE`, `FLOW`, `CONTRACT`).
5. **`MALFORMED_SPAN`**: Field 3 does not match `^span=[0-9]+-[0-9]+$` or has `start > end`.
6. **`MALFORMED_FINALITY`**: Field 4 is not one of `FINAL`, `INCOMPLETE`, `UNKNOWN`.
7. **`MALFORMED_REPAIRABLE`**: Field 5 is not one of `REPAIRABLE`, `UNREPAIRABLE`.
8. **`MALFORMED_ENCODING`**: Line contains non-ASCII characters or CR (`\r`) line endings.
