#!/bin/bash
# OpenJazz CD32X - heap per level (PC build): loads every LEVELn.www of the data folder with
# OpenJazz's -w/-l start (difficulty menu: Start at frame 100), plays 300 frames, prints the
# heap before the tiles, the tile set, the sprites, after loading, and the peak (bytes).
#   tools/level_mem.sh <Jazz 1 data folder> > level_mem.txt
HERE="$(cd "$(dirname "$0")/.." && pwd)"
DATA="${1:?usage: tools/level_mem.sh <Jazz 1 data folder>}"
make -C "$HERE/host" -j"${JOBS:-1}" >/dev/null || exit 1
TMP=$(mktemp -d); cd "$TMP"
printf "%-11s %8s %8s %8s %8s %8s\n" file before tiles sprites loaded peak
for f in $(ls "$DATA" | grep -i '^LEVEL[0-9]\.[0-9]*$' | sort); do
  l=${f:5:1}; w=$((10#${f:7:3}))
  out=$(T32X_MAX_FRAMES=400 T32X_MAX_SECONDS=1000 T32X_SAVE_EVERY=0 T32X_FRAMES_DIR="$TMP" \
        T32X_PAD="100:START,105:-" "$HERE/host/openjazz32x_host" -w $w -l $l "$DATA" 2>&1)
  m() { echo "$out" | grep "heap at 'level: $1'" | grep -o 'now [0-9]*' | cut -d' ' -f2; }
  b=$(m "before tiles"); t=$(m "tiles loaded"); s=$(m "sprites loaded"); d=$(m "loaded")
  p=$(echo "$out" | grep -o 'peak [0-9]*' | tail -1 | cut -d' ' -f2)
  if [ -z "$d" ]; then echo "$f  NOT LOADED"; continue; fi
  printf "%-11s %8d %8d %8d %8d %8d\n" "$f" "$b" $((t - b)) $((s - t)) "$d" "$p"
done
rm -rf "$TMP"
