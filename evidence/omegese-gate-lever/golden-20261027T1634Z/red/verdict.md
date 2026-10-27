# merge-train verdict: red

PASS/FAIL: identical / not identical to the reference. RED-OK/RED-FAIL: a control that must differ did / did not. NOT-ON-TRAIN: the lever (or its switch) is not in this candidate. SKIP: arm disabled in suite.json. ERROR: no verdict (no reply, 400, missing reference). -: the lever has no case there. A cell is the worst of the lever's arms.

| lever | name | VERDICT | golden-chat-max-24-red | golden-chat-code-red | golden-chat-prose2-red | golden-chat-chat-red | golden-chat-review-red | concurrent-32-red |
|---|---|---|---|---|---|---|---|---|
| L386-red | mutated Omegese gate (control) | **PASS** | RED-OK | RED-OK | RED-OK | RED-OK | RED-OK | RED-OK |

red controls (the comparison can fail): 6 RED-OK of 6 run, 6 in the suite

## cells (arm x case)

| arm | case | verdict | detail |
|---|---|---|---|
| plain | golden-chat-max-24-red | RED-OK | switched reply differs from the golden, as required |
| plain | golden-chat-code-red | RED-OK | switched reply differs from the golden, as required |
| plain | golden-chat-prose2-red | RED-OK | switched reply differs from the golden, as required |
| plain | golden-chat-chat-red | RED-OK | switched reply differs from the golden, as required |
| plain | golden-chat-review-red | RED-OK | switched reply differs from the golden, as required |
| plain | concurrent-32-red | RED-OK | rows 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 differ from alone as required (expected red [0], 0 others identical, 0 swap(s) logged) |
