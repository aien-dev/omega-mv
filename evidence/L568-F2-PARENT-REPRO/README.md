# Independent parent F2 reproduction

Reviewed source7177ad74 / hash-pinned ELF6617b4be from L568. All six native/interpreter command/script/stdin results diverge from actual clean Bash for `exit bad 9` followed by builtin printf. Bash exits2 without continuation; current command exits1, streams continue with tail/status0. This is the preexisting bi_exit precedence defect, not a new fixture/source regression.

First probe retained with a0775 journal-directory refusal warning. Fresh0700 owned fixture repeats all six wrong results with no journal-unavailable warning. Source unchanged between probes. L577 is the distinct live6.1Solmedium fixer; L563 owns separate prompt signal race. No source repair or issue closure is claimed here. L562 evidence publication HOLD was separately corrected append-only at f2dbbb24.
