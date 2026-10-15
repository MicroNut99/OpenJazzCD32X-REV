#!/usr/bin/env python3
"""make_boot_image.py - the boot splash for the CD32X loader (cd32x/cdloader_sh2.c).
    python3 tools/make_boot_image.py Splash.png build/IMAGE.RAW [preview.png]

IMAGE.RAW = 320x224 palette indices (the Sub-CPU's CMD 51 puts them into the frame buffer
from byte 0x200 on) followed by 256 palette words (big-endian, 32X format: bit 0-4 red,
5-9 green, 10-14 blue).  72,192 bytes.

The picture: the non-black part of the image (Splash.png: a 320x200 screen inside a larger
canvas) is cut out and, if it is 320x200 or smaller, placed 1:1 in the middle of the 320x224
screen (OpenJazz's own 320x200 picture with 12 black lines above and below, as the game is
shown); a larger picture is scaled down to fit.  Colours: 1..251 for the picture, 252 = black
(0x0400: the value 0x0000 and index 0 are see-through on the 32X, Kobo rule 5), index 0 is
unused and stays 0x0000 (a cleared screen shows colour 0: OpenLara lesson 7), 253/254/255 belong to the loader's
progress bar (empty / fill / frame)."""
import struct, sys
from PIL import Image

W, H = 320, 224

BLACK = 252   # the border lines and black parts of the picture (OpenLara's make_boot_image uses 252 too)

def to32x(r, g, b):
    v = (r >> 3) | (g >> 3) << 5 | (b >> 3) << 10
    return v or 0x0400  # Kobo rule 5 / Tyrian make_splash: colour 0x0000 is see-through on the 32X

def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    src = Image.open(sys.argv[1]).convert("RGB")
    box = src.point(lambda v: 255 if v > 8 else 0).getbbox()
    if box:
        src = src.crop(box)
    if src.width > W or src.height > H:
        s = min(W / src.width, H / src.height)
        src = src.resize((max(1, round(src.width * s)), max(1, round(src.height * s))), Image.LANCZOS)
    screen = Image.new("RGB", (W, H))
    screen.paste(src, ((W - src.width) // 2, (H - src.height) // 2))

    # J4b6: palette index 0 and the value 0x0000 are see-through on the 32X: with the Genesis
    # display on, the Genesis layer showed through the black border lines (glitches at the top
    # of the splash in the emulator). Index 0 is never used; black = index 252 = 0x0400.
    colours = screen.getcolors(1 << 16) or []
    if len(colours) <= 252:                       # exact: every colour keeps its value
        pal = [c for _, c in sorted(colours, key=lambda t: -t[0])]
        if (0, 0, 0) in pal:
            pal.remove((0, 0, 0))
        index = {c: i + 1 for i, c in enumerate(pal)}
        index[(0, 0, 0)] = BLACK
        rgb = screen.tobytes()
        idx = bytes(index[tuple(rgb[i:i + 3])] for i in range(0, len(rgb), 3))
    else:                                         # too many colours: 250 by median cut
        q = screen.quantize(colors=250, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
        flat = q.getpalette()[:250 * 3]
        pal = [tuple(flat[3 * i:3 * i + 3]) for i in range(250)]
        idx = bytes(i + 1 for i in q.tobytes())
    words = [0] * 256
    for i, c in enumerate(pal[:251]):
        words[i + 1] = to32x(*c)
    words[BLACK] = 0x0400
    words[253] = to32x(40, 40, 40)                # bar: empty
    words[254] = to32x(232, 16, 16)               # bar: fill (red)
    words[255] = to32x(200, 200, 200)             # bar: frame
    data = idx + b"".join(struct.pack(">H", w) for w in words)
    assert len(data) == W * H + 512
    open(sys.argv[2], "wb").write(data)
    if len(sys.argv) > 3:
        p = Image.new("RGB", (W, H))
        p.putdata([((v & 31) << 3, (v >> 5 & 31) << 3, (v >> 10 & 31) << 3) for v in (words[i] for i in idx)])
        p.save(sys.argv[3])
    print("%s: %d bytes (320x224 + palette, %d colours)" % (sys.argv[2], len(data), min(len(pal), 252) + 1))

if __name__ == "__main__":
    main()
