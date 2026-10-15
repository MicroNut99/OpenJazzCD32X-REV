#!/bin/bash
# OpenJazz CD32X - world packs (J12): one pack per world (planet) holds both of its levels, its
# tiles, sprites and planet screen: OJ<k>.PAK, k = world * 10 (port/pack.h). For every LEVELn.www
# the PC build plays the level for 300 frames with the common pack mounted and adds what the level
# needs that the common pack lacks to its world's pack (T32X_BAKE_MERGE). Music and demos stay out.
# Fails if a pack is bigger than the cart has room for (PACK_LEVEL_MAX).
#   tools/bake_levels.sh <data folder> <common pack> <output folder>
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
DATA="$1"; COMMON="$2"; OUT="$3"; KOFF="${4:-0}"   # J17: 100 for Holiday Hare 95 (OJ6xx)
MAX=$((0x170000))
make -C "$HERE/host" -j"${JOBS:-1}" >/dev/null
TMP=$(mktemp -d)
if [ "$KOFF" = 0 ]; then rm -f "$OUT"/OJ[0-5]*.PAK; else rm -f "$OUT"/OJ6*.PAK "$OUT"/OJ7*.PAK; fi
n=0
for f in $(ls "$DATA" | grep -i '^LEVEL[0-9]\.[0-9][0-9][0-9]$' | sort -t. -k2,2 -k1,1); do
  l=${f:5:1}; w=$((10#${f:7:3})); k=$(printf '%03d' $((w * 10 + KOFF)))
  if [ "$COMMON" = "-" ]; then PACKENV=""; else PACKENV="T32X_PACK=$COMMON"; fi   # J19: "-" = complete packs
  ( cd "$TMP" && env $PACKENV T32X_BAKE="$OUT/OJ$k.PAK" T32X_BAKE_MERGE=1 \
    T32X_BAKE_SKIP='*.PSM' \
    T32X_SAVE_EVERY=0 T32X_FRAMES_DIR="$TMP" T32X_MAX_FRAMES=400 T32X_MAX_SECONDS=100000 T32X_PAD="100:START,105:-" \
    "$HERE/host/openjazz32x_host" -w $w -l $l "$DATA" > "$TMP/log.txt" 2>&1 ) || true
  [ -f "$OUT/OJ$k.PAK" ] || { echo "*** $f: no pack made"; tail -3 "$TMP/log.txt"; exit 1; }
  n=$((n + 1))
done
rm -rf "$TMP"
big=0; total=0; packs=0
if [ "$KOFF" = 0 ]; then PACKS="$OUT/OJ[0-5]*.PAK"; else PACKS="$OUT/OJ[67]*.PAK"; fi
for p in $PACKS; do
  s=$(stat -c %s "$p"); total=$((total + s)); packs=$((packs + 1))
  if [ $s -gt $MAX ]; then echo "*** $(basename "$p"): $s bytes, more than $MAX"; big=1; fi
done
echo "    $n levels in $packs world packs, $((total / 1024)) KB in all"
[ $big = 0 ] || exit 1
