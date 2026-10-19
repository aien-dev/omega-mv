#!/bin/sh
# audit.sh <file.hex> <file.bin> <a64check> -- REFERENCE CHECK ONLY (never makes a byte of seed/).
# For every word of a hand-written hex file: the word as written, the instruction named in its comment,
# what our own decoder (src/aarch64_decoder.c via a64check) reads from it, what GNU objdump
# (-M no-aliases, a second independent decoder, reference only) reads from it, and whether each agrees
# with the name. Words whose comment does not start with [n] are data (ELF fields); their value is
# checked by the self-rebuild and by readelf. objdump spells LO as CC and HS as CS (the same codes, Table C1-1);
# those two spellings are mapped to LO and HS before comparing. Prints a Markdown table, then a summary line
# "AUDIT words=W code=C data=D decoder_mismatch=M objdump_mismatch=O byte_mismatch=B". Exit 0 iff all 0.
set -eu
hex=$1 bin=$2 chk=$3
t=$(mktemp -d /tmp/seed-audit.XXXXXX)
# 1. words + comments in file order
awk '{ l=$0; c=""; i=index(l,"#"); if (i) { c=substr(l,i+1); l=substr(l,1,i-1) } gsub(/[ \t\r]/,"",l);
       if (l!="") print l "\t" c }' "$hex" > "$t/w"
# 2. the words actually in the binary, read back with od (4-byte little-endian units)
od -An -v -tx4 -w4 "$bin" | tr -d ' ' > "$t/b"
cut -f1 "$t/w" | tr 'A-F' 'a-f' > "$t/wl"
"$chk" decode < "$t/wl" > "$t/d"
objdump -D -z -b binary -m aarch64 -M no-aliases "$bin" | awk -F'\t' '/^ *[0-9a-f]+:\t/ { o=$3; if ($4!="") o=o "\t" $4; sub(/ *\/\/.*$/,"",o); sub(/^b\.cc\t/,"b.lo\t",o); sub(/^b\.cs\t/,"b.hs\t",o); print o }' > "$t/o"
paste "$t/w" "$t/b" "$t/d" "$t/o" | awk -F'\t' '
function hx(v) { return sprintf("0x%x", v) }
function num(s) { s=s; if (s ~ /^#/) s=substr(s,2); return (s ~ /^0x/) ? strtonum(s) : s+0 }
function reg(r) { if (r=="xzr"||r=="wzr"||r=="sp"||r=="wsp") return 31; return substr(r,2)+0 }
function sf(r) { return (substr(r,1,1)=="x") ? 1 : 0 }
BEGIN { cc["eq"]=0;cc["ne"]=1;cc["hs"]=2;cc["cs"]=2;cc["lo"]=3;cc["cc"]=3;cc["mi"]=4;cc["pl"]=5;cc["vs"]=6;cc["vc"]=7;
        cc["hi"]=8;cc["ls"]=9;cc["ge"]=10;cc["lt"]=11;cc["gt"]=12;cc["le"]=13;cc["al"]=14;cc["nv"]=15
        print "| Offset | Word | Bytes in file | Named (hand-decoded, from the comment) | Our decoder | ours | objdump -M no-aliases | objdump | Class |"
        print "| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |" }
{ w=$1; com=$2; fb=$3; dec=$4; ob=$5; if ($6!="") ob=ob " " $6; gsub(/\t/," ",ob); gsub(/ +/," ",ob)
  off=(NR-1)*4; words++; if (fb!=tolower(w)) bytebad++
  if (com !~ /^ *\[[0-9]+\]/) { data++; c=com; sub(/^ */,"",c); gsub(/\|/,"/",c)
    printf "| `0x%04x` | `%s` | `%s` | %s | - | n/a | - | n/a | DATA |\n", off, w, fb, c; next }
  code++
  n=com; sub(/^ *\[[0-9]+\] */,"",n); i=index(n,"  "); if (i) n=substr(n,1,i-1)
  i=index(n," ("); if (i) n=substr(n,1,i-1); i=index(n," ->"); if (i) n=substr(n,1,i-1)
  sp=index(n," "); m=(sp?substr(n,1,sp-1):n); rest=(sp?substr(n,sp+1):"")
  k=split(rest,a,/, */)
  # expected objdump text
  eo=m; ops=""
  for (j=1;j<=k;j++) { x=a[j]
    if ((m=="b"||m ~ /^b\./||m=="cbz"||m=="cbnz") && j==k) x=hx(off+4*(x+0))
    else if (x ~ /^#/ && x !~ /^#0x/) x="#" hx(x==""?0:substr(x,2)+0)
    else if (x ~ /^lsl #/) x=x
    ops=ops (j>1?", ":"") x }
  if (ops!="") eo=eo " " ops
  # expected decoder fields
  E_op="INVALID"; E_sf=0; E_rd=0; E_rn=0; E_rm=0; E_imm=0; E_hw=0; E_b=0; E_c=0
  if (m=="movz"||m=="movk") { E_op=toupper(m); E_sf=sf(a[1]); E_rd=reg(a[1]); E_imm=num(a[2]); if (k>=3) { s=a[3]; sub(/lsl #/,"",s); E_hw=s/16 } }
  else if (m=="add"||m=="sub"||m=="orr"||m=="and"||m=="eor"||m=="mul") { E_op=toupper(m); E_sf=sf(a[1]); E_rd=reg(a[1]); E_rn=reg(a[2]); E_rm=reg(a[3]) }
  else if (m=="subs") { E_op="SUBS"; E_sf=sf(a[1]); E_rd=reg(a[1]); E_rn=reg(a[2]); if (a[3] !~ /^#/) E_rm=reg(a[3]) }
  else if (m ~ /^b\./) { E_op="B_COND"; E_c=cc[substr(m,3)]; E_b=(a[1]+524288)%524288 }
  else if (m=="b") { E_op="B"; E_b=(a[1]+67108864)%67108864 }
  else if (m=="cbz"||m=="cbnz") { E_op=toupper(m); E_sf=sf(a[1]); E_rd=reg(a[1]); E_b=(a[2]+524288)%524288 }
  else if (m=="ldrb"||m=="strb") { E_op=toupper(m); E_rd=reg(a[1]); r=a[2]; gsub(/[\[\]]/,"",r); E_rn=reg(r) }
  else if (m=="ldr"||m=="str") { E_op=toupper(m); E_sf=sf(a[1]); E_rd=reg(a[1]); r=a[2]; gsub(/[\[\]]/,"",r); E_rn=reg(r) }
  else if (m=="ret") { E_op="RET" }
  ed=sprintf("op=%s sf=%d rd=%d rn=%d rm=%d imm16=%d hw=%d bimm=%d cond=%d",E_op,E_sf,E_rd,E_rn,E_rm,E_imm,E_hw,E_b,E_c)
  dok=(ed==dec)?"agree":"MISMATCH"; if (E_op=="INVALID" && dec ~ /^op=INVALID/) dok="not covered (agree: INVALID)"
  if (dok=="MISMATCH") dbad++
  ook=(eo==ob)?"agree":"MISMATCH"; if (ook=="MISMATCH") obad++
  printf "| `0x%04x` | `%s` | `%s` | `%s` | `%s` | %s | `%s` | %s | INSTRUCTION |\n", off, w, fb, n, dec, dok, ob, ook }
END { printf "\nAUDIT words=%d code=%d data=%d decoder_mismatch=%d objdump_mismatch=%d byte_mismatch=%d\n", words, code, data, dbad+0, obad+0, bytebad+0
      exit (dbad+obad+bytebad) ? 1 : 0 }'
rc=$?
nw=$(wc -l < "$t/w"); nb=$(wc -l < "$t/b")
[ "$nw" = "$nb" ] || { echo "AUDIT FAIL: $nw words written, $nb words in the binary"; rc=1; }
rm -f "$t"/w "$t"/wl "$t"/b "$t"/d "$t"/o; rmdir "$t"
exit $rc
