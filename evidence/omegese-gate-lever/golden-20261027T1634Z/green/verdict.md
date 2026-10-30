# merge-train verdict: green

PASS/FAIL: identical / not identical to the reference. RED-OK/RED-FAIL: a control that must differ did / did not. NOT-ON-TRAIN: the lever (or its switch) is not in this candidate. SKIP: arm disabled in suite.json. ERROR: no verdict (no reply, 400, missing reference). -: the lever has no case there. A cell is the worst of the lever's arms.

| lever | name | VERDICT | golden-chat-max-24 | golden-chat-code | golden-chat-prose2 | golden-chat-chat | golden-chat-review | concurrent-32 | turn2 | tool-call |
|---|---|---|---|---|---|---|---|---|---|---|
| L386 | Omegese decode gate | **PASS** | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |

red controls (the comparison can fail): 0 RED-OK of 0 run, 0 in the suite

## cells (arm x case)

| arm | case | verdict | detail |
|---|---|---|---|
| plain | golden-chat-max-24 | PASS | ids identical to the golden (24 prompt tokens) |
| plain | golden-chat-code | PASS | ids identical to the golden (31 prompt tokens) |
| plain | golden-chat-prose2 | PASS | ids identical to the golden (37 prompt tokens) |
| plain | golden-chat-chat | PASS | ids identical to the golden (40 prompt tokens) |
| plain | golden-chat-review | PASS | ids identical to the golden (66 prompt tokens) |
| plain | concurrent-32 | PASS | 32/32 replies identical to the reference (alone) |
| plain | turn2 | PASS | turn 2 identical to alone |
| plain | tool-call | PASS | reply is a parsed tool call to get_weather ("{\"city\":\"Paris\"}") |
