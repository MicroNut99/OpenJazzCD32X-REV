#!/bin/bash
# OpenJazz CD32X - bonus level packs (J18): for every BONUSMAP.nnn the PC build goes through the
# menus to "Z Bonus levels" with T32X_BONUS_MAP=nnn and plays it for a few seconds with the common
# pack mounted; what the bonus level needs that the common pack lacks becomes OJ<800+nnn>.PAK
# (the map's tiles and events decoded, the bonus graphics, sprites, pictures).
#   tools/bake_bonus.sh <data folder> <common pack> <output folder>
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
DATA="$1"; COMMON="$2"; OUT="$3"
MAX=$((0x170000))
TOUR="40:START,45:-,100:DOWN,103:-,110:DOWN,113:-,120:DOWN,123:-,130:DOWN,133:-,140:DOWN,143:-,150:DOWN,153:-,160:DOWN,163:-,170:DOWN,173:-,180:DOWN,183:-,190:DOWN,193:-,220:START,225:-,320:START,325:-"
make -C "$HERE/host" -j"${JOBS:-1}" >/dev/null
TMP=$(mktemp -d)
rm -f "$OUT"/OJ8[0-2]*.PAK
n=0; total=0; big=0
for f in $(ls "$DATA" | grep -i '^BONUSMAP\.[0-9][0-9][0-9]$' | sort); do
  m=$((10#${f:9:3})); k=$(printf '%03d' $((800 + m)))
  ( cd "$TMP" && T32X_SKIP_INTRO=1 T32X_BONUS_MAP=$m T32X_PACK="$COMMON" T32X_BAKE="$OUT/OJ$k.PAK" T32X_BAKE_SKIP='*.PSM' \
    T32X_SAVE_EVERY=0 T32X_FRAMES_DIR="$TMP" T32X_MAX_FRAMES=520 T32X_MAX_SECONDS=100000 T32X_PAD="$TOUR" \
    "$HERE/host/openjazz32x_host" "$DATA" > "$TMP/log.txt" 2>&1 ) || true
  [ -f "$OUT/OJ$k.PAK" ] || { echo "*** $f: no pack made"; tail -3 "$TMP/log.txt"; exit 1; }
  s=$(stat -c %s "$OUT/OJ$k.PAK"); total=$((total + s)); n=$((n + 1))
  [ $s -le $MAX ] || { echo "*** OJ$k.PAK: $s bytes, more than $MAX"; big=1; }
done
rm -rf "$TMP"
echo "    $n bonus packs, $((total / 1024)) KB in all"
[ $big = 0 ] || exit 1
