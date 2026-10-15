#!/bin/bash
# REV: the 68000's Genesis plane code (cd32x/subcpu/genbg_md.s) in a plain Genesis test ROM, run
# in PicoDrive, every dump compared with the level file (tools/genesis_check_vdp.py).
#   tools/genesis_test_rom.sh <OJGB file> <work folder> <picodrive harness> <picodrive core>
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
GB="$1"; W="$2"; FE="$3"; CORE="$4"
TC=${TC:-/opt/toolchains/sega/m68k-elf/bin/m68k-elf-}
mkdir -p "$W" && cd "$W"
# COMM4 / COMM6 / the bank register are work RAM in the test; the cart window is the ROM
sed -e 's/0xA15124/0xFFF010/g; s/0xA15126/0xFFF012/g; s/0xA15104/0xFFF014/g; s/adda.l  #0x900000,a0/adda.l  #0x000000,a0/' \
  "$HERE/cd32x/subcpu/genbg_md.s" > genbg_test.s
cp "$HERE/tools/genesis_test_rom.s" t.s
${TC}as -m68000 --register-prefix-optional t.s -o t.o
${TC}ld -Ttext 0 -e 0 t.o -o t.elf
${TC}objcopy -O binary t.elf t.bin
python3 - "$GB" <<'PY'
import sys
d = bytearray(open('t.bin', 'rb').read())
assert len(d) <= 0x80000
d += bytes(0x80000 - len(d)) + open(sys.argv[1], 'rb').read()
while len(d) % 0x20000: d += b'\0'
open('rom.bin', 'wb').write(d)
PY
rm -f v*.bin f*.ppm
DUMPVDP=1 "$FE" "$CORE" rom.bin 400 20 "" > log.txt 2>&1 || true
bad=0
for f in v*.bin; do
  [ "$f" = v00000.bin ] && continue   # before the load
  python3 "$HERE/tools/genesis_check_vdp.py" --picodrive "$f" "$GB" - - "${f%.bin}.png" || bad=1
done
python3 - <<'PY'
import glob
worst = 0
for f in sorted(glob.glob('v*.bin')):
    r = open(f, 'rb').read()[0x10120:]
    w = lambda a: r[a ^ 1] << 8 | r[(a + 1) ^ 1]
    worst = max(worst, w(0xEECE) & 255)
print("most lines one vblank pass took (from line 224; a frame has 262):", worst)
PY
exit $bad
