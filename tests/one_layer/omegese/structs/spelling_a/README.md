# Spelling A (measured, not chosen)

The six struct twins written in candidate spelling A: `p = new S` makes every field zero and
`put p f v` sets each one. Spelling B (`p = new S v1 v2 ...`, the values in field order) needed
fewer tokens on these programs and is the language (docs/one_layer/OMEGESE.md section 3); the
counts are in evidence/OMEGESE-STRUCTS/tokens.md. These files are kept so the count can be
repeated; `omegese` built with `-DOMEGESE_STRUCT_A` accepts them.
