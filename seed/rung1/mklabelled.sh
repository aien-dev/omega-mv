#!/bin/sh
# mklabelled.sh <hex1.hex> -- CHECK INPUT ONLY. Writes a copy of hex1.hex where every branch's
# hand-counted distance is wiped (imm19 field set to 0) and replaced by a label mark '@c', with ':c'
# placed before each target. hex1 must turn that copy back into exactly hex1.bin: then hex1's own label
# arithmetic and my hand-counted distances agree on all 50 branches. It never makes a byte of seed/.
awk 'BEGIN { L="abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ" }
  { line[NR]=$0 }
  /# \[[0-9]+\].*-> \[[0-9]+\]/ { match($0,/-> \[[0-9]+\]/); t=substr($0,RSTART+4,RLENGTH-5)+0; if (!(t in lab)) lab[t]=substr(L,++nl,1) }
  END { for (n=1;n<=NR;n++) { s=line[n]
      if (match(s,/# \[[0-9]+\]/)) { i=substr(s,RSTART+3,RLENGTH-4)+0; if (i in lab) print ":" lab[i] }
      if (s ~ /-> \[[0-9]+\]/) { match(s,/-> \[[0-9]+\]/); t=substr(s,RSTART+4,RLENGTH-5)+0
        w=strtonum("0x" substr(s,1,8)); w=and(w, 0xFF00001F); s=sprintf("%08x@%s", w, lab[t]) substr(s,9) }
      print s } }' "$1"
