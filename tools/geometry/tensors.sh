#!/usr/bin/env bash
# tensors.sh MODEL_DIR: one line per tensor (name, dtype, shape, bytes, file) read from the safetensors headers only.
set -euo pipefail
cd "$1"
for f in *.safetensors; do
  n=$(od -An -tu8 -N8 "$f" | tr -d ' ')
  dd if="$f" bs=1M iflag=count_bytes,skip_bytes skip=8 count="$n" 2>/dev/null |
    jq -r --arg f "$f" 'to_entries[] | select(.key != "__metadata__") |
      "\(.key)\t\(.value.dtype)\t\(.value.shape | map(tostring) | join("x"))\t\(.value.data_offsets[1] - .value.data_offsets[0])\t\($f)"'
done | sort
