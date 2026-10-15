#!/bin/bash
# OpenJazz CD32X - full build (J5: the game with all levels):
#   splash -> engine (mars/) -> ROM check (OpenLara's prep_openlara_cd.py) -> SH2 loader ->
#   Sub-CPU/68000 -> disc image, then the same disc checks OpenLara's build.sh makes.
# The loader (cd32x/sh2) and the Sub-CPU program (cd32x/subcpu) are OpenLara's, UNCHANGED;
# the engine goes on the disc under the name they open: OPENLARA.32X (file 20).
#   ./build.sh 2>&1 | tee build_log.txt
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
trap 'echo; echo "*** BUILD FAILED at line $LINENO: $BASH_COMMAND"; echo "*** look above for the first error (grep -n -B6 Error build_log.txt)"' ERR
MARKER="SH2-OPENLARA-CD-OL1-LOADER"
ROMS="$HERE/cd32x/subcpu/ROMS"
mkdir -p "$ROMS"

grep -q '^#define CDL_TARGET_OPENLARA 1' "$HERE/cd32x/sh2/cdloader_sh2.c" \
  || { echo "*** cd32x/sh2/cdloader_sh2.c is not in OpenLara mode - stopping"; exit 1; }

echo "==> splash";      python3 "$HERE/tools/make_boot_image.py" "$HERE/Splash.png" "$ROMS/IMAGE.RAW"
echo "==> data packs";  # J5: the common pack (IMAGE8.RAW: menus, fonts, panel, all planet
                        # screens; cart 0x100000) and one pack per level file (OJ<k>.PAK, cart
                        # 0x280000), made by the PC build - see port/pack.h, tools/bake_levels.sh
DATA="${DATA:-$HERE/JazzJackRabbitGame}"
[ -f "$DATA/PANEL.000" ] || { echo "*** no Jazz data in $DATA (set DATA=<folder>) - stopping"; exit 1; }
make -C "$HERE/host" -j"${JOBS:-1}" >/dev/null
BAKE_TMP="$(mktemp -d)"
# the common pack: a tour of every menu without the intro (the 32X skips it): Instructions,
# Setup, Order info, New game -> episode 1 -> difficulty -> planet screen (not the level)
TOUR="40:DOWN,43:-,60:DOWN,63:-,80:START,83:-,180:B,183:-,240:DOWN,243:-,260:START,263:-,360:B,363:-,420:DOWN,423:-,440:START,443:-,540:B,543:-,600:UP,603:-,620:UP,623:-,640:UP,643:-,660:UP,663:-,680:START,683:-,780:START,783:-"
rm -f "$ROMS/IMAGE8.RAW"
( cd "$BAKE_TMP" && T32X_SKIP_INTRO=1 T32X_BAKE="$ROMS/IMAGE8.RAW" T32X_BAKE_CATALOG="$DATA" \
  T32X_BAKE_SKIP='*.PSM' T32X_SAVE_EVERY=0 T32X_FRAMES_DIR="$BAKE_TMP" \
  T32X_MAX_FRAMES=960 T32X_MAX_SECONDS=100000 T32X_PAD="$TOUR" "$HERE/host/openjazz32x_host" "$DATA" 2>&1 | grep "\[bake\]" )
SIZE=$(stat -c %s "$ROMS/IMAGE8.RAW")
[ "$SIZE" -le $((0x180000)) ] || { echo "*** IMAGE8.RAW is $SIZE bytes: more than the 1.5 MB before the level pack area - stopping"; exit 1; }
# check: the same tour from the pack alone (no data folder) gives the same pictures
( cd "$BAKE_TMP" && mkdir a b && \
  T32X_SKIP_INTRO=1 T32X_SAVE_EVERY=20 T32X_FRAMES_DIR="$BAKE_TMP/a" T32X_MAX_FRAMES=940 T32X_MAX_SECONDS=100000 \
  T32X_PAD="$TOUR" "$HERE/host/openjazz32x_host" "$DATA" > /dev/null 2>&1; \
  T32X_SKIP_INTRO=1 T32X_PACK="$ROMS/IMAGE8.RAW" T32X_SAVE_EVERY=20 T32X_FRAMES_DIR="$BAKE_TMP/b" T32X_MAX_FRAMES=940 \
  T32X_MAX_SECONDS=100000 T32X_PAD="$TOUR" "$HERE/host/openjazz32x_host" /nonexistent > "$BAKE_TMP/check.txt" 2>&1 ) || true
grep -q "reached 940 frames" "$BAKE_TMP/check.txt" || { echo "*** the pack alone does not get through the menus on the PC:"; tail -5 "$BAKE_TMP/check.txt"; exit 1; }
NDIFF=0; for f in "$BAKE_TMP"/a/frame_0*.png; do cmp -s "$f" "$BAKE_TMP/b/$(basename "$f")" || NDIFF=$((NDIFF + 1)); done
[ "$NDIFF" = 0 ] || { echo "*** from the pack alone, $NDIFF frames of the menu tour differ on the PC - stopping"; exit 1; }
echo "    common pack: $SIZE bytes, menu tour checked from the pack alone"
rm -rf "$BAKE_TMP"
# the level packs (several minutes; LEVEL_PACKS=keep reuses the ones already there)
if [ "${LEVEL_PACKS:-}" = keep ] && ls "$ROMS"/OJ*.PAK >/dev/null 2>&1; then
  echo "    level packs: kept ($(ls "$ROMS"/OJ*.PAK | wc -l))"
else
  rm -f "$ROMS"/OJ[0-5]*.PAK
  "$HERE/tools/bake_levels.sh" "$DATA" "$ROMS/IMAGE8.RAW" "$ROMS"
fi
# J18: the 30 bonus levels, one pack each (OJ800-OJ829)
if [ "${LEVEL_PACKS:-}" = keep ] && ls "$ROMS"/OJ8*.PAK >/dev/null 2>&1 && [ -f "$ROMS/.bonus_v2" ]; then
  echo "    bonus packs: kept ($(ls "$ROMS"/OJ8*.PAK | wc -l))"
else
  "$HERE/tools/bake_bonus.sh" "$DATA" "$ROMS/IMAGE8.RAW" "$ROMS"
  touch "$ROMS/.bonus_v2"   # J19b: packs with the 64 KB map blocks (made again once)
fi
echo "==> sound effects"; python3 "$HERE/tools/make_oj_sfx.py" "$DATA/SOUNDS.000" "$ROMS/SFX00.BIN"
# J19: Holiday Hare '95 as an episode of the Jazz CD menu (in place of "specific level"): its two
# world packs are baked against Jazz CD's common pack - of the files both use only MAINCHAR.000
# differs (Jazz in his Christmas outfit), so that one is read from JAZZXMAS1995/ and goes into its
# packs (T32X_BAKE_UNPACKED); a level pack is searched before the common pack
HH95="${HH95:-$HERE/JAZZXMAS1995}"
rm -f "$ROMS/IMAGE9.RAW"
if [ -f "$HH95/PANEL.000" ]; then
  echo "==> Holiday Hare '95"
  if [ "${LEVEL_PACKS:-}" = keep ] && ls "$ROMS"/OJ6*.PAK >/dev/null 2>&1 && [ -f "$ROMS/.hh95_complete" ]; then
    echo "    world packs: kept ($(ls "$ROMS"/OJ6*.PAK | wc -l))"
  else
    T32X_HH95=1 T32X_BAKE_UNPACKED=MAINCHAR.000 "$HERE/tools/bake_levels.sh" "$HH95" "$ROMS/IMAGE8.RAW" "$ROMS" 100
    touch "$ROMS/.hh95_complete"
  fi
  python3 "$HERE/tools/make_oj_sfx.py" "$HH95/SOUNDS.000" "$ROMS/SFX01.BIN"
else
  echo "==> no JAZZXMAS1995/ folder: no Holiday Hare '95"
  rm -f "$ROMS"/OJ6*.PAK "$ROMS/SFX01.BIN" "$ROMS/.hh95_complete"
fi
# J10: music - WAVs in music/ (named like the game's music files, tools/music_tracks.txt)
MUSIC_WAVS=$(ls "$HERE"/music/*.wav "$HERE"/music/*.WAV 2>/dev/null | wc -l)
if [ "$MUSIC_WAVS" -gt 0 ]; then MUSIC_DEFS="-DOJ32X_MUSIC=1"; else MUSIC_DEFS=""; fi
rm -f "$HERE/mars/obj/port/plat_mars.o" "$HERE/mars/obj/port/plat_mars.d"   # the switch lives there
python3 "$HERE/tools/music_lengths.py" "$HERE/tools/music_tracks.txt" "$HERE/music" "$HERE/port/music_lengths.h"   # J13c: CD player
echo "==> engine";      make -C "$HERE/mars" EXTRA_DEFS="$MUSIC_DEFS"
[ -f "$HERE/mars/OPENJAZZ.32X" ] || { echo "*** mars/OPENJAZZ.32X was not made - stopping"; exit 1; }
echo "==> ROM check";   python3 "$HERE/cd32x/ref/openlara/tools/prep_openlara_cd.py" "$HERE/mars/OPENJAZZ.32X" "$ROMS/OPENLARA.32X"
echo "==> SH2 loader";  cd "$HERE/cd32x/sh2"    && make -f Makefile.cdloader clean && make -f Makefile.cdloader
echo "==> Sub-CPU";     cd "$HERE/cd32x/subcpu" && make clean && make cd

ISO="$HERE/cd32x/subcpu/CDROMPlayer.iso"
[ -f "$ISO" ] || { echo "*** $ISO was not made - stopping"; exit 1; }
grep -a -q "$MARKER" "$ISO" || { echo "*** the disc does NOT contain the loader - stopping"; exit 1; }
for f in APP.BIN CDLOADER.BIN OPENLARA.32X IMAGE.RAW IMAGE8.RAW OJ000.PAK; do
  [ -f "$HERE/cd32x/subcpu/cd/$f" ] || { echo "*** $f is not on the disc - stopping"; exit 1; }
done
NAME="$(dd if="$HERE/cd32x/subcpu/cd/OPENLARA.32X" bs=1 skip=$((0x3C0)) count=8 2>/dev/null)"
[ "$NAME" = "OpenJazz" ] || { echo "*** cd/OPENLARA.32X is not the OpenJazz engine ($NAME) - stopping"; exit 1; }
cmp -s "$HERE/cd32x/subcpu/cd/OPENLARA.32X" "$ROMS/OPENLARA.32X" || { echo "*** the disc has an old engine - stopping"; exit 1; }
echo "==> disc checked: loader + OpenJazz engine + splash"

# a mixed-mode image (data + one 2 s silent audio track) for emulators that want a .cue
python3 - "$HERE/cd32x/subcpu/silence.wav" <<'PY'
import sys, wave
w = wave.open(sys.argv[1], "wb"); w.setnchannels(2); w.setsampwidth(2); w.setframerate(44100)
w.writeframes(b"\0" * 44100 * 4 * 2); w.close()
PY
if [ "$MUSIC_WAVS" -gt 0 ]; then
  echo "==> music: $MUSIC_WAVS WAVs -> CD audio tracks"
  rm -rf "$HERE/cd32x/subcpu/soundtrack_cd"
  python3 "$HERE/tools/make_music_tracks.py" "$HERE/tools/music_tracks.txt" "$HERE/music" "$HERE/cd32x/subcpu/soundtrack_cd"
  cd "$HERE/cd32x/subcpu" && python3 make_mixed_cd2.py CDROMPlayer.iso soundtrack_cd/Track*.wav --out OpenJazzCD32X
else
  echo "==> no music/*.wav: one silent audio track (the engine plays no music)"
  cd "$HERE/cd32x/subcpu" && python3 make_mixed_cd2.py CDROMPlayer.iso silence.wav --out OpenJazzCD32X
fi
echo
echo "==> done:"
echo "    emulator: cd32x/subcpu/OpenJazzCD32X.cue (with its .bin)"
echo "    burn:     cd32x/subcpu/OpenJazzCD32X.cue, or the plain cd32x/subcpu/CDROMPlayer.iso"
