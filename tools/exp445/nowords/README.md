# nowords: Omegese without the human words

The tools of the no-words experiment (evidence/NO-WORDS, docs/one_layer/OMEGESE_MATH.md). Three spellings of the
same language:

- `omegese`: the words as they are (`add`, `while`, `uint`);
- `symbols`: every fixed word and every chosen name is one meaningless token (a shape or a katakana sound),
  `symbols.map`, one token each on the 27B tokenizer;
- `numbers`: every fixed word is a number 1..48, names are 50..99, number values are written `#N`, `numbers.map`.

`nowords.sh` renders each arm's instructions from one template (`omegese.md.tmpl`; the words render equals
`../arms/omegese.md` byte for byte), translates the worked examples, translates a model's answer back to Omegese
words for the unchanged `omegese emit`, and shows a refusal in the arm's own spelling. `test_nowords.sh` checks all
of it (`make omegese omega-emit` first; `NOWORDS_TOKCOUNT` and `NOWORDS_TOKENIZER` add the one-token gate).

Run an arm set with the exp445 harness: `EXP445_PARAMS=tools/exp445/PARAMS.nowords tools/exp445/run.sh --config N ...`.
This is a host-only research tool: no hardware is involved beyond the model server it asks.
