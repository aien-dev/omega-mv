# 35b-host-20261021T0025Z: the smallest full loop on the 35B host

`run-host-35b.sh`, binary sha256 2f17072f66ad6a9f (built from the tree that became sc d3113940; the `run35` path is unchanged since sc 5afbd03c).
1. `trace35`: L306's golden `evidence/q36-host-20261020/gen-p1.json` (5 prompt ids, 32 greedy tokens, routes per step)
   -> `trace.json` (`golden.json` is the copy used).
2. `plan.json`: the golden's own routes (prefill union + 32 decode steps), 53 to 136 experts per layer, buffers 2.
3. `schedule.json` + `schedule.check.json`: 1,280 beats, 3,011 parts, 2 x 136 slots x 3,146,112 bytes; proof PASS.
4. `loop/`: `run35`, both engines on the host, L306's exact forward with every routed expert served from the ring.
   `gate.json`: identical_tokens true, 32/32, routes identical 32/32 steps, eager expert reads at open 0, 0 misfires.
   `summary.json`, `beats.json`, `diagram.txt`, `diagram.png`: the per-beat stamps and the diagram.
5. `move/`: the movement engines alone on the same schedule (stand-in fire): 1,280 beats, 0 late, 0 misfires.
Load at the loop's start 34.7, MemAvailable 100 GB; cells pinned to the X925 cores (taskset 5-9,15-19).
