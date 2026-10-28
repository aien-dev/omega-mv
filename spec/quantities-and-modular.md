# SPECIFICATION: Distinct Semantic Domains for Bounded Quantities and Modular Words

```text
Document ID:     SPEC-OMEGA-QUANTITY-DOMAINS
Classification:  Sovereign Semantic Specification
Target Substrate: Typed AST, Seeded Compiler (seed/omc), and Semantic Value Graphs
Status:          PROPOSED (Refs #756, Essay Priority 3)
```

---

## 1. Problem and Purpose

Omega distinguishes ownership and certain bounds checks, but its integer types (`ulong`, `uint`, `ushort`, `uchar`) implement modular arithmetic modulo $2^w$ ($w \in \{64, 32, 16, 8\}$).

Modular arithmetic with wrapping is necessary and intentional for cryptography:
- Hash functions (SHA-256, SHA-512) require wrapping addition modulo $2^{32}$ and $2^{64}$.
- Stream ciphers, bit permutations, and field arithmetic rely on exact modular reduction without branch or trap penalties.

However, treating resource counters, buffer lengths, object offsets, memory quotas, and loop bounds as wrapping modular words is hazardous:
- An unchecked calculation such as `offset + count` can wrap modulo $2^{64}$, bypassing a naive boundary check `offset + count <= length`.
- Integer overflow in length calculation causes undersized buffer allocations followed by out-of-bounds writes.
- Implicit mixing of bitwise masks or wrapping counters into resource sizes allows logic errors to compile silently.

This specification defines the separation of Omega's numeric types into distinct semantic domains, specifies the conversion and arithmetic rules, establishes the slice validation invariant, and guarantees the preservation of modular crypto.

---

## 2. Distinct Semantic Domains

Omega recognizes three distinct numeric and resource domains:

### 2.1 Modular Words (`ulong`, `uint`, `ushort`, `uchar`)
- **Mathematical Domain**: $\mathbb{Z} / 2^w \mathbb{Z}$ for fixed bit width $w \in \{64, 32, 16, 8\}$.
- **Semantics**: Arithmetic operations (`add`, `sub`, `mul`) wrap algebraically modulo $2^w$.
- **Allowed Operations**: Arithmetic, bitwise operations (`and`, `or`, `xor`), shifts (`shl`, `shr`), and rotates (`rotr`, `rotl`).
- **Domain Role**: Cryptographic state, bit masks, hash digests, machine words, and raw serialized data.

### 2.2 Bounded Quantities (`qty`)
- **Mathematical Domain**: Non-negative integers $\mathbb{N} \cap [0, 2^{64}-1]$ (or bounded by an explicit capacity $C \le 2^{64}-1$).
- **Semantics**: Arithmetic operations are checked at runtime. Any operation whose exact mathematical result falls outside $[0, 2^{64}-1]$ (or the declared capacity) traps fail-closed (`trap 1` for overflow/underflow) or returns a declared failure before any effect or state change occurs.
- **Allowed Operations**: Checked addition (`add`), checked subtraction (`sub`), checked multiplication (`mul`), division (`div`), remainder (`rem`), and relational comparisons (`eq`, `ne`, `lt`, `le`, `gt`, `ge`).
- **Disallowed Operations**: Bitwise operations (`and`, `or`, `xor`), bit shifts (`shl`, `shr`), and bitwise negation (`not`) are strictly forbidden on quantities. A quantity represents an arithmetic amount, not a bit vector. Attempting bitwise operations on a quantity is a compile-time type refusal (error 7).
- **Domain Role**: Buffer lengths, element counts, iteration limits, allocation sizes, memory quotas, and generation numbers.

### 2.3 Object-Indexed Offsets and Slices (`qoff`, `qslice`)
- **Mathematical Domain**: Offsets $o$ and lengths/counts $k$ defined relative to an explicit target object extent $L \in \mathbb{N}$.
- **Semantics**: An offset or slice cannot be evaluated in isolation without referencing the underlying object capacity.
- **Validation Invariant**:
  $$0 \le o \le L \quad \land \quad k \le L - o$$
- **Domain Role**: Subviews, slices, memory-mapped windows, and record fields.

---

## 3. Conversion Discipline

### 3.1 Prohibition of Implicit Conversions
There are **no implicit conversions** between modular words and bounded quantities:
1. An operation combining a modular word and a quantity (e.g. `add ulong_val qty_val`) is rejected at compile time (error 7).
2. Assigning a modular word to a quantity variable, or a quantity to a modular word variable, is rejected at compile time (error 7).
3. Passing a modular word to a parameter declared as `qty`, or a quantity to a parameter declared as `ulong`, is rejected at compile time (error 7).
4. Returning a modular word from a function declaring a `qty` return type (or vice versa) is rejected at compile time (error 7).

### 3.2 Explicit Checked Ingress and Egress
1. **Modular to Quantity (`qty(w)`)**:
   Explicit conversion from an unsigned modular word `w` to `qty` is valid across the full unsigned 64-bit range.
   Converting a signed integer to `qty` requires checking that the sign bit is clear; a negative value traps with `trap 5` (out of range) or returns a declared error status before any assignment.
2. **Quantity to Modular (`ulong(q)`)**:
   Explicit conversion from `qty` to `ulong` is permitted to mark intentional transition into a modular computation domain (e.g., feeding a message length into a SHA padding block).
3. **Narrowing Conversions**:
   Converting `qty` to a narrower scalar (such as `uint` or `uchar`) requires checking that no higher-order bits are set. If `q >= 2^w`, the conversion traps (`trap 5`) or returns a declared refusal before effects.

---

## 4. Checked Arithmetic and Slice Invariants

### 4.1 Addition and Multiplication
- **Quantity Addition (`add a b`)**:
  Computes $a + b$ in $\mathbb{Z}$. If $a + b \ge 2^{64}$, the processor carry flag is set, triggering an immediate branch to `trap 1` (integer overflow). No destination register or storage receives the wrapped value.
- **Quantity Multiplication (`mul a b`)**:
  Computes the 128-bit unsigned product $a \times b$. The upper 64 bits are checked (`UMULH != 0`). If nonzero, an immediate branch to `trap 1` is taken.
- **Quantity Subtraction (`sub a b`)**:
  Computes $a - b$ in $\mathbb{Z}$. If $a < b$, an unsigned underflow (borrow) occurs, triggering an immediate branch to `trap 1` (integer underflow).

### 4.2 Slices and Subviews
To construct a slice or view of length $k$ at offset $o$ from an object of length $L$:
1. The check $o \le L$ is evaluated first. If false, execution traps immediately (`trap 3`: index/bounds violation).
2. The remaining capacity $R = L - o$ is computed. Because $o \le L$, $R$ cannot underflow.
3. The check $k \le R$ (i.e. $k \le L - o$) is evaluated. If false, execution traps immediately (`trap 3`).
4. **Prohibition of Wrapped Bounds Check**:
   The check MUST NOT be implemented as $o + k \le L$. In 64-bit modular arithmetic, if $o = 2^{64} - 2$ and $k = 4$, $o + k = 2$, which trivially satisfies $2 \le L$, leading to severe memory safety violations. Evaluating $o \le L$ followed by $k \le L - o$ prevents arithmetic wrapping entirely.

---

## 5. Preservation of Modular Cryptography

Nothing in this specification alters the semantics of existing modular types:
- `ulong`, `uint`, `ushort`, `uchar` remain pure wrapping types.
- Hash algorithms, ciphers, and MAC implementations continue to compile with identical machine instructions and cycle counts.
- The translation-v1 S1 pure-u64 profile and existing contracts under #625 retain their exact behavior.

---

## 6. Implementation Status and Roadmap

- **Phase 1 (This Slice, seed/omc)**: Add the `qty` type to the seeded self-hosting compiler `seed/omc/omc.om`, with checked `add`, `sub`, and `mul` trapping on overflow (`trap 1`), explicit conversion via `qty(...)`, compile-time refusal of implicit mixing with `ulong` (error 7), and slice invariants in `part`.
- **Phase 2 (Library & Nominals)**: Formalize `struct qty`, `struct qobj`, and `struct qslic` in the standard library (`om/quantity/`) for nominal sub-range capacities and explicit result codes (0 success, 1 invalid operand, 2 domain mismatch, 3 overflow/underflow, 4 object mismatch).
- **Phase 3 (Substrate Lowering & IR)**: Lower `qty` through intermediate representations with static range analysis and formal proofs for bounds check elimination.
