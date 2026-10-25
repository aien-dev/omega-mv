#!/bin/sh
# mk4.sh -- build om4.bin with asm2 (seed/rung2) and write its size into the p_filesz word of om4.s.
# asm2 has no size arithmetic, so the size is measured and written into the text; the build is then
# repeated so the image carries its own size (gate4.sh checks p_filesz = file size).
set -eu
T="seed/rung2/table.s2 seed/rung3/table3.s2 seed/rung4/table4.s2"
cat $T seed/rung4/om4.s | seed/rung2/asm2.bin > seed/rung4/om4.bin
n=$(stat -c %s seed/rung4/om4.bin)
sed -i "s/^  \.w 0x[0-9a-f]* ; p_filesz = the size of om4.bin.*/  .w $(printf 0x%08x $n) ; p_filesz = the size of om4.bin, $n bytes (asm2 has no size arithmetic; gate4.sh checks it)/" seed/rung4/om4.s
cat $T seed/rung4/om4.s | seed/rung2/asm2.bin > seed/rung4/om4.bin
chmod +x seed/rung4/om4.bin
echo "om4.bin $(stat -c %s seed/rung4/om4.bin) bytes"
