#!/bin/sh
set -eu
out=build/L533
# Deliberately wrong compiler input with unchanged dependency graph/recipe count.
node <<'JS'
const fs=require('fs');let s=fs.readFileSync('build/L533/pr495.Makefile','utf8');let n=0;
s=s.replace(/(\$\(OUT_DIR\)\/(?:m16_native|nvrm)\.o:[^\n]*\n)\t\$\(CC\) \$\(CFLAGS\) -c \$< -o \$@/g,(_,rule)=>{n++;return rule+'\t$(CC) $(CFLAGS) -c src/sha256.c -o $@';});
if(n!==2)throw Error('expected two recipes');fs.writeFileSync('build/L533/wrong-source.Makefile',s);
JS
sh tests/physics_build_identity_test.sh "$out/wrong-source.Makefile" "$out/physics-wrong-source" dirty
printf '%s\n' 'LIMIT CONFIRMED: wrong source recipe passes spy; no compiled-artifact/source-argument qualification claimed'
