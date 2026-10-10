#!/usr/bin/env bash
# Starts the packaged Player on a software GPU (lavapipe) and renders 10 frames.
# Usage: smoke-test.sh <package dir>   (needs mesa-vulkan-drivers and xvfb)
set -euo pipefail
cd "$1"
icd=$(ls /usr/share/vulkan/icd.d/lvp_icd*.json | head -1)
export VK_DRIVER_FILES="$icd" VK_ICD_FILENAMES="$icd" KRONOS_SILENT_AUDIO=1 SDL_AUDIODRIVER=dummy
timeout 180 xvfb-run -a -s "-screen 0 1280x720x24" ./engine_runtime --self-test | tee smoke.log
grep -q -- "--self-test PASSED" smoke.log
rm smoke.log
