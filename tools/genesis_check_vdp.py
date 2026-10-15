#!/usr/bin/env python3
"""genesis_check_vdp.py - checks the 68000's Genesis plane (cd32x/subcpu/genbg_md.s) against the
level file: a VDP dump (VRAM 64 KB, CRAM 128 bytes, VSRAM 128 bytes, registers 32 bytes) is drawn
as the VDP draws planes A and B, and compared pixel by pixel (palette entry per pixel) with the
same screen drawn straight from the OJGB file for the camera. Used with the PicoDrive test ROM
(tools/genesis_test_rom.sh).

    python3 tools/genesis_check_vdp.py [--picodrive] <dump> <OJGB file> [<camera x> <camera y> | - -] [out.png]
(the camera defaults to the one the dump's scroll values stand for)
"""
import struct, sys
import numpy as np
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from genesis_preview import read_ojgb, cell_word


def swap16(b):
    a = np.frombuffer(b, np.uint8).reshape(-1, 2)[:, ::-1]
    return a.reshape(-1).tobytes()


def normalise(dump, swapped):
    """PicoDrive keeps VRAM, CRAM and VSRAM as little-endian words: back to the VDP's order"""
    if not swapped:
        return dump
    return swap16(dump[:0x10000]) + swap16(dump[0x10000:0x10080]) + swap16(dump[0x10080:0x10100]) + dump[0x10100:]


def camera_of(dump):
    """the camera the scroll values stand for: x = -h-scroll, y = v-scroll + 12"""
    regs = dump[0x10100:0x10120]
    hs_base = (regs[13] & 0x3F) << 10
    hsa = struct.unpack('>h', dump[hs_base:hs_base + 2])[0]
    vsa = struct.unpack('>h', dump[0x10080:0x10082])[0]
    return (-hsa) & 0xFFFF, (vsa + 12) & 0xFFFF


def vdp_planes(dump, lines=range(12, 179)):
    vram = np.frombuffer(dump[:0x10000], np.uint8)
    regs = dump[0x10100:0x10120]
    vsram = dump[0x10080:0x10100]
    a_base = (regs[2] & 0x38) << 10
    b_base = (regs[4] & 7) << 13
    hs_base = (regs[13] & 0x3F) << 10
    hsa = struct.unpack('>h', bytes(vram[hs_base:hs_base + 2]))[0]
    hsb = struct.unpack('>h', bytes(vram[hs_base + 2:hs_base + 4]))[0]
    vsa = struct.unpack('>h', vsram[0:2])[0]
    vsb = struct.unpack('>h', vsram[2:4])[0]
    out = np.zeros((224, 320), np.int64)          # palette * 16 + entry, 0 = backdrop
    def pix(base, word, px, py):
        if word == 0 and False:
            return 0
        pat = word & 0x7FF
        if word & 0x800: px = 7 - px
        if word & 0x1000: py = 7 - py
        b = vram[pat * 32 + py * 4 + px // 2]
        v = (b >> 4) if px % 2 == 0 else (b & 15)
        return 0 if v == 0 else ((word >> 13) & 3) * 16 + v
    for s in lines:
        for x in range(320):
            v = 0
            for base, hs, vs in ((a_base, hsa, vsa), (b_base, hsb, vsb)):
                px = (x - hs) & 511
                py = (s + vs) & 255
                w = int(vram[base + ((py >> 3) * 64 + (px >> 3)) * 2]) << 8 | int(vram[base + ((py >> 3) * 64 + (px >> 3)) * 2 + 1])
                v = pix(base, w, px & 7, py & 7)
                if v:
                    break
            out[s, x] = v
    return out


def expected(g, camx, camy, lines=range(12, 179)):
    out = np.zeros((224, 320), np.int64)
    pix = g['pix']
    for s in lines:
        wy = camy - 12 + s
        for x in range(320):
            wx = camx + x
            v = 0
            word = cell_word(g, wx >> 3, wy >> 3)
            if word:
                q = pix[g['vram'][word & 0x7FF]]
                px, py = wx & 7, wy & 7
                if word & 0x800: px = 7 - px
                if word & 0x1000: py = 7 - py
                e = q[py, px]
                if e: v = ((word >> 13) & 3) * 16 + e
            if not v and 0 <= wy < 2048 and 0 <= wx < 8192 and g['black'][wy >> 5, wx >> 5]:
                v = 3 * 16 + 15
            if not v and not (0 <= wy < 2048 and 0 <= wx < 8192):
                v = 3 * 16 + 15
            out[s, x] = v
    return out


if __name__ == '__main__':
    args = sys.argv[1:]
    swapped = '--picodrive' in args
    if swapped: args.remove('--picodrive')
    sys.argv = [sys.argv[0]] + args
    dump = normalise(open(sys.argv[1], 'rb').read(), swapped)
    g = read_ojgb(sys.argv[2])
    cx, cy = camera_of(dump)
    if len(sys.argv) > 4 and sys.argv[3] != '-':
        cx, cy = int(sys.argv[3]), int(sys.argv[4])
    a = vdp_planes(dump)
    b = expected(g, cx, cy)
    bad = np.argwhere(a != b)
    print("%s camera %d,%d: %d of %d pixels differ" % (sys.argv[1], cx, cy, len(bad), 320 * 167))
    if len(bad):
        print("first:", bad[:5].tolist(), [(int(a[y, x]), int(b[y, x])) for y, x in bad[:5]])
    if len(sys.argv) > 5:
        from PIL import Image
        pal = np.frombuffer(dump[0x10000:0x10080], '>u2').astype(np.int64)
        rgb = np.stack([((pal >> 1) & 7), ((pal >> 5) & 7), ((pal >> 9) & 7)], -1) * 36
        img = np.concatenate([rgb[a], np.zeros((224, 4, 3), np.int64), rgb[b]], 1).astype(np.uint8)
        Image.fromarray(img).save(sys.argv[5])
    sys.exit(1 if len(bad) else 0)
