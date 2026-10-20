# NO-WORDS: pre-registered prediction (written before any generation)

Written 2026-10-20 ~00:20Z by the L108 agent, committed and pushed before the first model request of this
experiment (G0 included). The commit that adds this file is the proof of order.

## Question
Drake: "Take out the human words." Omegese's fixed words (`add`, `while`, `uint`, ...) were chosen from the
model's own vocabulary. Under the legal-next-word mask they are arbitrary labels (docs/one_layer/OMEGESE_MATH.md
section 4), so they matter only through the model's prior. Do they help, hurt, or do nothing?

## Design (frozen in tools/exp445/PARAMS.nowords)
- Arms, same grammar, same reader, same tasks, same prompts apart from the spelling of program text:
  - **A words** (`omegese`): Omegese as it is; the prompt is byte-identical to the r3 omegese arm.
  - **B symbols**: every fixed word is one shape or katakana token (48), every chosen name one of 24 more,
    each one token of the 27B tokenizer with its leading space (`tools/exp445/nowords/symbols.map`, drawn by a
    seeded shuffle; checked by `test_nowords.sh`). Number values stay digits.
  - **C numbers**: every fixed word is its place in `omegese words` (1..48), chosen names are 50..99, a number
    value is written `#N` (needed: otherwise `17` could be the word 17 or the value 17).
  The instructions of all three arms come from one template (`tools/exp445/nowords/omegese.md.tmpl`); its words
  render is byte-identical to `tools/exp445/arms/omegese.md`. The 14 worked examples are the same programs,
  translated by the map. The task text (signature and prompt, in English) is the same in every arm.
- Reader: B and C are translated back to Omegese words (`nowords.sh decode`) and then read by the unchanged
  `omegese emit` -> omega_emit -> signature and back-end check. An English fixed word inside B or C is refused
  (it is not that arm's vocabulary); plain identifiers (the task's function name, a name the model invents) and
  the IR type names `bool u8 .. i64` (the task's own notation) are accepted in every arm. A refusal is shown to
  the model in its arm's spelling (names, quoted program text, op and type words).
- Model: qwen3.8-27b-fp8 on 127.0.0.1:18091 (config N), temperature 0.7, top_p 0.8, max 3072 tokens a round,
  seeds as r3 (seed_base 445000), many-shot prompt, one repair round, 3 attempts per task.
- **No mask while the model writes**: the server has no option for it. So the arms differ in two ways: the
  prior on the right continuation, and how often the model writes something the reader refuses.
- Tasks: the 10 public dev tasks (`tests/one_layer/exp445/dev`).
- Primary measure: attempt 1, final candidate (after at most one repair round), harness ACCEPT on hidden inputs
  (`score.sh`). Secondary: round 1 alone; all 3 attempts pooled (30 generations per arm); reader acceptance;
  completion tokens.

## Measured before the run (no model involved)
Reference programs of the 10 dev tasks (`tests/one_layer/omegese/dev`), translated by the maps, counted with the
27B tokenizer (`tokcount`, glyph-tok, on `qwen3.8-27b-fp8-017b9c7a/tokenizer.json`); all 30 pass the reader:

| spelling | tokens, 10 programs |
|---|---|
| words | 449 |
| symbols | 449 |
| numbers | 793 (x1.766, calc:2169b1a422df845b5c8a3e375bb9e12fb188d49c62c7ed5cf3813251355c7c9d) |

Prior result for scale: the 30B (config A), same omegese prompt, many-shot, dev tasks: 6/10 attempt 1
(evidence/ONE-LAYER-2/generations/score-20261015T170304Z-1623767).

## Prediction (attempt 1, final candidate, of 10)

| arm | correct | passes the reader, round 1 | completion tokens per generation vs A |
|---|---|---|---|
| A words | **7** | 8 | 1.00 |
| B symbols | **4** | 5 | 1.00 to 1.15 |
| C numbers | **2** | 3 | 1.6 to 2.0 |

- A words, 7: the 27B is a newer model than the 30B that scored 6/10 on the same prompt, and the words match
  what it saw in training.
- B symbols, 4: same grammar and same examples, but every word must be looked up on the card; I expect more
  arity and type slips (the categories that already dominate Omegese refusals) and some answers that fall back
  to English words.
- C numbers, 2: numbers fight the model's strongest prior (a number is a value), and the `#` rule is unusual;
  I expect the most refusals and the most wrong programs.
- Tokens: B equals A by construction for the same program (measured 449 = 449); slightly more if repairs are
  more frequent. C costs about the measured 1.77x for the same program.
- Overall: **human words help** (A > B > C on correct answers); A and B tie on tokens; C loses on both.

## Noise, stated before the run
With 10 tasks, a difference of 3 between two arms is not significant: the paired exact McNemar test needs about
6 discordant tasks all in one direction for p < 0.05. To detect 60% vs 40% (20 points) with 80% power at
alpha 0.05 (unpaired, normal approximation) needs about 94 tasks per arm
(calc:a3bc0d136f7b303fde21088a21f3aacaf2e0a98a2a172d549aef4f445af09885). The pooled 30 generations per arm are
not 30 independent tasks (attempts of one task were identical on 8 of 10 tasks in the 30B run). If the arms land
within 3 of each other, the verdict is "within noise at n = 10".
