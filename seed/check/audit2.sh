#!/bin/sh
# audit2.sh <prog.s> <prog.bin> -- REFERENCE CHECK ONLY (never makes a byte of seed/).
# For a program written in asm2's language (seed/rung2) and the bytes asm2 made from it: every
# instruction line is read back by GNU objdump (-M no-aliases, the manual's base names) and must show
#   1. the base instruction the asm2 name stands for (map below, names as in the Arm ARM headings),
#   2. the registers written in the source, in the same order (objdump may show more, e.g. xzr),
#   3. for a label operand, the address of that label.
# Data lines (.w, .b) are skipped (counted). Prints a Markdown table of every instruction and a summary
# line "AUDIT2 instructions=N data_bytes=D mismatch=M"; exit 1 when M > 0.
set -eu
s=$1; b=$2
t=$(mktemp -d)
objdump -D -z -b binary -m aarch64 -M no-aliases "$b" | grep -E '^ +[0-9a-f]+:' |
  awk -F"\t" '{a=$1; sub(/^ +/,"",a); sub(/:$/,"",a); print a "\t" $2 "\t" $3 "\t" $4}' > "$t/od"
awk -v od="$t/od" '
function strip(x) { sub(/;.*/, "", x); gsub(/,/, " ", x); return x }
BEGIN {
  # asm2 name (up to the first dot, or the whole name for the forms below) -> objdump base mnemonic
  m["add"]="add"; m["sub"]="sub"; m["subs"]="subs"; m["cmp"]="subs"; m["and"]="and"; m["orr"]="orr"
  m["eor"]="eor"; m["mov"]="orr"; m["mul"]="madd"; m["madd"]="madd"; m["msub"]="msub"; m["udiv"]="udiv"
  m["lslv"]="lslv"; m["lsrv"]="lsrv"; m["asrv"]="asrv"; m["movz"]="movz"; m["movk"]="movk"
  m["ldr"]="ldr"; m["str"]="str"; m["ldrb"]="ldrb"; m["strb"]="strb"; m["b"]="b"; m["bl"]="bl"
  m["cbz"]="cbz"; m["cbnz"]="cbnz"; m["adr"]="adr"; m["ret"]="ret"; m["br"]="br"; m["blr"]="blr"
  m["svc"]="svc"; m["ubfm"]="ubfm"; m["sbfm"]="sbfm"
  while ((getline l < od) > 0) { split(l, f, "\t"); a0 = strtonum("0x" f[1]); txt[a0] = f[3] " " f[4]; wd[a0] = f[2] }
}
# pass 1 (first file read): label offsets
FNR == NR {
  x = strip($0); n = split(x, w, /[ \t]+/); k = 1; if (w[1] == "") k = 2
  if (k > n) next
  if (substr(w[k],1,1) == ":") { lab[substr(w[k],2)] = off; next }
  if (w[k] == ".b") off += 1; else off += 4
  next
}
# pass 2: check each instruction
{
  x = strip($0); n = split(x, w, /[ \t]+/); k = 1; if (w[1] == "") k = 2
  if (k > n) next
  if (substr(w[k],1,1) == ":") next
  if (w[k] == ".b") { off2 += 1; data += 1; next }
  if (w[k] == ".w") { off2 += 4; data += 4; next }
  name = w[k]; o = off2; off2 += 4; ins++
  d = txt[o]
  split(d, dw, /[ \t,\[\]!]+/); mn = dw[1]
  base = name; if (name ~ /^b\./) base = "b.c"; else sub(/\..*/, "", base)
  want = (base == "b.c") ? substr(name, 1) : m[base]
  if (base == "b.c") { gsub(/^b\.cs/, "b.hs", mn); gsub(/^b\.cc/, "b.lo", mn) }
  ok = (mn == want)
  # registers in the source, in order, must be a subsequence of those in the decode
  nd = 0; delete dr
  for (i = 2; i in dw; i++) if (dw[i] ~ /^([xw]([0-9]|[12][0-9]|30)|xzr|wzr|sp|wsp)$/) dr[++nd] = dw[i]
  j = 1
  for (i = k + 1; i <= n; i++) {
    a = w[i]
    if (a ~ /^([xw]([0-9]|[12][0-9]|30)|xzr|wzr|sp|wsp)$/) {
      while (j <= nd && dr[j] != a) j++
      if (j > nd) ok = 0; else j++
    } else if (a in lab) {
      tg = ""; for (q = 2; q in dw; q++) if (dw[q] ~ /^0x[0-9a-f]+$/) tg = dw[q]
      if (tg == "" || strtonum(tg) != lab[a]) ok = 0
    }
  }
  # numbers: the numbers in the source, scaled as the fields of the manual hold them, must equal the
  # #numbers of the decode, in order (LDR/STR unsigned offsets are in access-size units in the source; MOVZ/MOVK hw
  # shows as "lsl #16*hw"; objdump leaves out a zero unsigned offset and a zero hw shift)
  ns = 0; delete sn
  for (i = k + 1; i <= n; i++) if (w[i] ~ /^-?(0x[0-9a-f]+|[0-9]+)$/) sn[++ns] = strtonum(w[i])
  sc = 0; if (name ~ /^(ldr|str)\.x$/) sc = 8; else if (name ~ /^(ldr|str)\.w$/) sc = 4; else if (name ~ /^(ldrb|strb)$/) sc = 1
  ne = 0; delete ex
  if (sc) { if (sn[1] != 0) ex[++ne] = sn[1] * sc }
  else if (name ~ /^mov[zk]\./) { ex[++ne] = sn[1]; if (sn[2] != 0) ex[++ne] = sn[2] * 16 }
  else for (i = 1; i <= ns; i++) ex[++ne] = sn[i]
  nn = 0; delete dn
  for (i = 2; i in dw; i++) if (dw[i] ~ /^#-?(0x[0-9a-f]+|[0-9]+)$/) { v = substr(dw[i], 2); dn[++nn] = (v ~ /^-/) ? -strtonum(substr(v, 2)) : strtonum(v) }
  if (nn != ne) ok = 0
  for (i = 1; i <= ne; i++) if (dn[i] != ex[i]) ok = 0
  if (!ok) bad++
  printf("| %d | %s | %s | %s |\n", o, wd[o], x, (ok ? d : "MISMATCH: " d))
}
END { printf("\nAUDIT2 instructions=%d data_bytes=%d mismatch=%d\n", ins, data, bad + 0); exit(bad > 0) }
' "$s" "$s"
rc=$?
rm -rf "$t"
exit $rc
