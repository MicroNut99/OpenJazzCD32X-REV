#!/usr/bin/env python3
"""genesis_convert.py - a Jazz level for the Genesis plane (REV).

The Genesis draws the level's background tiles on its plane A; the 32X draws only the sky, the
sprites, the foreground tiles and the panel (see docs/GENESIS_PLANE.md).

Input: the level dump the PC build writes with T32X_DUMP_GB=<folder> (GB<world>_<level>.DMP,
oj/src/jj1/level/jj1levelload.cpp). Output: one OJGB file per level, put into the world pack as
GENESIS<level>.BIN (tools/pack_add.py).

    python3 tools/genesis_convert.py <GBwww_l.DMP> <out.bin> [--png preview.png]

What it makes:
  - 4 palettes x 15 colours (9-bit, entry 0 transparent). Palette 3 entry 15 is black for the
    black cells (plane B). Colours of the palette-cycling ranges the level runs (jj1levelload.cpp
    RotatePaletteEffect) get palette entries of their own, which the 68000 rotates; when there
    are too many, neighbouring indices of the long rotations share one (coarser steps).
  - levels whose background is a parallax made of palette rotation (palette effect 8: an 8 x 8
    texture, 9: diagonal stripes; texture_mode()) get that texture on plane B, its colours in
    palette 3; plane A then holds the black cells too, and the tiles' see-through pixels show
    the plain background colour (127), as the 32X's fill did.
  - 8x8 patterns, 4 bits per pixel, stored once (mirrored copies merged). Pattern 0 is empty,
    pattern 1 solid colour 15 (the black cells). If there are more than the video memory holds,
    the most similar ones are merged.
  - per tile (32 x 32 pixels) 16 name-table words for plane A
  - the level's map (256 x 64 tile numbers, 255 = nothing on plane A: foreground tiles and tiles
    the conversion does not have) and its black cells (bit map, plane B)

OJGB v4, big-endian, offsets from the start of the file:
   0  'OJGB'          4  u16 version 4      6  u16 flags (bit 0: the level has a sky)
   8  u16 patterns (incl. 0 and 1)          10 u16 tiles
  12  u32 palettes: 64 words (palette p entry e at word p * 16 + e), Genesis 0BGR
  16  u32 patterns: 32 bytes each from pattern 0, in the order the 68000 fills video memory
  20  u32 tile table: 16 words per tile (rows of 4 cells), plane A name-table words
  24  u32 map: 64 rows x 256 bytes
  28  u32 black cells: 64 rows x 32 bytes, bit 7 of byte 0 = column 0
  32  u32 colour cycles: u16 groups; per group: u16 first, u16 amount, s32 step per vblank (16.16
      fixed, palette position; negative = the other way), u16 entries, entries x {u16 index - first,
      u16 CRAM byte address}, amount x u16 base colours (Genesis 0BGR); padded to 4
  36  u16 size of the whole file / 4 (a check for the 68000)
  38  u16 plane B: 0 black cells, 1 the 2D parallax texture, 2 the 1D diagonal stripes
  40  4 x u16 the texture's name-table words (1: one pattern; 2: cell (u, v) word (u + v) % 4)
"""
import struct, sys
import numpy as np

TKEY = 127
# Genesis video memory for patterns (cd32x/subcpu/hw_md.s, genbg_*): 4 areas between the plane
# tables, the listener's text table and the scroll / sprite tables
VRAM_AREAS = [(0x0000, 1280), (0xB000, 128), (0xD000, 96), (0xF400, 96)]
MAX_PATTERNS = sum(n for _, n in VRAM_AREAS)          # 1600
PE_SKY, PE_2D, PE_1D, PE_WATER = 2, 8, 9, 11
MS_PER_VBLANK = 1000.0 / 59.92
F16, F32 = 16384, 32768


def vram_index(pid):
    for base, n in VRAM_AREAS:
        if pid < n:
            return base // 32 + pid
        pid -= n
    raise ValueError("pattern number too large")


def cycles_for(typ):
    """the palette rotations jj1levelload.cpp runs for this effect type: (first, amount, speed)"""
    c = [(112, 4, F32), (116, 8, F16), (124, 3, F16)]
    if typ not in (PE_1D, PE_2D):
        c.append((132, 8, F16))
    if typ not in (PE_SKY, PE_2D):
        c.append((160, 32, -F16))
    if typ != PE_SKY:
        c += [(192, 32, -F32), (224, 16, F16)]
    return c


def load_dump(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'OJGD', path
    typ, sky, orb, nt = d[4], d[5], d[6], d[7] or 256
    o = 8
    pal = np.frombuffer(d[o:o + 768], np.uint8).reshape(256, 3).astype(np.float64); o += 768
    spal = np.frombuffer(d[o:o + 768], np.uint8).reshape(256, 3); o += 768
    tiles = np.frombuffer(d[o:o + nt * 1024], np.uint8).reshape(nt, 32, 32); o += nt * 1024
    grid = np.frombuffer(d[o:o + 64 * 256 * 2], np.uint8).reshape(64, 256, 2)
    return dict(type=typ, sky=sky, orb=orb, pal=pal, spal=spal, tiles=tiles, grid=grid)


def q9(rgb):
    """RGB 0-255 -> the nearest Genesis level 0-7 per channel"""
    return np.clip(np.round(np.asarray(rgb, np.float64) * 7.0 / 255.0), 0, 7).astype(np.int64)


def word9(lv):
    r, g, b = (int(x) for x in lv)
    return (b << 9) | (g << 5) | (r << 1)


def kmeans(points, weights, k, iters=12, seed=0):
    k = min(k, len(points))
    if k <= 0:
        return np.zeros((0, 3))
    rng = np.random.default_rng(seed)
    p = weights / weights.sum()
    cent = points[rng.choice(len(points), k, replace=False, p=p)].astype(np.float64)
    for _ in range(iters):
        lab = ((points[:, None, :] - cent[None]) ** 2).sum(-1).argmin(1)
        for j in range(k):
            m = lab == j
            if m.any():
                cent[j] = (points[m] * weights[m, None]).sum(0) / weights[m].sum()
    return cent


def convert(dump, budget=MAX_PATTERNS, all_tiles=True):
    """all_tiles: every tile of the set, so tiles the game puts in during play (destroyed blocks,
    jj1level.cpp setTile) are there too; convert() falls back to the tiles the map uses when
    that does not fit"""
    if all_tiles:
        data, info = convert(dump, budget, False)
        try:
            full, finfo = convert_tiles(dump, list(range(len(dump['tiles']))), budget, merge_ok=False)
            return full, finfo
        except NoRoom:
            return data, info
    tiles, grid = dump['tiles'], dump['grid']
    nt = len(tiles)
    on_plane = (grid[:, :, 1] & 2) == 0
    used = sorted(t for t in set(np.unique(grid[:, :, 0][on_plane]).tolist()) if t < nt)
    return convert_tiles(dump, used, budget, merge_ok=True)


class NoRoom(Exception):
    pass


def texture_mode(dump):
    """the level's parallax background made of palette rotation (jj1levelload.cpp,
    paletteeffects.cpp): mode 1 = P2DPaletteEffect (128-191: an 8 x 8 texture, a tile pixel's
    index is 128 + 8 * (y % 8) + x % 8), mode 2 = P1DPaletteEffect (128-159: diagonal stripes,
    index 128 + (x + y + 1) % 32). Those pixels become the texture on plane B, scrolled by the
    68000 as the effect turns the palette. Returns (mode, mask of those pixels in the tiles)."""
    t = dump['tiles']
    yy, xx = np.mgrid[0:32, 0:32]
    if dump['type'] == PE_2D:
        m = (t >= 128) & (t < 192) & (t == (128 + 8 * (yy % 8) + (xx % 8))[None])
        if m.any():
            return 1, m
    if dump['type'] == PE_1D:
        m = (t >= 128) & (t < 160) & (t == (128 + (xx + yy + 1) % 32)[None])
        if m.any():
            return 2, m
    return 0, np.zeros(t.shape, bool)


def convert_tiles(dump, used, budget, merge_ok):
    tiles0, grid, pal = dump['tiles'], dump['grid'], dump['pal']
    nt = len(tiles0)
    on_plane = (grid[:, :, 1] & 2) == 0
    mode, pattern = texture_mode(dump)

    # the colours: 9-bit levels per palette index; 256 = the plain background (127), 257 = black
    lv = np.vstack([q9(pal), q9(pal[127:128]), np.zeros((1, 3), np.int64)])
    T = tiles0.astype(np.int16)
    used = list(used)
    if mode:
        # with the texture on plane B, plane A has no see-through pixels but the texture's:
        # the tiles' own see-through pixels show the plain background (as the 32X's fill did),
        # and the black cells without a tile become a black tile (number nt)
        T = T.copy()
        T[T == TKEY] = 256
        T[pattern] = TKEY
        T = np.concatenate([T, np.full((1, 32, 32), 257, np.int16)])
        used.append(nt)

    cyc = [c for c in cycles_for(dump['type'])]
    cyc_of = {}                                           # palette index -> (group, position)
    for g, (first, amount, _) in enumerate(cyc):
        for k in range(amount):
            cyc_of[first + k] = (g, k)

    # cells: 16 per tile (rows of 4), 64 palette indices each
    tl = T[used].reshape(len(used), 4, 8, 4, 8).transpose(0, 1, 3, 2, 4).reshape(-1, 64)
    key = tl == TKEY
    is_cyc = np.isin(tl, list(cyc_of.keys()))
    stat = ~key & ~is_cyc

    entries = [[None] * 16 for _ in range(4)]             # entry 0 unused (transparent)
    tex_entry = {}
    if mode:
        rng = range(128, 192) if mode == 1 else range(128, 160)
        cols = []
        for i in rng:
            c = tuple(int(x) for x in lv[i])
            if c not in cols:
                cols.append(c)
        if len(cols) > 15:
            raise SystemExit("the parallax texture has %d colours" % len(cols))
        for e, c in enumerate(cols, 1):
            entries[3][e] = ('tex', c)
        for i in rng:
            tex_entry[i] = 1 + cols.index(tuple(int(x) for x in lv[i]))
    else:
        entries[3][15] = ('black',)

    # cycling entries: one per palette index the tiles use - or, when there are too many, one per
    # 2, 4, 8 neighbouring indices of the longest rotations (they then step coarser)
    cyc_used = sorted(set(np.unique(tl[is_cyc & ~key]).tolist()))
    groups_idx = {}
    for i in cyc_used:
        groups_idx.setdefault(cyc_of[i][0], []).append(i)
    quant = {g: 1 for g in groups_idx}
    cap = 28 if mode else 40

    def reps(g):
        first = cyc[g][0]
        return sorted(set(((i - first) // quant[g]) * quant[g] for i in groups_idx[g]))
    for g in groups_idx:
        while len(reps(g)) > 13:
            quant[g] *= 2
    while sum(len(reps(g)) for g in groups_idx) > cap:
        g = max(groups_idx, key=lambda g: len(reps(g)))
        if quant[g] >= cyc[g][1]:
            raise SystemExit("cycling colours do not fit")
        quant[g] *= 2
    cyc_entry, rep_entry = {}, {}
    for g in sorted(groups_idx, key=lambda g: -len(reps(g))):
        first = cyc[g][0]
        rs = reps(g)
        for p in (3, 2, 1, 0):
            free = [e for e in range(1, 16) if entries[p][e] is None]
            if len(free) >= len(rs) + (1 if p else 0):
                for r, e in zip(rs, free):
                    entries[p][e] = ('cyc', first + r)
                    rep_entry[first + r] = (p, e)
                break
        else:
            raise SystemExit("cycling colours do not fit")
        for i in groups_idx[g]:
            cyc_entry[i] = rep_entry[first + ((i - first) // quant[g]) * quant[g]]

    # static colours: palettes chosen by clustering the cells' mean colours, then each palette's
    # free entries by k-means over its cells' colours (weighted by pixel count), refined 3 times
    has_stat = stat.any(1)
    mean = np.zeros((len(tl), 3))
    for ci in np.nonzero(has_stat)[0]:
        mean[ci] = lv[tl[ci][stat[ci]]].mean(0)
    cell_cyc_pal = np.full(len(tl), -1)
    for ci in np.nonzero((is_cyc & ~key).any(1))[0]:
        ps = {cyc_entry[i][0] for i in set(tl[ci][is_cyc[ci] & ~key[ci]].tolist())}
        cell_cyc_pal[ci] = min(ps) if len(ps) == 1 else -2
    pals_free = [sum(1 for e in range(1, 16) if entries[p][e] is None) for p in range(4)]
    stat_pals = [p for p in range(4) if pals_free[p] > 0]
    if has_stat.any():
        cent = kmeans(mean[has_stat], np.ones(int(has_stat.sum())), len(stat_pals), seed=1)
        lab = ((mean[:, None] - cent[None]) ** 2).sum(-1).argmin(1)
        cell_pal = np.array([stat_pals[l] for l in lab])
    else:
        cell_pal = np.zeros(len(tl), int)
    fixed = cell_cyc_pal >= 0
    cell_pal[fixed] = cell_cyc_pal[fixed]
    for it in range(4):
        for p in stat_pals:
            free = [e for e in range(1, 16) if entries[p][e] is None or entries[p][e][0] == 'st']
            sel = (cell_pal == p) & has_stat
            if sel.any():
                px = lv[tl[sel][stat[sel]]]
            else:
                px = lv[tl[stat]] if stat.any() else np.zeros((1, 3))
            u, cnt = np.unique(px, axis=0, return_counts=True)
            cols = np.round(kmeans(u.astype(np.float64), cnt.astype(np.float64), len(free), seed=p + 7)).astype(np.int64)
            for e in free:
                entries[p][e] = None
            for e, c in zip(free, cols):
                entries[p][e] = ('st', tuple(int(x) for x in c))
        # each cell: the palette with its cycling colours and the smallest error
        stat_cols = {p: np.array([entries[p][e][1] for e in range(1, 16)
                                  if entries[p][e] and entries[p][e][0] in ('st', 'tex')] or [(0, 0, 0)])
                     for p in range(4)}
        for ci in range(len(tl)):
            if not has_stat[ci]:
                continue
            cands = [cell_cyc_pal[ci]] if cell_cyc_pal[ci] >= 0 else [p for p in range(4) if len(stat_cols[p])]
            sp = lv[tl[ci][stat[ci]]]
            best, be = cands[0], None
            for p in cands:
                e = ((sp[:, None] - stat_cols[p][None]) ** 2).sum(-1).min(1).sum()
                if be is None or e < be:
                    best, be = p, e
            cell_pal[ci] = best

    # patterns (the texture's colours in palette 3 serve the cells there as static ones too)
    pal_cols = {}
    for p in range(4):
        pal_cols[p] = [(e, entries[p][e][1]) for e in range(1, 16) if entries[p][e] and entries[p][e][0] in ('st', 'tex')]
    pats = {bytes(64): 0}
    solid = bytes([15] * 64)
    pats[solid] = 1
    names = []
    err = 0.0
    nstat = 0
    for ci in range(len(tl)):
        c = tl[ci]
        p = int(cell_pal[ci])
        out = np.zeros(64, np.uint8)
        sc = np.array([col for _, col in pal_cols[p]]) if pal_cols[p] else np.zeros((1, 3))
        se = [e for e, _ in pal_cols[p]] or [0]
        for j in range(64):
            v = int(c[j])
            if v == TKEY:
                continue
            if v in cyc_entry:
                cp, ce = cyc_entry[v]
                if cp == p:
                    out[j] = ce
                    continue
                # a cycling colour of another palette: its nearest static colour here
            d = ((sc - lv[v]) ** 2).sum(-1)
            k = int(d.argmin())
            out[j] = se[k]
            err += float(d[k]); nstat += 1
        q = out.reshape(8, 8)
        found = None
        for f, hf, vf in ((q, 0, 0), (q[:, ::-1], 1, 0), (q[::-1, :], 0, 1), (q[::-1, ::-1], 1, 1)):
            b = np.ascontiguousarray(f).tobytes()
            if b in pats:
                found = (pats[b], hf, vf)
                break
        if found is None:
            pats[q.tobytes()] = len(pats)
            found = (pats[q.tobytes()], 0, 0)
        names.append((found[0], p, found[1], found[2]))

    ntex = {0: 0, 1: 1, 2: 4}[mode]
    merged = 0
    if len(pats) + ntex > budget:
        if not merge_ok:
            raise NoRoom()
        pats, names, merged = merge(pats, names, entries, lv, budget - ntex)

    # the texture's patterns (plane B): 2D one 8 x 8 pattern; 1D four (a 32 x 32 block of
    # diagonal stripes: cell (u, v) is pattern (u + v) % 4)
    tex_words = [0, 0, 0, 0]
    if mode:
        yy, xx = np.mgrid[0:8, 0:8]
        for m in range(ntex):
            if mode == 1:
                idx = 128 + 8 * yy + xx
            else:
                idx = 128 + (8 * m + yy + xx) % 32
            q = np.vectorize(lambda i: tex_entry[int(i)])(idx).astype(np.uint8)
            b = q.tobytes()
            if b not in pats:
                pats[b] = len(pats)
            tex_words[m] = (3 << 13) | vram_index(pats[b])

    nte = nt + (1 if mode else 0)
    tile_words = np.zeros((nte, 16), np.uint16)
    tile_ok = np.zeros(nte, bool)
    for ui, t in enumerate(used):
        tile_ok[t] = True
        for k in range(16):
            pid, p, hf, vf = names[ui * 16 + k]
            tile_words[t, k] = 0 if pid == 0 else (p << 13) | (vf << 12) | (hf << 11) | vram_index(pid)

    mp = grid[:, :, 0].copy()
    mp[~on_plane] = 255
    mp[(mp >= nt) | ~tile_ok[np.minimum(mp, nt - 1)]] = 255
    blk = (grid[:, :, 1] & 1) != 0
    if mode:
        mp[blk & (mp == 255)] = nt                         # black cells: the black tile
        blk = np.zeros_like(blk)
    black = np.packbits(blk, axis=1)                       # 64 x 32 bytes

    info = dict(tiles=nt, used=len(used), patterns=len(pats), merged=merged,
                cycling=len(cyc_used), reps=len(rep_entry), mode=mode, rms=(err / max(nstat, 1)) ** 0.5)
    return dict(entries=entries, pats=pats, tile_words=tile_words, map=mp, black=black,
                cyc=cyc, rep_entry=rep_entry, lv=lv, sky=dump['sky'], mode=mode,
                tex_words=tex_words), info


def merge(pats, names, entries, lv, target):
    """replace the most similar patterns (same palette) by their nearest neighbour until they fit"""
    inv = {v: np.frombuffer(b, np.uint8) for b, v in pats.items()}
    pal_of = {}
    for pid, p, hf, vf in names:
        pal_of.setdefault(pid, p)

    def rgb(pid):
        p = pal_of.get(pid, 0)
        cols = np.zeros((16, 3))
        for e in range(1, 16):
            v = entries[p][e]
            cols[e] = v[1] if v and v[0] == 'st' else (3.5, 3.5, 3.5)
        cols[0] = (-30, -30, -30)
        return cols[inv[pid]].reshape(-1)

    alive = set(inv) - {0, 1}
    repl = {}
    while len(alive) + 2 > target:
        lst = sorted(alive)
        vec = np.array([rgb(i) for i in lst], np.float32)
        pl = np.array([pal_of.get(i, 0) for i in lst])
        cand = []
        for a0 in range(0, len(lst), 256):
            d = ((vec[a0:a0 + 256, None, :] - vec[None]) ** 2).sum(-1)
            d[pl[a0:a0 + 256, None] != pl[None, :]] = np.inf
            for r in range(d.shape[0]):
                d[r, a0 + r] = np.inf
            j = d.argmin(1)
            cand += [(float(d[r, j[r]]), lst[a0 + r], lst[j[r]]) for r in range(d.shape[0])]
        cand.sort()
        need = len(alive) + 2 - target
        touched = set()
        for dist, a, b in cand:
            if need <= 0 or dist == np.inf:
                break
            if a in touched or b in touched:
                continue
            repl[a] = b
            alive.discard(a)
            touched |= {a, b}
            need -= 1
        if not touched:
            raise SystemExit("patterns cannot be merged further")

    def final(pid):
        while pid in repl:
            pid = repl[pid]
        return pid
    keep = [0, 1] + sorted(alive)
    renum = {old: i for i, old in enumerate(keep)}
    new_pats = {inv[old].tobytes(): renum[old] for old in keep}
    new_names = [(renum[final(pid)], p, hf, vf) for pid, p, hf, vf in names]
    return new_pats, new_names, len(inv) - len(keep)


def write(path, d):
    entries = d['entries']
    pal_words = []
    for p in range(4):
        for e in range(16):
            v = entries[p][e]
            if v is None or e == 0 or v[0] == 'black':
                pal_words.append(0)
            elif v[0] in ('st', 'tex'):
                pal_words.append(word9(v[1]))
            else:                                         # cycling: its starting colour
                pal_words.append(word9(d['lv'][v[1]]))
    pat_bytes = bytearray()
    for b in sorted(d['pats'], key=d['pats'].get):
        q = np.frombuffer(b, np.uint8)
        pat_bytes += bytes(((int(q[i]) << 4) | int(q[i + 1])) for i in range(0, 64, 2))
    tmap = d['tile_words'].astype('>u2').tobytes()
    # colour cycles
    cyc = bytearray()
    groups = []
    for g, (first, amount, speed) in enumerate(d['cyc']):
        ents = sorted((i - first, (p * 16 + e) * 2) for i, (p, e) in d['rep_entry'].items()
                      if first <= i < first + amount)
        if ents:
            groups.append((first, amount, speed, ents))
    cyc += struct.pack('>H', len(groups))
    for first, amount, speed, ents in groups:
        # position -= mspf * speed >> 10 (10-bit fixed): per vblank in 16.16
        step = int(round(-MS_PER_VBLANK * speed / 1024.0 * 64.0))
        cyc += struct.pack('>HHiH', first, amount, step, len(ents))
        for k, a in ents:
            cyc += struct.pack('>HH', k, a)
        for k in range(amount):
            cyc += struct.pack('>H', word9(d['lv'][first + k]))
    while len(cyc) % 4:
        cyc += b'\0'
    off_pal = 48
    off_pat = off_pal + 128
    off_tmap = off_pat + len(pat_bytes)
    off_map = off_tmap + len(tmap)
    off_black = off_map + 64 * 256
    off_cyc = off_black + 64 * 32
    total = off_cyc + len(cyc)
    head = (b'OJGB' + struct.pack('>HH', 4, 1 if d['sky'] else 0) +
            struct.pack('>HH', len(d['pats']), len(d['tile_words'])) +
            struct.pack('>IIIIII', off_pal, off_pat, off_tmap, off_map, off_black, off_cyc) +
            struct.pack('>HH', total // 4, d['mode']) + struct.pack('>HHHH', *d['tex_words']))
    assert len(head) == 48
    out = head + b''.join(struct.pack('>H', w) for w in pal_words) + bytes(pat_bytes) + tmap + \
        d['map'].tobytes() + d['black'].tobytes() + bytes(cyc)
    assert len(out) == total
    open(path, 'wb').write(out)
    return total


if __name__ == '__main__':
    args = sys.argv[1:]
    png = None
    if '--png' in args:
        i = args.index('--png'); png = args[i + 1]; del args[i:i + 2]
    dump = load_dump(args[0])
    data, info = convert(dump)
    size = write(args[1], data)
    print("%s: mode %d tiles %d used %d patterns %d merged %d cycling %d (%d entries) rms %.2f size %d" %
          (args[0].rsplit('/', 1)[-1], info['mode'], info['tiles'], info['used'], info['patterns'], info['merged'],
           info['cycling'], info['reps'], info['rms'], size))
    if png:
        import genesis_preview
        genesis_preview.preview(args[1], dump, png)
