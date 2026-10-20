#!/bin/sh
# L202 final receipts on the pushed commit: every gate (gates.sh final) and object bytes vs main.
cd $HOME/workspace/omega-omega-native-platform-services || exit 9
EXTRA_TARGETS="test-gpu-encoding test-platform-services test-rx-seat-fake test-r16-authpath-linkmap-silicon" \
  $HOME/handoffs/L202/gates.sh final
rm -rf /tmp/l202-ob/head /tmp/l202-ob/DONE && mkdir -p /tmp/l202-ob/head && git archive HEAD | tar -x -C /tmp/l202-ob/head
git rev-parse HEAD > /tmp/l202-ob/head-sha.txt
$HOME/handoffs/L202/objbytes.sh
