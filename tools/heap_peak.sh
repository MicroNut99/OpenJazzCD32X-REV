#!/bin/bash
# OpenJazz CD32X - who holds the heap at its peak (PC build).
#   tools/heap_peak.sh <Jazz 1 data folder> [pad script] [min block bytes, default 2048]
# HEAP_SNAP=<mark text> lists the live blocks at that memory mark instead (e.g. "level: loaded").
# Runs the game like run_host.sh with T32X_HEAP_LOG and names the allocating functions.
HERE="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${BIN:-$HERE/host/openjazz32x_host}"
make -C "$HERE/host" -j"${JOBS:-1}" >/dev/null || exit 1
mkdir -p "$HERE/frames" && cd "$HERE/frames"
T32X_HEAP_LOG="${3:-2048}" T32X_HEAP_SNAP="${HEAP_SNAP:-}" T32X_PACK="${PACK:-}" T32X_PACK_DIR="${PACK_DIR:-}" T32X_SAVE_EVERY=0 T32X_MAX_FRAMES="${T32X_MAX_FRAMES:-1800}" \
T32X_FRAMES_DIR="$HERE/frames" T32X_PAD="${2:-}" "$BIN" ${ARGS:-} "$1" 2>&1 >/dev/null | grep '^\[heap-peak\]' |
while read -r tag size rest; do
  if [[ "$size" =~ ^[0-9]+$ && "$rest" != *bytes* ]]; then
    # the first caller inside the game (skip operator new / allocators)
    who=$(addr2line -f -C -e "$BIN" $rest 2>/dev/null | paste - - | \
          grep -v -E 'operator new|__wrap_|big_add|malloc|calloc|SDL_CreateRGBSurface|new_surface|^\?\?' | head -4 | \
          awk -F'\t' '{split($2,a,"/"); printf "%s (%s)  ", $1, a[length(a)]}')
    printf "%8d  %s\n" "$size" "$who"
  else
    echo "$tag $size $rest"
  fi
done
