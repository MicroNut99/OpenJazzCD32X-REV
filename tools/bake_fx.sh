#!/bin/bash
# OpenJazz CD32X - OJ32X_FX (the EverDrive PRO build with the cart compositor, FX=1): one engine pack per
# world, the cart compositor's copies of the world's tiles and sprites (their colour key moved to 0),
# its sky and its flash tables (port/cfx_oj.h). They go on the disc as GB<k>.PAK, the file the Genesis
# plane's pack has in the other builds (FX draws the level on the 32X; the Genesis plane is off), so the
# cart takes each one into its world pack's page, right after it, as it takes a Genesis pack.
# For every LEVELn.www the PC build with the engine's model (host: SCROLL=1 FX=1 CART=16) plays the level
# for 400 frames with the common pack and the world's pack mounted, and adds what the engine needs and
# no pack has to its world's engine pack (T32X_BAKE_MERGE). A world whose pack and engine pack do not fit a
# page below the engine's window (0x1E0000 together) gets no engine pack: it is drawn as without the core.
#   tools/bake_fx.sh <data folder> <common pack> <folder with the OJ<k>.PAK> <BootCamp folder> [koff]
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
DATA="$1"; COMMON="$2"; ROMS="$3"; BC="$4"; KOFF="${5:-0}"   # koff 100: Holiday Hare 95 (OJ6xx)
MAX=$((0x1E0000))
make -C "$HERE/host" SCROLL=1 FX=1 CART=16 BOOTCAMP="$BC" -j"${JOBS:-1}" >/dev/null
BIN="$HERE/host/openjazz32x_host_scroll_fx_cart16"
TMP=$(mktemp -d); mkdir -p "$TMP/packs" "$TMP/out"
for p in "$ROMS"/OJ[0-7]*.PAK; do ln -s "$p" "$TMP/packs/"; done   # the world packs only (no Genesis or old engine packs)
n=0
for f in $(ls "$DATA" | grep -i '^LEVEL[0-9]\.[0-9][0-9][0-9]$' | sort -t. -k2,2 -k1,1); do
  l=${f:5:1}; w=$((10#${f:7:3})); k=$(printf '%03d' $((w * 10 + KOFF)))
  [ -f "$ROMS/OJ$k.PAK" ] || { echo "*** $f: no world pack OJ$k.PAK"; exit 1; }
  HHENV=""; [ "$KOFF" = 100 ] && HHENV="T32X_HH95=1"
  ( cd "$TMP" && env $HHENV T32X_PACK="$COMMON" T32X_PACK_DIR="$TMP/packs" T32X_BAKE="$TMP/out/GB$k.PAK" T32X_BAKE_MERGE=1 \
    T32X_BAKE_SKIP='*.PSM' T32X_SAVE_EVERY=0 T32X_FRAMES_DIR="$TMP" T32X_MAX_FRAMES=400 T32X_MAX_SECONDS=100000 \
    T32X_PAD="100:START,105:-" "$BIN" -w $w -l $l "$DATA" > "$TMP/log.txt" 2>&1 ) || true
  grep -q "FX: JAZZ'S CORE" "$TMP/log.txt" || { echo "*** $f: the engine did not start"; tail -5 "$TMP/log.txt"; exit 1; }
  [ -f "$TMP/out/GB$k.PAK" ] || { echo "*** $f: no engine pack made"; tail -3 "$TMP/log.txt"; exit 1; }
  n=$((n + 1))
done
total=0; packs=0; left=""
for p in "$TMP"/out/GB*.PAK; do
  b=$(basename "$p"); k=${b:2:3}
  s=$(stat -c %s "$p"); ws=$(stat -c %s "$ROMS/OJ$k.PAK")
  pair=$(( ((ws + 0x7FFF) & ~0x7FFF) + s ))
  if [ $pair -gt $MAX ]; then
    echo "    OJ$k.PAK + $b: $pair bytes, more than $MAX (a page below the engine's window): world $((10#$k / 10)) is drawn without the engine"
    left="$left $k"; continue
  fi
  total=$((total + s)); packs=$((packs + 1))
  cp "$p" "$ROMS/$b"
done
rm -rf "$TMP"
echo "    $n levels: $packs engine packs, $((total / 1024)) KB in all${left:+; none for$left}"
