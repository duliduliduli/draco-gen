#!/usr/bin/env bash
# Generates a demo set of patches with draco_render: presets (.draco), a short
# rendered riff per patch (.wav) and a manifest with loudness numbers.
#
#   tools/demo/generate_demo.sh <path/to/draco_render> <out-dir> [count] [seed]
set -euo pipefail

RENDER="${1:?usage: generate_demo.sh <draco_render> <out-dir> [count] [seed]}"
OUT="${2:?usage: generate_demo.sh <draco_render> <out-dir> [count] [seed]}"
COUNT="${3:-10}"
SEED="${4:-2026}"

mkdir -p "$OUT"
"$RENDER" --generate --count "$COUNT" --seed "$SEED" --out-dir "$OUT" --wav | tee "$OUT/summary.txt"

if command -v ffmpeg >/dev/null 2>&1; then
  for wav in "$OUT"/*.wav; do
    ffmpeg -loglevel error -y -i "$wav" -codec:a libmp3lame -q:a 4 "${wav%.wav}.mp3"
  done
fi
