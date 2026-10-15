#!/bin/bash
# OpenJazz CD32X - run the game on the PC with the 32X port layer (no SDL, no window).
#   ./run_host.sh <Jazz 1 data folder> [pad script]
# Frames go to frames/ as PNG (frames/last.png = the latest one).
# Pad script (frame:buttons, '+' combines, '-' = nothing; 't' = milliseconds):
#   ./run_host.sh ~/jazz1 "t5000:START,t5100:-,t6000:C,t6100:-"
# Buttons: UP DOWN LEFT RIGHT A B C START X Y Z MODE.
# Menus: C or START = select, B = back.  Playing: C jump, B fire, A weapon, START menu, Z stats.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
DATA="${1:?usage: ./run_host.sh <Jazz 1 data folder> [pad script]}"
make -C "$HERE/host" -j"${JOBS:-1}" >/dev/null
mkdir -p "$HERE/frames"
cd "$HERE/frames"   # OpenJazz writes openjazz.cfg into the working folder (PORTABLE build)
T32X_FRAMES_DIR="$HERE/frames" T32X_SAVE_EVERY="${T32X_SAVE_EVERY:-30}" \
T32X_MAX_FRAMES="${T32X_MAX_FRAMES:-1800}" T32X_PAD="${2:-}" \
"$HERE/host/openjazz32x_host" "$DATA"
