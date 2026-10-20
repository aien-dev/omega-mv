# 35B plans from L308's measured heads (2026-10-20, L312)

`heads-0.90.l308-copy.json` is L308's measured per-domain head file (omega feat/typewriter-engine sibling branch
feat/typewriter-experts, `evidence/typewriter/heads-0.90.json`: the experts that cover 90 percent of routed mass per
layer per domain on the 35B). `plan-<domain>.json` = `engine plan l308 MODEL_DIR heads-0.90.json <domain> OUT --buffers 2`
(synthetic false). `schedule-<domain>.check.json` = the host proof of the schedule built from each plan with the 35B
manifest for 4 tokens: complete, acyclic, no collision, exactly the true edges: PASS for prose, code, math.
Head sizes per layer (90 percent coverage): prose 48 to 168 experts, code 82 to 194, math 93 to 179, of 256; at
3.146 MB per expert a prose beat moves 151 MB to 529 MB. The full schedule JSON files are not kept (3 MB each, rebuilt
by the command above in under a second).
