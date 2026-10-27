# L386 Omegese decode gate on the 35B: golden tokens (C1Land window886.sh from C1Lang golden.sh, 20261027T1631Z)

green (AIEN_OMEGA_Q36_OMEGESE_GATE=1, gate code fabdfc55): **FAIL** - 0
0/0
0 cells PASS (); log proof 'omegese gate on' fabdfc55: no
red (mutated gate a*sigmoid(-g), code 09522207): **RED-FAIL** - 0
0/0
0 cells RED-OK (); log proof 'omegese gate on' 09522207: no
GB10 lock held 4 s; locked part exit 2; port 18195 (127.0.0.1), loads via q36-extra

Builds: see HEADS.txt and BUILD-*.txt. The green lib differs from the live lib (6de999da, sha256 110295ef, rebuilt here byte-identical) only by
the lever commits da99bf8bc edd47302f 2f6561d4b. Tables: green/verdict.md, red/verdict.md.
