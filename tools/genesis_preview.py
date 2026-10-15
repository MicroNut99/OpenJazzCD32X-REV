#!/usr/bin/env python3
"""genesis_preview.py - what the Genesis plane shows, from an OJGB file (tools/genesis_convert.py),
read the way the 68000 reads it (cd32x/subcpu/hw_md.s genbg_*), beside the original level.

    python3 tools/genesis_preview.py <GENESIS.BIN> <GBwww_l.DMP> <out.png> [x y w h]  (pixels)
"""
import struct, sys
import numpy as np

VRAM_AREAS = [(0x0000, 1280), (0xB000, 128), (0xD000, 96), (0xF400, 96)]


def read_ojgb(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'OJGB'
    ver, flags, npat, ntiles = struct.unpack('>HHHH', d[4:12])
    offs = struct.unpack('>IIIIII', d[12:36])
    pal = np.frombuffer(d[offs[0]:offs[0] + 128], '>u2').astype(np.int64)
    pats = np.frombuffer(d[offs[1]:offs[1] + npat * 32], np.uint8).reshape(npat, 8, 4)
    pix = np.zeros((npat, 8, 8), np.uint8)
    pix[:, :, 0::2] = pats >> 4
    pix[:, :, 1::2] = pats & 15
    vram = {}
    pid = 0
    for base, n in VRAM_AREAS:
        for k in range(n):
            if pid < npat:
                vram[base // 32 + k] = pid
            pid += 1
    tmap = np.frombuffer(d[offs[2]:offs[2] + ntiles * 32], '>u2').reshape(ntiles, 16)
    mp = np.frombuffer(d[offs[3]:offs[3] + 64 * 256], np.uint8).reshape(64, 256)
    black = np.unpackbits(np.frombuffer(d[offs[4]:offs[4] + 64 * 32], np.uint8).reshape(64, 32), axis=1)
    mode = struct.unpack('>H', d[38:40])[0] if ver >= 4 else 0
    tex = struct.unpack('>HHHH', d[40:48]) if ver >= 4 else (0, 0, 0, 0)
    return dict(pal=pal, pix=pix, vram=vram, tmap=tmap, map=mp, black=black, flags=flags, raw=d, offs=offs,
                mode=mode, tex=tex)


def plane_b_scroll(g, camx, camy):
    """plane B's h / v scroll for the camera, as gb_scroll in cd32x/subcpu/genbg_md.s"""
    if g['mode'] == 1:
        return -(camx + ((8192 - camx) >> 3)), camy - 12 + ((2048 - camy) >> 3)
    if g['mode'] == 2:
        return ((camx + camy) >> 1) - camx, camy - 12
    return -camx, camy - 12


def texture_pixel(g, camx, camy, sx, s):
    """plane B's texture at screen pixel (sx, line s): palette * 16 + entry"""
    hb, vb = plane_b_scroll(g, camx, camy)
    px, py = (sx - hb) & 511, (s + vb) & 255
    word = g['tex'][0] if g['mode'] == 1 else g['tex'][((px >> 3) + (py >> 3)) & 3]
    q = g['pix'][g['vram'][word & 0x7FF]]
    e = q[py & 7, px & 7]
    return ((word >> 13) & 3) * 16 + e if e else 0


def rgb_of_word(w):
    return np.stack([((w >> 1) & 7), ((w >> 5) & 7), ((w >> 9) & 7)], -1) * 255 // 7


def cell_word(g, cx, cy):
    """the plane A name-table word for world cell (cx, cy), as gb_cell in hw_md.s"""
    if not (0 <= cx < 1024 and 0 <= cy < 256):
        return 0
    t = int(g['map'][cy >> 2, cx >> 2])
    if t == 255 or t >= len(g['tmap']):
        return 0
    return int(g['tmap'][t, (cy & 3) * 4 + (cx & 3)])


def render(g, x0, y0, w, h, sky_rgb=None):
    """plane B (black cells) under plane A for the world rectangle; transparent = sky_rgb or None"""
    out = np.zeros((h, w, 3), np.uint8)
    alpha = np.zeros((h, w), bool)
    prgb = rgb_of_word(g['pal'])
    for cy in range(y0 >> 3, (y0 + h + 7) >> 3):
        for cx in range(x0 >> 3, (x0 + w + 7) >> 3):
            cell = np.zeros((8, 8, 3), np.uint8)
            a = np.zeros((8, 8), bool)
            if 0 <= cy < 256 and 0 <= cx < 1024 and g['black'][cy >> 2, cx >> 2]:
                a[:] = True                       # plane B: the solid black pattern
            word = cell_word(g, cx, cy)
            if word:
                pid = g['vram'][word & 0x7FF]
                p = (word >> 13) & 3
                q = g['pix'][pid]
                if word & 0x800: q = q[:, ::-1]
                if word & 0x1000: q = q[::-1, :]
                m = q != 0
                cell[m] = prgb[p * 16 + q[m]]
                a |= m
            ys, xs = cy * 8 - y0, cx * 8 - x0
            for yy in range(8):
                for xx in range(8):
                    Y, X = ys + yy, xs + xx
                    if 0 <= Y < h and 0 <= X < w:
                        out[Y, X] = cell[yy, xx]
                        alpha[Y, X] = a[yy, xx]
    if sky_rgb is not None:
        out[~alpha] = sky_rgb
    return out, alpha


def original(dump, x0, y0, w, h):
    tiles, grid, pal = dump['tiles'], dump['grid'], dump['pal'].astype(np.uint8)
    out = np.zeros((h, w, 3), np.uint8)
    out[:] = (40, 40, 120)
    for ty in range(y0 // 32, (y0 + h + 31) // 32):
        for tx in range(x0 // 32, (x0 + w + 31) // 32):
            if not (0 <= ty < 64 and 0 <= tx < 256):
                continue
            t, f = grid[ty, tx]
            blk = np.zeros((32, 32, 3), np.uint8); blk[:] = (40, 40, 120)
            if f & 1: blk[:] = 0
            if t < len(tiles):
                m = tiles[t] != 127
                blk[m] = pal[tiles[t][m]]
            for yy in range(32):
                Y = ty * 32 + yy - y0
                if 0 <= Y < h:
                    for xx in range(32):
                        X = tx * 32 + xx - x0
                        if 0 <= X < w: out[Y, X] = blk[yy, xx]
    return out


def preview(gb_path, dump, png, x0=0, y0=0, w=640, h=400):
    from PIL import Image
    g = read_ojgb(gb_path)
    a = original(dump, x0, y0, w, h)
    b, _ = render(g, x0, y0, w, h, sky_rgb=(40, 40, 120))
    img = np.concatenate([a, np.zeros((h, 8, 3), np.uint8), b], 1)
    Image.fromarray(img).save(png)


if __name__ == '__main__':
    sys.path.insert(0, __file__.rsplit('/', 1)[0])
    import genesis_convert
    dump = genesis_convert.load_dump(sys.argv[2])
    nums = [int(v) for v in sys.argv[4:8]] or [0, 0, 640, 400]
    preview(sys.argv[1], dump, sys.argv[3], *nums)
