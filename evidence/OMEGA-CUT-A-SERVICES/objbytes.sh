#!/bin/sh
# L202: byte-compare every build/*.o compiled from omega main a8ffd2b9 and from this branch HEAD, same flags.
P=$HOME/workspace/physics-pf-6d7cf0d4
for t in base head; do
  ( cd /tmp/l202-ob/$t && for o in $(cat /tmp/l202-ob/objs.txt); do
      make -s PHYSICS_DIR=$P AIENOS_LOCK_REPO=$HOME/workspace/pipeline/clones/aienos build/$o >/dev/null 2>&1 || echo "$t $o build_failed"; done ) 
done > /tmp/l202-ob/build-fail.txt
( cd /tmp/l202-ob && for o in $(cat objs.txt); do
    if cmp -s base/build/$o head/build/$o; then echo "same $o"; else echo "DIFF $o"; fi; done ) > /tmp/l202-ob/cmp.txt
( cd /tmp/l202-ob && sha256sum base/build/*.o head/build/*.o ) > /tmp/l202-ob/sha256.txt
echo done > /tmp/l202-ob/DONE
