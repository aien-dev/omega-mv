W-RC2 small-weight chip checks of the own30b/rc2 omega merge (HARDWARE, GB10, synthetic weights, under /tmp/aien-gb10.lock
while the live :18091 server stayed up; no 30 GB load). Binaries built from this commit (commit.txt) with -Werror into
/tmp/wrc2/omega; digests in binaries.sha256 (libomega_gpu.a 1baabd41... = the archive linked into sc own30b/rc2).
- dgraph-resident-rows2.log: gpu_decode_graph_test 2 layers, vocab 4096, resident grid 128 rows 2: 20/20 PASS.
  Resident ON bit-identical to the launched list (x, hidden, logits, token) every rep; no-barrier red differs (caught).
- pfa16-chip-512.log: gpu_pfa16_test --chip --tokens 512 (W8A16 kernels, real layer shapes): GPU_PFA16: PASS.
Host (same build): cmdlist 20+39, decode_graph 24, fp8_api 1482+70, decode_attn 41+12, router 69+10, gpr_boundary 298,
pfhost/qgemm/pfa16 host+sim PASS, resident_chain --host 26: all PASS.
