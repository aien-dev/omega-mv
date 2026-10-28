# TileLab: independent Wolfram references

Every number in `expected.txt` was reproduced by Wolfram Language. The program never reads these numbers.
The evaluator ran on the hosted service `https://agenttools.wolfram.com/mcp` (server "Wolfram" 2026.09.15).
On 2026-10-28 it was called over direct MCP HTTP, because this session's `mcp.json` does not mount it.

## Planner (model, not a time prediction)

The query rebuilds the 405-configuration search from its definition: `cfg`, `regs = 4(4 rt + 2 ct + rt ct)`,
`BM = 16 rt wm`, `BN = 8 ct wn`, `Kp = 32 ceil(K/32)`,
`bytes = 2 M Kp ceil(N/BN) + 2 N Kp ceil(M/BM) + 4 M N`, and `cost = bytes ceil(ctas/48) 48 / ctas`.
It takes the first minimum and applies a register budget of 100.

| shape | Wolfram pick | bytes | floor(cost) |
|---|---|---|---|
| 1024^3 | rt2 ct4 wm4 wn4 band 8 | 37748736 | 56623104 |
| 4096^3 | rt2 ct4 wm4 wn4 band 8 | 2214592512 | 2283798528 |
| 2048x8192x2048 | rt2 ct4 wm4 wn4 band 8 | 1140850688 | 1176502272 |
| 100^3 | rt2 ct2 wm1 wn1 band 0 | 321600 | 551314 |

With a register budget of 20, zero configurations fit, so the program refuses with rc 2.
The issue #906 guide values come from `UnitConvert` on the same formula: 64x64 at 1024^3 is 71303168 B, which is 68 MiB, and 64x128 is 54525952 B, which is 52 MiB.

## Streaming softmax (m, s, p)

The fixture has 32 scores: `x_i = (mod(37 i + 11, 64) - 32)/4` and `v_i = (mod(53 i + 7, 41) - 20)/8`, for i = 0..31.
It is split into four tiles of 8. The merge rule is
`(m1,s1,p1) + (m2,s2,p2) = (m, s1 e^(m1-m) + s2 e^(m2-m), p1 e^(m1-m) + p2 e^(m2-m))` with `m = max(m1, m2)`.

- Exact identity check: `FullSimplify[p/s - direct] === 0` is True for the sequential fold and for the balanced tree.
  The two folds give identical (s, p). The check used exact rationals and `Exp`.
- `Floor[(direct + 100) 10^9]` gives 99264610583, which is the program's `direct_plus100_e9`. The direct value is -0.735389416339536.
- Mutant 1 averages tile-local normalisations: `Floor[|rel err| 10^12]` gives 158400527060, the program's `one_err_ppt`.
- Mutant 2 merges without the rescale: the same measure gives 108396641107, the program's `seq_err_ppt` and `tree_err_ppt`.
- Mutant 3 uses the wrong tree order, so only tiles 0 and 1 reach the result: the same measure gives 1235283119784, the program's `tree_err_ppt`.

The tolerance is 2e-6 relative (2000000 ppt), and it applies to the f64 host evaluation only.
Exact real associativity does not make float merges bitwise associative.
The GB10 tolerance is a separate figure, owned by CUDApp#153.
