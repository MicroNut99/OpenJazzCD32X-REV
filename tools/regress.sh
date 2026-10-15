#!/bin/bash
# OpenJazz CD32X - regression: run the scripted scenarios on the PC build and compare
# every 15th frame with the references in tests/<scenario>.ref (md5 of each saved frame).
#   tools/regress.sh <Jazz 1 data folder>            compare  -> "0 different" per scenario
#   tools/regress.sh <Jazz 1 data folder> --renew    write new references (after looking!)
# Frames of each run stay in /tmp/openjazz32x_regress/frames_<scenario>/ for a look
# (REGRESS_DIR=<folder> to put them elsewhere).
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
DATA="${1:?usage: tools/regress.sh <Jazz 1 data folder> [--renew]}"
RENEW="${2:-}"
make -C "$HERE/host" -j"${JOBS:-1}" >/dev/null

# name | max frames | pad script
SCENARIOS=(
  # no input: intro, title, main menu, demo 1 (episode 1 level 1) to the end, start of demo 2
  "attract|9200|"
  # New game -> episode 1 -> medium -> planet skipped -> level 1: walk, jump, fire, kill,
  # level menu with Start, closed with B, opened again, Continue with C, walk left
  "newgame|1380|600:START,605:-,700:START,705:-,800:START,805:-,900:START,905:-,980:RIGHT,1040:RIGHT+C,1055:RIGHT,1100:RIGHT+B,1104:RIGHT,1112:RIGHT+B,1116:RIGHT,1160:-,1180:START,1185:-,1220:B,1225:-,1240:START,1245:-,1270:C,1275:-,1290:LEFT,1350:-"
)

fail=0
for s in "${SCENARIOS[@]}"; do
  IFS='|' read -r name frames pad <<< "$s"
  # frames in Linux's own /tmp: writing ~130 MB of pictures to a Windows drive (/mnt/s) is slow
  out="${REGRESS_DIR:-/tmp/openjazz32x_regress}/frames_$name"
  rm -rf "$out" && mkdir -p "$out"
  ( cd "$out" && T32X_FRAMES_DIR="$out" T32X_SAVE_EVERY=15 T32X_MAX_FRAMES="$frames" \
    T32X_MAX_SECONDS=100000 T32X_PAD="$pad" "$HERE/host/openjazz32x_host" "$DATA" > log.txt 2>&1 ) || true
  ( cd "$out" && md5sum frame_0*.png ) > "$out/frames.md5"
  heap=$(grep -o 'peak [0-9]* bytes' "$out/log.txt" | head -1)
  n=$(wc -l < "$out/frames.md5")
  ref="$HERE/tests/$name.ref"
  if [ "$RENEW" = "--renew" ] || [ ! -f "$ref" ]; then
    cp "$out/frames.md5" "$ref"
    echo "$name: $n frames, reference written ($heap)"
  else
    d=$(diff <(sort -k2 "$ref") <(sort -k2 "$out/frames.md5") | grep '^>' | wc -l)
    m=$(wc -l < "$ref")
    if [ "$d" = 0 ] && [ "$n" = "$m" ]; then
      echo "$name: $n frames, 0 different ($heap)"
    else
      echo "$name: $n frames (reference $m), $d DIFFERENT - first: $(diff <(sort -k2 "$ref") <(sort -k2 "$out/frames.md5") | grep '^>' | head -3 | awk '{print $3}' | tr '\n' ' ')"
      fail=1
    fi
  fi
done
exit $fail
