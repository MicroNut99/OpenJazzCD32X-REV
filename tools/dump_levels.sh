#!/bin/bash
# REV: every level of a Jazz data folder dumped for the Genesis converter (T32X_DUMP_GB)
#   tools/dump_levels.sh <data folder> <out folder> [pack]
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
DATA="$1"; OUT="$2"; PACK="${3:-}"
mkdir -p "$OUT"
make -C "$HERE/host" -j"${JOBS:-2}" >/dev/null
TMP=$(mktemp -d)
for f in $(ls "$DATA" | grep -i '^LEVEL[0-9]\.[0-9][0-9][0-9]$'); do
  l=${f:5:1}; w=$((10#${f:7:3}))
  ( cd "$TMP" && env ${PACK:+T32X_PACK=$PACK} T32X_HH95="${T32X_HH95:-}" T32X_DUMP_GB="$OUT" T32X_SAVE_EVERY=0 T32X_FRAMES_DIR="$TMP" \
    T32X_MAX_FRAMES=110 T32X_MAX_SECONDS=100000 T32X_PAD="100:START,105:-" \
    "$HERE/host/openjazz32x_host" -w $w -l $l "$DATA" > /dev/null 2>&1 ) || true
done
rm -rf "$TMP"
