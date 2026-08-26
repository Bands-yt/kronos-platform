#!/usr/bin/env bash
# Regenerates marketing/kronos_opening.mp4 from build_opening.py.
# Requires: python3, rsvg-convert, ffmpeg.
set -euo pipefail
cd "$(dirname "$0")"

rm -rf frames
python3 build_opening.py ./frames all

ls frames/*.svg | xargs -P "$(nproc)" -I{} sh -c 'rsvg-convert -w 1920 -h 1080 "{}" -o "${0%.svg}.png"' {}

ffmpeg -y -framerate 30 -i frames/f_%04d.png \
  -c:v libx264 -pix_fmt yuv420p -crf 16 -preset slow \
  ../kronos_opening.mp4

echo "wrote ../kronos_opening.mp4"
