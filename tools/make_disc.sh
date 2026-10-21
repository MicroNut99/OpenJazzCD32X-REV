#!/bin/bash
# make_disc.sh - builds the OpenJazz CD32X REV disc of this folder and puts it into
# discs/<NAME>/ inside this folder (default NAME: R1). Everything stays inside the folder.
#   cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/OpenJazzCD32X-REV && NAME=R1 bash tools/make_disc.sh
# Needs the game files in JazzJackRabbitGame/ (and JAZZXMAS1995/, music/*.wav if wanted; both are
# copied from ../OpenJazzCD32X when this folder lacks them).
# The world and bonus packs are kept when cd32x/subcpu/ROMS has them (LEVEL_PACKS=keep);
# if not, they are taken from ../OpenJazzCD32X/cd32x/subcpu/ROMS (the same packs) when there.
set -u
HERE="$(cd "$(dirname "$0")/.." && pwd)"
# CART=8 / CART=16 - the paged cart's test builds (docs/CART_8MB_16MB.md); their default
# name is R8-8MB / R8-16MB (R7: the frame rate work, docs/R7_FRAME_RATE.md; R8: compressed
# packs in the cart, docs/R8_COMPRESSED_PACKS.md)
# WSL sets NAME to the computer's name in every shell: that NAME is not a build name
[ "${NAME:-}" = "$(hostname)" ] && NAME=""
if [ -n "${CART:-}" ]; then NAME="${NAME:-R8-${CART}MB}"; export CART; fi
[ -n "${NOMUSIC:-}" ] && export NOMUSIC
# PROF=1: the frame profiler box in levels; the name gets -PROF
if [ -n "${PROF:-}" ]; then export PROF; NAME="${NAME:-R8${CART:+-${CART}MB}}-PROF"; fi
NAME="${NAME:-R1}"
DISCS="$HERE/discs/$NAME"

say()  { echo; echo "==> $*"; }
fail() { echo; echo "*** STOPPED: $*"; echo "*** Paste the lines above and this message."; exit 1; }

cd "$HERE" || fail "cannot enter $HERE"
say "Project folder: $HERE"
python3 -c "import numpy, PIL" 2>/dev/null || fail "Python's numpy and PIL are needed (sudo apt install python3-numpy python3-pil)"
[ -f JazzJackRabbitGame/PANEL.000 ] || fail "no JazzJackRabbitGame/ folder with the game files here"

ROMS=cd32x/subcpu/ROMS
mkdir -p "$ROMS"
if ! ls "$ROMS"/OJ0*.PAK >/dev/null 2>&1 && [ -d ../OpenJazzCD32X/cd32x/subcpu/ROMS ]; then
  say "World and bonus packs from ../OpenJazzCD32X"
  cp ../OpenJazzCD32X/cd32x/subcpu/ROMS/OJ*.PAK "$ROMS"/ || fail "copy of the packs failed"
  for f in .bonus_v2 .hh95_complete; do [ -f ../OpenJazzCD32X/cd32x/subcpu/ROMS/$f ] && cp ../OpenJazzCD32X/cd32x/subcpu/ROMS/$f "$ROMS"/; done
fi

# the music: the WAVs from ../OpenJazzCD32X/music when this folder does not have them all
# NOMUSIC=1: no music on the disc (build.sh), the WAVs are not needed
if [ -z "${NOMUSIC:-}" ] && [ -d ../OpenJazzCD32X/music ]; then
  mkdir -p music
  for f in ../OpenJazzCD32X/music/*.wav ../OpenJazzCD32X/music/*.WAV; do
    [ -f "$f" ] || continue
    b=$(basename "$f")
    if [ ! -f "music/$b" ] || [ "$(stat -c %s "music/$b")" != "$(stat -c %s "$f")" ]; then
      cp "$f" "music/$b" || fail "copy of $b failed"
    fi
  done
fi
[ -d JAZZXMAS1995 ] || { [ -d ../OpenJazzCD32X/JAZZXMAS1995 ] && cp -r ../OpenJazzCD32X/JAZZXMAS1995 .; }

say "Building (this takes a while; the full log goes to build_log.txt)"
LEVEL_PACKS=keep ./build.sh > build_log.txt 2>&1
STATUS=$?
tail -15 build_log.txt
[ $STATUS -eq 0 ] || fail "the build ended with an error (see build_log.txt)"
N=$(ls "$ROMS"/GB*.PAK 2>/dev/null | wc -l)
[ "$N" -ge 20 ] || fail "only $N Genesis plane packs (GB*.PAK) were made"
say "Genesis plane packs: $N"

BIN=cd32x/subcpu/OpenJazzCD32X.bin
CUE=cd32x/subcpu/OpenJazzCD32X.cue
[ -f "$BIN" ] && [ -f "$CUE" ] || fail "no .bin / .cue was made"
[ "$(find "$CUE" -mmin -240 | wc -l)" -eq 1 ] || fail "the .cue is old - the build did not make a new disc"

mkdir -p "$DISCS" || fail "cannot make $DISCS"
cp "$BIN" "$DISCS/OpenJazzCD32X_$NAME.bin" || fail "copy to $DISCS failed"
sed "s/OpenJazzCD32X\.bin/OpenJazzCD32X_$NAME.bin/" "$CUE" > "$DISCS/OpenJazzCD32X_$NAME.cue" || fail "the .cue could not be written"
grep -q "OpenJazzCD32X_$NAME.bin" "$DISCS/OpenJazzCD32X_$NAME.cue" || fail "the .cue does not name the .bin"
say "DONE. Burn this file:"
WIN=$(echo "$DISCS" | sed 's#^/mnt/\([a-z]\)/#\U\1:\\#; s#/#\\#g')
echo "    $WIN\\OpenJazzCD32X_$NAME.cue"
ls -la "$DISCS"
