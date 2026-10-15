#!/bin/bash
# OpenJazz CD32X - fill music/ with the game's own 35 songs as WAV (J10b): the .PSM files of
# the data folder played by OpenJazz's libxmp. J11d: each song = one complete pass,
# faded over its last 2 s; the console repeats the CD track.
#   tools/make_music_wavs.sh [data folder]        (then ./build.sh puts them on the disc)
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
DATA="${1:-$HERE/JazzJackRabbitGame}"
MAXS="${MAX_SECONDS:-600}"   # J11d: safety limit only - each song is one complete pass
make -C "$HERE/host" -j"${JOBS:-1}" >/dev/null      # the libxmp objects come from the PC build
gcc -O2 -no-pie -I"$HERE/oj/ext/xmp" -o "$HERE/host/psm2wav" "$HERE/tools/psm2wav.c" "$HERE"/host/obj/xmp/*.o "$HERE"/host/obj/xmp/loaders/*.o -lm
mkdir -p "$HERE/music"
for line in $(grep -v '^#' "$HERE/tools/music_tracks.txt" | awk '{print $2}'); do
  name="${line%.}"
  src=$(ls "$DATA" | grep -i "^$name\.psm$" | head -1)
  [ -n "$src" ] || { echo "  note: $name.PSM not in $DATA - skipped"; continue; }
  "$HERE/host/psm2wav" "$DATA/$src" "$HERE/music/$name.wav" "$MAXS" 1   # one pass + 2 s fade
done
# J17: Holiday Hare '95's songs (its menu, level-end and XM3 have the same names as Jazz CD's)
HH95="${HH95:-$HERE/JAZZXMAS1995}"
if [ -d "$HH95" ]; then
  for pair in MENUSNG:H95MENU ENDLEVEL:H95ENDLV XM3:H95XM3 XMAS2:XMAS2 XMAS3:XMAS3; do
    src=$(ls "$HH95" | grep -i "^${pair%%:*}\.psm$" | head -1)
    [ -n "$src" ] && "$HERE/host/psm2wav" "$HH95/$src" "$HERE/music/${pair##*:}.wav" "$MAXS" 1
  done
fi
echo "==> $(ls "$HERE"/music/*.wav | wc -l) songs in music/ ($(du -sm "$HERE/music" | cut -f1) MB)"
