# L386 Omegese decode gate on the 35B: golden tokens (C1Land window886.sh from C1Lang golden.sh, 20261027T1634Z)

green (AIEN_OMEGA_Q36_OMEGESE_GATE=1, gate code fabdfc55): **PASS** - 8/8 cells PASS ( 8 PASS ); log proof 'omegese gate on' fabdfc55: yes
red (mutated gate a*sigmoid(-g), code 09522207): **RED-OK** - 6/6 cells RED-OK ( 6 RED-OK ); log proof 'omegese gate on' 09522207: yes
GB10 lock held 209 s; locked part exit 0; port 18195 (127.0.0.1), loads via q36-extra

Builds: see HEADS.txt and BUILD-*.txt. The green lib differs from the live lib (6de999da, sha256 110295ef, rebuilt here byte-identical) only by
the lever commits da99bf8bc edd47302f 2f6561d4b. Tables: green/verdict.md, red/verdict.md.
