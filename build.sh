#!/bin/bash
# OpenJazz CD32X - full build (the game with all levels):
#   splash -> engine (mars/) -> ROM check (OpenLara's prep_openlara_cd.py) -> SH2 loader ->
#   Sub-CPU/68000 -> disc image, then the same disc checks OpenLara's build.sh makes.
# The loader (cd32x/sh2) and the Sub-CPU program (cd32x/subcpu) are OpenLara's, UNCHANGED;
# the engine goes on the disc under the name they open: OPENLARA.32X (file 20).
#   ./build.sh 2>&1 | tee build_log.txt
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
trap 'echo; echo "*** BUILD FAILED at line $LINENO: $BASH_COMMAND"; echo "*** look above for the first error (grep -n -B6 Error build_log.txt)"' ERR
MARKER="SH2-OPENLARA-CD-OL1-LOADER"
# REV: SEGA_TOOLCHAIN=<folder> - a toolchain somewhere else than /opt/toolchains/sega (sh-elf,
# m68k-elf, bin/zasm, bootblocks, ldscripts; genisoimage in <folder>/usr/bin if not installed)
if [ -n "${SEGA_TOOLCHAIN:-}" ]; then
  export ROOTDIR="$SEGA_TOOLCHAIN" TC="$SEGA_TOOLCHAIN/sh-elf/bin/sh-elf-"
  export PATH="$PATH:$SEGA_TOOLCHAIN/usr/bin"
fi
ROMS="$HERE/cd32x/subcpu/ROMS"
mkdir -p "$ROMS"
# R5: CART=8 / CART=16 - the paged cart's test builds (docs/CART_8MB_16MB.md): the common pack
# at cart 0x080000, the worlds kept in the cart's extra pages, Sub-CPU CMD 68 (window 1's page)
# and CMD 38's destination. Unset: the 4 MB REV build, unchanged.
# ED: CART=ED - the Mega EverDrive PRO as the 16 MB paged cart (CD32X BootCamp docs/12; the listener's
# MODE1-9 turns the page writes into its window registers; pages 1-6 - page 7 is the EverDrive's own).
#   CART=ED EDCD=1 bash build.sh      the disc + cd32x/subcpu/EDCD_JAZZ.32X, the launcher started from the
#                                     EverDrive menu with the disc in the Sega CD (BOOTCAMP=<folder>,
#                                     default ../CD32X-BootCamp)
#   EDCD_DIAG=1                       (with EDCD=1) the launcher with the Sub-CPU's green text on
# Saves still go to the cart's page 1 (the EverDrive wipes its PSRAM at its menu: nothing persists yet).
CART="${CART:-}"
case "$CART" in ""|8|16|ED) ;; *) echo "*** CART must be 8, 16 or ED (or unset) - stopping"; exit 1;; esac
[ -n "$CART" ] && echo "==> cart: the $CART MB test build"
[ "$CART" = ED ] && echo "==> cart: the Mega EverDrive PRO (16 MB, pages 1-6)"
# R11: LZ=1 - the world packs compressed in the cart (R8); default 0: as on the disc
LZ="${LZ:-0}"
[ -n "$CART" ] && [ "$LZ" = 1 ] && echo "==> the world packs compressed in the cart (LZ=1)"
# PROF=1: the frame profiler box in levels (ms per frame: logic, background, sprites, foreground,
# panel, present, other) - for measuring on the console
PROF="${PROF:-}"
[ -n "$PROF" ] && echo "==> the frame profiler box is on"

grep -q '^#define CDL_TARGET_OPENLARA 1' "$HERE/cd32x/sh2/cdloader_sh2.c" \
  || { echo "*** cd32x/sh2/cdloader_sh2.c is not in OpenLara mode - stopping"; exit 1; }

echo "==> splash";      python3 "$HERE/tools/make_boot_image.py" "$HERE/Splash.png" "$ROMS/IMAGE.RAW"
echo "==> data packs";  # the common pack (IMAGE8.RAW: menus, fonts, panel, demos, the file
                        # catalogue; cart 0x100000), one world pack per planet and one pack per
                        # bonus level (OJ<k>.PAK, cart 0x280000), made by the PC build - see
                        # port/pack.h, tools/bake_levels.sh, tools/bake_bonus.sh
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
# R5: the paged builds put the common pack at 0x080000; it must end before page 0's tail (0x1F0000)
if [ -n "$CART" ]; then [ "$SIZE" -le $((0x170000)) ] || { echo "*** IMAGE8.RAW is $SIZE bytes: more than the $((0x170000)) page 0 has for it - stopping"; exit 1; }; fi
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
# the 30 bonus levels, one pack each (OJ800-OJ829)
if [ "${LEVEL_PACKS:-}" = keep ] && ls "$ROMS"/OJ8*.PAK >/dev/null 2>&1 && [ -f "$ROMS/.bonus_v2" ]; then
  echo "    bonus packs: kept ($(ls "$ROMS"/OJ8*.PAK | wc -l))"
else
  "$HERE/tools/bake_bonus.sh" "$DATA" "$ROMS/IMAGE8.RAW" "$ROMS"
  touch "$ROMS/.bonus_v2"   # packs with the 64 KB map blocks (made again once)
fi
echo "==> sound effects"; python3 "$HERE/tools/make_oj_sfx.py" "$DATA/SOUNDS.000" "$ROMS/SFX00.BIN"
# Holiday Hare '95 as an episode of the Jazz CD menu (in place of "specific level"): its two
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
# REV: the Genesis plane (docs/GENESIS_PLANE.md) - every level dumped by the PC build, converted
# into Genesis tiles, palettes and maps, one GB<k>.PAK per world (loaded at cart 0x080000)
echo "==> Genesis plane"
rm -f "$ROMS"/GB*.PAK
GB_DUMPS="$(mktemp -d)"
"$HERE/tools/dump_levels.sh" "$DATA" "$GB_DUMPS/jazz" "$ROMS/IMAGE8.RAW"
python3 "$HERE/tools/genesis_pack.py" "$GB_DUMPS/jazz" "$ROMS" 0
if [ -f "$HH95/PANEL.000" ]; then
  T32X_HH95=1 "$HERE/tools/dump_levels.sh" "$HH95" "$GB_DUMPS/hh95" "$ROMS/IMAGE8.RAW"
  python3 "$HERE/tools/genesis_pack.py" "$GB_DUMPS/hh95" "$ROMS" 100
fi
rm -rf "$GB_DUMPS"
# R9: the bonus levels' floor for the Sega CD's graphics chip, GB800-GB829 (docs/R9_BONUS_FLOOR.md)
echo "==> bonus floors (tools/bonus_md/make_bonus_md.py)"
python3 "$HERE/tools/bonus_md/make_bonus_md.py" "$DATA" "$ROMS"
# R8: the 8 / 16 MB builds keep compressed copies of the world, bonus and Genesis plane packs
# in the cart (docs/R8_COMPRESSED_PACKS.md): OZ<k>.PAK / GZ<k>.PAK beside them on the disc
rm -f "$ROMS"/OZ*.PAK "$ROMS"/GZ*.PAK
if [ -n "$CART" ] && [ "$LZ" = 1 ]; then
  echo "==> compressed packs (tools/ojlz.c)"
  mkdir -p "$HERE/build"
  gcc -O2 -o "$HERE/build/ojlz" "$HERE/tools/ojlz.c"
  for f in "$ROMS"/OJ*.PAK "$ROMS"/GB*.PAK; do
    b=$(basename "$f"); z="$ROMS/$(echo "$b" | sed 's/^OJ/OZ/; s/^GB/GZ/')"
    "$HERE/build/ojlz" "$f" "$z"
  done
  echo "    $(ls "$ROMS"/OJ*.PAK "$ROMS"/GB*.PAK | xargs cat | wc -c) bytes -> $(ls "$ROMS"/OZ*.PAK "$ROMS"/GZ*.PAK | xargs cat | wc -c)"
fi
# music - WAVs in music/ (named like the game's music files, tools/music_tracks.txt)
MUSIC_WAVS=$(ls "$HERE"/music/*.wav "$HERE"/music/*.WAV 2>/dev/null | wc -l)
# NOMUSIC=1: a disc without the music (one silent audio track, the engine plays none) - small, for emulators
[ -n "${NOMUSIC:-}" ] && MUSIC_WAVS=0
if [ "$MUSIC_WAVS" -gt 0 ]; then MUSIC_DEFS="-DOJ32X_MUSIC=1"; else MUSIC_DEFS=""; fi
rm -f "$HERE"/mars/obj*/port/plat_mars.o "$HERE"/mars/obj*/port/plat_mars.d   # the switch lives there
# R5: the 4 MB and the CART builds keep their own objects: link the engine again every time
rm -f "$HERE/mars/openjazz32x.elf" "$HERE/mars/openjazz32x.bin" "$HERE/mars/OPENJAZZ.32X"
python3 "$HERE/tools/music_lengths.py" "$HERE/tools/music_tracks.txt" "$HERE/music" "$HERE/port/music_lengths.h"   # CD player
echo "==> engine";      make -C "$HERE/mars" EXTRA_DEFS="$MUSIC_DEFS" CART="$CART" PROF="$PROF" LZ="$LZ"
[ -f "$HERE/mars/OPENJAZZ.32X" ] || { echo "*** mars/OPENJAZZ.32X was not made - stopping"; exit 1; }
# REV: the engine sits in the cart from 0; the Genesis plane's files from 0x080000
ENGINE_SIZE=$(stat -c %s "$HERE/mars/OPENJAZZ.32X")
[ "$ENGINE_SIZE" -le $((0x80000)) ] || { echo "*** the engine is $ENGINE_SIZE bytes: it would reach the Genesis plane's files at cart 0x080000 - stopping"; exit 1; }
PREP_MODE1=""; if [ "${EDCD:-0}" = 1 ]; then PREP_MODE1="--mode1"; fi   # MODE1-1 (EDCD=1, below): the RAM jump table too
echo "==> ROM check";   python3 "$HERE/cd32x/ref/openlara/tools/prep_openlara_cd.py" $PREP_MODE1 "$HERE/mars/OPENJAZZ.32X" "$ROMS/OPENLARA.32X"
echo "==> SH2 loader";  cd "$HERE/cd32x/sh2"    && make -f Makefile.cdloader clean && make -f Makefile.cdloader
CART_DEFS="${CART:+-DCART_PAGED}"; [ "$CART" = ED ] && CART_DEFS="-DCART_PAGED -DCART_ED"
echo "==> Sub-CPU";     cd "$HERE/cd32x/subcpu" && make clean && make cd CART_DEFS="$CART_DEFS"

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

# EDCD=1 (MODE1-1, CD32X BootCamp docs/12): EDCD.32X, made by the BootCamp's edboot/build_edcd.sh around
# this disc's Sub-CPU program built a second time for the cartridge boot (MODE1=1 M1PAGE=1, in a copy
# of cd32x/subcpu: the disc keeps the normal program)
if [ "${EDCD:-0}" = 1 ]; then
  BC="${BOOTCAMP:-$HERE/../CD32X-BootCamp}"
  [ -f "$BC/edboot/build_edcd.sh" ] || { echo "*** EDCD=1 needs the CD32X BootCamp next to this folder (or BOOTCAMP=<folder>): not at $BC"; exit 1; }
  echo "==> EDCD_JAZZ.32X: the disc started from the EverDrive PRO (cartridge boot)"
  T=$(mktemp -d); mkdir -p "$T/subcpu/ROMS" "$T/sh2"
  find "$HERE/cd32x/subcpu" -maxdepth 1 -type f -exec cp {} "$T/subcpu/" \;
  cp "$HERE/cd32x/sh2/CDLOADER.32x" "$T/sh2/"
  ( cd "$T/subcpu" && make clean > /dev/null && make CDROMPlayer.bin MODE1=1 M1CTRL0=0x8000 M1PAGE=1 CART_DEFS="$CART_DEFS" ${EDCD_DIAG:+DIAG=$EDCD_DIAG} > "$T/mode1.log" 2>&1 ) \
    || { tail -20 "$T/mode1.log"; echo "*** the MODE1 Sub-CPU program did not build"; exit 1; }
  bash "$BC/edboot/build_edcd.sh" "$T/subcpu/CDROMPlayer.bin" "$HERE/cd32x/sh2/CDLOADER.32x" "$HERE/cd32x/subcpu/EDCD_JAZZ.32X"
  rm -rf "$T"
  echo "==> EDCD_JAZZ.32X: $HERE/cd32x/subcpu/EDCD_JAZZ.32X - copy it to the EverDrive's SD card and start it from the menu"
  echo "    with the disc above in the Sega CD (not \"Run Mega-CD\")"
fi
