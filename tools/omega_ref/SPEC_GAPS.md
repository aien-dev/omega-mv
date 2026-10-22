# What the rule book does not say yet, and two om4 slips (L412)

Found while writing the reference interpreter from `docs/one_layer/OMEGESE.md` alone, then running
small probe programs through both om4 and the interpreter (`tools/omega_ref/probe_ref.sh`, log in
`evidence/OMEGA-REF/probe_ref.log`: 125 probes, PROBE_REF PASS, RC=0).

Decision 19 says no meaning may be left open. Each gap below gets the sentence we propose adding to
OMEGESE.md section 9. Unless a line says otherwise the proposal is what om4 already does, and the
interpreter does the same. Cases where om4 does something else are marked **om4 differs**.

## om4 slips (the rule book decides; ledger fixer line, L408 told)

- **F1. ulong numbers from 2^63 up are refused.** Section 9.3 makes ulong a 64-bit number, so
  `ulong 9223372036854775808` (2^63) and `ulong 18446744073709551615` (2^64 - 1) are numbers of that
  type. om4 refuses the first with error 8 and the second with error 6. Probes `conv_sign`,
  `cmp_unsigned`, `write_wrap`. 2^63 - 1 and every signed long are read correctly (`u63`,
  `rem_i64_min`). The interpreter accepts them and gives the spec's answers (105, 0, 103).
- **F2. `view x` passed to an edit parameter gives error 7; 9.4 says 20.** 9.4: "A view is read-only;
  `put`, `read` into it, or passing it as edit is error 20." om4 already gives 20 when a view
  *parameter* is passed as edit (gate4) and when a text is (`text_edit`), but 7 for `s view x`
  (`view_pfx_to_edit`). Only the error number is wrong; the program is refused either way.

## Gaps (proposed sentences)

- **G1. om4 has no `break` and no `trap` line.** Section 3 lists both; om4 refuses them with error 9
  (`break_inner`, `break_while`, `after_break`, `trap_1`, `trap_14`, `trap_15`). Proposed: "om4 and omc
  read every word of section 3 except `break`, `trap` and `own`, which are error 9."
- **G2. No conversion from or to bool.** `int b` with b bool, `bool x` with x uint: om4 error 9.
  Proposed: "A conversion takes and gives one of the eight whole-number types; bool is never converted
  (error 9). Use `if` to turn a bool into a number."
- **G3. A bare number with no type to take.** `x = 5` for a new x: om4 error 11. Proposed: "A bare number
  where the place gives no type (the whole value of a new name) is error 11; write it typed (`int 5`)."
- **G4. main may return bool.** 9.1 says "a whole-number type or void"; 9.3 calls true and false "the
  bool numbers". om4 accepts `fn main to bool` (exit 1 for true). Proposed: "main returns any number
  type, bool included, or void."
- **G5. How a number is spelled.** Section 6.1 (the C path) says no leading zeros and no `-0`; section 9
  is silent. om4 reads `07` as 7 and `-0` as 0. **om4 differs** from the proposal. Proposed: "Numbers
  are written as in 6.1: decimal digits, an optional minus, no leading zeros, and never `-0` (error 6)."
  Reason: decision 22 wants one spelling per word, so the writer's mask has one legal next character.
  The interpreter follows the proposal.
- **G6. A text may be passed as a view.** 9.4 says an argument is a local array or an array parameter,
  and a global is never passed; a text is neither, and it is not said whether a text counts as a
  global. om4 accepts a text to a view parameter (`text_pass`) and refuses it to an edit one with 20.
  Proposed: "A text may be passed to a view parameter (it is read-only, so no write is ever seen through
  another name); to an edit parameter it is error 20. A pool is never passed (error 19)."
- **G7. The type of a bare index number.** om4 reads it as ulong: a negative one is error 8, one at or
  past the length is error 21, even past 2^32 (`idx_lit_neg`, `idx_2_32`). Proposed: "A bare number
  index is a ulong (error 8 when negative) and must be below the length (error 21)."
- **G8. read and write on arrays that are not uchar.** 9.5 says "A must be an array of uchar for the
  counts to be bytes" but not what happens otherwise. om4 accepts `write 1 g 0 1` with g a ulong pool
  and writes one byte. **om4 differs** from the proposal. Proposed: "read and write take an array of
  uchar only (error 7)." Reason: decision 20 (a byte count is not an element count) and no meaning to
  define for a partial element.
- **G9. A line after an if/else whose two branches both end the path** is error 17 in om4
  (`after_ifelse`). Proposed: "An if with an else whose two blocks both end the path ends it too: a line
  after its `end` is error 17, and the function's `end` after it is not reachable (no error 18)."
- **G10. A `for` count written as a number has no 1000000 limit.** om4 runs `for i ulong 1000001`
  to the end. Proposed: "A for count written as a number may be any value of the loop's type and has
  no pass limit; only a computed count is guarded (trap 4 after 1000000 passes)."
- **G11. When `put` checks its index.** om4 reads the index, then the value, then checks the index
  (`put_idx_first`: a value that divides by zero traps 2 before a bad index traps 3). Proposed: "put A i v
  reads i, then v; then it traps 3 if i is not below the length; then it writes."
- **G12. An empty text.** `text t ""` is accepted, length 0. Proposed: "A text may be empty (length 0)."
- **G13. Tabs separate words** like spaces (`tab_sep`). Proposed: "Words are separated by spaces or tabs."
- **G14. The IR type names `u8` .. `i64`** are read as in section 3 (`ir_types`). Proposed: keep section
  3's sentence and say it holds for om4.
- **G15. A carriage return before the newline** is ignored (`crlf`). Proposed: "A carriage return at
  the end of a line is ignored."
- **G16. How deep a value nests.** 1024 `neg` words before a name is error 14 (`nest_1024`; gate4 has
  1025). Proposed: "A value holds at most 1024 words counted from its first to its innermost name or
  number (every op, conversion or call counts one); more is error 14."
- **G17. Writing the loop name inside a for** is accepted and changes the passes (`loop_name_assign`:
  `i = int 4` inside `for i int 5` gives one pass). Proposed: either refuse it (error 9, clearer) or
  say: "The loop name may be written in the body; the loop goes on from the written value."
- **G18. Error 12 cannot be decided from the text.** 9.6: "A function that needs more than the 16
  registers it hands out is error 12", but the rule book does not say how many registers a line needs.
  om4 refuses `regs17` (17 int names, then one line adding them all) at the adding line, not at the 17th
  name. **om4 differs**: the interpreter does not model registers and runs it. Proposed: "Each name
  in scope holds one register and each op waiting for its second value holds one more; more than 16 at
  any point is error 12" (or whatever om4 counts, written down).

## Not probed, but needed for decision 19

- **G19. The point where the stack runs out.** 9.2 says a call or local array that would go below the
  8 MiB pool traps 6, but not how many bytes a call uses. The interpreter charges the least any
  machine-code version must (16 bytes for a function that calls others, 0 for one that does not, plus
  each local array rounded up to 16 bytes), so it traps 6 only where every implementation must.
  Proposed: "A call uses at most F bytes of stack plus its local arrays" with om4's F written in.
- **G20. Which function is the entry in function mode.** 9.1 says "entry = word 0" but not which
  function's code is at word 0. In every gate4 case the entry is the last function. Proposed:
  "In function mode the last function is the entry; its code starts at word 0."
- **G21. Shifts mix types.** 9.3: "the two values of an op ... all have the same type, or the line is
  refused (error 7)"; section 3 and fm3/fm4 (`shl a s`, a long, s uint) let a shift amount have any
  whole-number type. Proposed: add "(except the shift amount of shl and shr, which may be any
  whole-number type)".
- **G22. Unread names.** Section 2 rule 4 refuses a name that is written but never read; om4 does not
  (fib.om and sort.om write `w` and never read it). Proposed: "om4 does not check that a written name
  is read" (or make om4 check it, and change the two tests).
- **G23. Local array length.** Section 3 says `new` makes 1 .. 64 elements; 9.4 allows up to 4 MiB in
  total. Proposed: "In om4 a local array may have any length; all local arrays of one function together
  hold at most 4 MiB."
- **G24. Order of top-level lines.** `global` and `text` may come after functions and are seen by the
  functions written after them (`global_after`); a function after main is accepted (`fn_after_main`).
  Proposed: "A pool or text is visible to the functions written after it."
