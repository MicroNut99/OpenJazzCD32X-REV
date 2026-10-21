#!/usr/bin/env python3
"""make_bonus_md.py - the bonus levels' floor for the Sega CD graphics chip (R9).

    python3 tools/bonus_md/make_bonus_md.py <Jazz data folder> <out folder> [preview folder]

For every BONUSMAP.nnn: GB<800+nnn>.PAK (port/pack.h format, files only) with BONUSMD.BIN:
the floor's 32 x 32 tiles as stamps for the Mega CD's rotation / scaling chip, the colour
rotation's phases, and the 256 x 256 tile map. The chip draws 4 bits a pixel, so the floor is
two layers: plane A (palette line 0, colours 1-15) in front of plane B (line 1, colours 1-15);
where both are 0 the 32X's hole colour shows - 31 colours. The 32X draws everything else (sky,
horizon, sprites, panel); see docs/R9_BONUS_FLOOR.md.

BONUSMD.BIN (big-endian):
     0  'OJBM'
     4  u16 version (2)
     6  u16 stamps (1 .. n; stamp 0 is the chip's empty stamp)
     8  u16 hole: the 32X palette index the floor's area is filled with
    10  u16 phases of the colour rotation (NPHASE)
    12  u16 x 16 Genesis colours, palette line 0 (plane A; entry 0 not used)
    44  u16 x 16 Genesis colours, palette line 1 (plane B; entry 0 not used)
    76  u8 x 3 the hole's colour (8-bit RGB), u8 layers (1: plane A only, 16 colours, 30 pictures
        a second; 2: both, 31 colours - the chip draws twice: 20 a second)
    80  u16 x 60 each tile's plane A stamp (phase 0; 0 = nothing on A)
   200  u16 x 60 each tile's first plane A phase stamp (0: the tile does not change)
   320  u16 x 60 each tile's plane B stamp
   440  u16 x 60 each tile's first plane B phase stamp
   560  (zeros to 1024)
  1024  stamps 1 .. n, 512 bytes each (stamp k goes to Word RAM k * 512)
        then the map: 65536 bytes, tile number (0 - 59) at y * 256 + x
"""
import collections, math, os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from genesis_pack import write_pack

NPHASE = 4
ROT = (112, 192, 240)          # jj1bonuslevel.cpp: three 16-colour rotations, the same speed
MAX_STAMPS = 319               # Word RAM 0x00200 - 0x27FFF (docs/R9_BONUS_FLOOR.md)
NCOL = 31                      # the hole, 15 on plane A, 15 on plane B
ONE_LAYER_OK = 1.35             # one layer when its error is at most this times two layers' (+2)
ONE_LAYER_WORST = 250           # and no shade often seen is further off than this (8-bit RGB distance)


def rle(b, p, length):
    sz = b[p] | b[p + 1] << 8
    p += 2
    st = p
    out = bytearray(length)
    pos = 0
    while pos < length:
        c = b[p]
        p += 1
        a = c & 127
        if c & 128:
            v = b[p]
            p += 1
            if pos + a >= length:
                break
            out[pos:pos + a] = bytes([v]) * a
            pos += a
        elif a:
            if pos + a >= length:
                break
            out[pos:pos + a] = b[p:p + a]
            p += a
            pos += a
        else:
            out[pos] = b[p]
            p += 1
            pos += 1
            break
    return out, st + sz


def tileset(path):
    b = open(path, 'rb').read()
    bg, p = rle(b, 0, 832 * 20)
    pal, p = rle(b, p, 768)
    ts, p = rle(b, p, 1024 * 60)
    return bg, pal, ts


def bonusmap(path, start=False):
    """the tileset's name and the tile map (and the player's start tile, as jj1bonuslevel.cpp
    reads it: after the events and the sounds)"""
    b = open(path, 'rb').read()
    n = b[90]
    name = b[91:91 + n].decode() + '.000'
    p = 90 + 9 + 9 + 13 + 13 + (32 << 6) + 32 * 16
    tiles, p = rle(b, p, 256 * 256)
    grid = bytes(min(t, 59) for t in tiles)
    if not start:
        return name, grid
    for ln in (16 * 16, 256 * 256):
        _, p = rle(b, p, ln)
    p += 16 * 2 + 16 * 9 + 2 + 2 + 2
    return name, grid, struct.unpack_from('<HH', b, p)


def sprite_indices(path):
    """palette indices the bonus sprites (BONUS.000) may use (over-inclusive)"""
    b = open(path, 'rb').read()
    p = 2
    n = b[p] | b[p + 1] << 8
    p += 2
    used = set()
    for _ in range(n):
        w, h, pl, ml = struct.unpack_from('<HHHH', b, p)
        p += 8
        if pl != 0xFFFF:
            w <<= 2
            end = p + (pl << 2) + ml
            used.update(b[p + (w * h) // 4:end])
            p = end
        elif w:
            used.update(b[p:p + w * h])
            p += w * h
    return used


def md(rgb6):
    """6-bit RGB -> Genesis 3-bit levels"""
    return tuple(round(c * 7 / 63) for c in rgb6)


def cram(c3):
    return c3[2] << 9 | c3[1] << 5 | c3[0] << 1


def stamp_bytes(px):
    """32 x 32 nibbles (row-major) -> the chip's stamp: four 8-pixel-wide strips, each 32 rows"""
    out = bytearray(512)
    for x in range(32):
        for y in range(32):
            nib = (x >> 3) * 256 + y * 8 + (x & 7)
            v = px[y * 32 + x]
            if nib & 1:
                out[nib >> 1] |= v
            else:
                out[nib >> 1] |= v << 4
    return bytes(out)


def convert(data, mapfile, others):
    name, grid, (sx, sy) = bonusmap(os.path.join(data, mapfile), True)
    sx, sy = sx & 255, sy & 255
    bg, pal, ts = tileset(os.path.join(data, name))
    rgb6 = [tuple(pal[i * 3:i * 3 + 3]) for i in range(256)]
    used = collections.Counter(grid)
    # the colours the floor shows, weighted by how often they are on the screen: views from 300
    # places on the level's own tiles (not the filler around it, not the walls - the colours
    # 192-207, jj1bonuslevel.cpp's masks), at random angles, every 4th pixel
    import random
    rnd = random.Random(1)
    wall = [sum(1 for v in ts[t * 1024:(t + 1) * 1024] if v & 240 == 192) > 64 for t in range(60)]
    filler = used.most_common(1)[0][0]
    spots = [i for i, t in enumerate(grid) if t != filler and not wall[t]] or list(range(65536))
    seen = collections.Counter()
    near = [((sy + dy) & 255) * 256 + ((sx + dx) & 255) for dy in range(-8, 9) for dx in range(-8, 9)]
    near = [c for c in near if not wall[grid[c]]] or near
    for n in range(300):
        c = rnd.choice(near if n < 150 else spots)   # half of them around the start
        X = (c & 255) << 15 | rnd.randrange(32768)
        Y = (c >> 8) << 15 | rnd.randrange(32768)
        a = rnd.randrange(1024) * 2 * math.pi / 1024
        sn, cs = int(round(math.sin(a) * 1024)), int(round(math.cos(a) * 1024))
        for y in range(1, 86, 2):
            distance = ((800 << 10) << 10) // ((92 << 10) - ((y * 84) << 10) // 84)
            sideX, sideY = (distance * cs) >> 10, (distance * sn) >> 10
            fwdX = X + (((distance - 16384) * sn) >> 10) - (sideX >> 1)
            fwdY = Y - (((distance - 16384) * cs) >> 10) - (sideY >> 1)
            for x in range(0, 320, 4):
                nX = (x << 10) // 320
                lx = (fwdX + ((nX * sideX) >> 10)) >> 10
                ly = (fwdY + ((nX * sideY) >> 10)) >> 10
                seen[ts[(grid[((ly >> 5) & 255) * 256 + ((lx >> 5) & 255)] << 10) + (ly & 31) * 32 + (lx & 31)]] += 1
    floor_idx = set()
    for t in used:
        floor_idx.update(ts[t * 1024:(t + 1) * 1024])
    rot = set(i for f in ROT for i in range(f, f + 16))
    shows = set(floor_idx) | set(i for f in ROT if floor_idx & set(range(f, f + 16)) for i in range(f, f + 16))
    # how much of the screen each shade covers; a rotating range's shades pass every pixel of it,
    # so each of them counts with the range's whole area / 16
    area = {i: seen[i] + 0.5 for i in shows}
    for f in ROT:
        r = [i for i in range(f, f + 16) if i in shows]
        if r:
            tot = sum(area[i] for i in r)
            for i in r:
                area[i] = tot / len(r)
    # Jazz's palettes are ramps of 16 shades. Each ramp the floor uses is cut into runs of
    # neighbouring shades (by brightness), each run one Genesis colour (its area-weighted mean);
    # the 16 colours go one by one to the ramp where one more run removes the most error
    # (area x squared distance) from the screen
    ramps = collections.defaultdict(list)
    for i in shows:
        ramps[i >> 4].append(i)
    lum = lambda c: 2 * c[0] + 4 * c[1] + c[2]
    for r in ramps:
        ramps[r].sort(key=lambda i: lum(rgb6[i]))
    if len(ramps) > 16:
        raise SystemExit("*** %s: the floor uses %d ramps" % (mapfile, len(ramps)))

    def sse(grp):
        w = sum(area[i] for i in grp)
        m = [sum(rgb6[i][t] * area[i] for i in grp) / w for t in range(3)]
        # area x distance (not squared): a small step between two big flat shades counts - it is
        # the pattern the eye sees
        return sum(area[i] * math.sqrt(sum((rgb6[i][t] - m[t]) ** 2 for t in range(3))) for i in grp), tuple(m), w

    def runs(ix, k):
        """the best cut of ix (in order) into k runs: (error, [runs])"""
        n = len(ix)
        k = min(k, n)
        best = {(0, 0): (0.0, [])}
        for j in range(1, k + 1):
            for e in range(j, n + 1):
                cand = None
                for st in range(j - 1, e):
                    if (st, j - 1) not in best:
                        continue
                    err = best[(st, j - 1)][0] + sse(ix[st:e])[0]
                    if cand is None or err < cand[0]:
                        cand = (err, best[(st, j - 1)][1] + [ix[st:e]])
                if cand:
                    best[(e, j)] = cand
        return best[(n, k)]

    cache = {}
    def err(r, k):
        if (r, k) not in cache:
            cache[(r, k)] = runs(ramps[r], k)
        return cache[(r, k)][0]
    allc = [(r, g, b) for r in range(8) for g in range(8) for b in range(8)]
    d2 = lambda x, c: sum((a * 9 - b) ** 2 for a, b in zip(x, c))

    def build(budget):
        """budget runs shared out by gain, and their Genesis colours (the nearest)"""
        slots = {r: 1 for r in ramps}
        while sum(slots.values()) < budget:
            gain = {r: err(r, slots[r]) - err(r, slots[r] + 1) for r in ramps if slots[r] < len(ramps[r])}
            if not gain:
                break
            r = max(gain, key=gain.get)
            slots[r] += 1
        reps, rep_of = [], {}
        for r in sorted(ramps):
            for grp in runs(ramps[r], slots[r])[1]:
                _, c, w = sse(grp)
                for i in grp:
                    rep_of[i] = len(reps)
                reps.append((c, w, r))
        gen = [min(allc, key=lambda x: d2(x, c)) for c, _, _ in reps]
        return reps, rep_of, gen

    def choose(ncol):
        """ncol colours: (the shade -> colour number map, the Genesis colours, most seen first)"""
        # runs that come out as the same Genesis colour share it: more runs while there are no
        # more than ncol colours
        budget = min(ncol, sum(len(ix) for ix in ramps.values()))
        reps, rep_of, gen = build(budget)
        while budget < 80:
            r2 = build(budget + 1)
            if len(set(r2[2])) > ncol:
                break
            budget += 1
            reps, rep_of, gen = r2
        colours = []
        for g in gen:
            if g not in colours:
                colours.append(g)
        while len(colours) < ncol:
            colours.append((0, 0, 0))
        # the most seen becomes 0 (the hole's colour: the 32X shows it where the planes have 0)
        seen_of = collections.Counter()
        for k, g in enumerate(gen):
            seen_of[g] += reps[k][1]
        colours.sort(key=lambda g: -seen_of[g])
        return {i: colours.index(gen[k]) for i, k in rep_of.items()}, colours

    def screen_error(colour_of, colours):
        return sum(seen[i] * math.sqrt(sum((rgb6[i][t] * 255 / 63 - colours[colour_of[i]][t] * 255 / 7) ** 2 for t in range(3)))
                   for i in shows) / (sum(seen[i] for i in shows) or 1)

    def worst(colour_of, colours):
        """the largest error of a shade that is often on the screen"""
        return max([math.sqrt(sum((rgb6[i][t] * 255 / 63 - colours[colour_of[i]][t] * 255 / 7) ** 2 for t in range(3)))
                    for i in shows if seen[i] > 200] or [0])

    # one layer (16 colours, the chip draws once: 30 pictures a second) when that is nearly as
    # close to the original as two (31 colours, twice: 20 a second) and no shade that is often
    # on the screen is far off (a white stripe gone black, a pattern gone flat)
    one = choose(16)
    two = choose(NCOL)
    e1, e2 = screen_error(*one), screen_error(*two)
    layers = 1 if e1 <= e2 * ONE_LAYER_OK + 2 and worst(*one) <= ONE_LAYER_WORST else 2
    colour_of, gen = one if layers == 1 else two

    def q(i):
        return colour_of[i]

    def shown(i, phase):
        """the palette index whose colour pixel i shows at this phase of the rotation"""
        for f in ROT:
            if f <= i < f + 16:
                return f + ((i - f) + phase * 16 // NPHASE) % 16
        return i

    stamps, index = [], {}
    def add(px):
        if not any(px):
            return 0                     # the chip's empty stamp
        sb = stamp_bytes(px)
        if sb not in index:
            stamps.append(sb)
            index[sb] = len(stamps)      # 1-based
        return index[sb]

    layer = (lambda k: k if 1 <= k <= 15 else 0, lambda k: k - 15 if k >= 16 else 0)
    tile_stamp = [[0] * 60, [0] * 60]
    tile_anim = [[0] * 60, [0] * 60]
    for t in range(60):
        if t not in used:
            continue
        pix = ts[t * 1024:(t + 1) * 1024]
        anim = any(f <= i < f + 16 for i in pix for f in ROT)
        for L in (0, 1):
            tile_stamp[L][t] = add([layer[L](q(shown(i, 0))) for i in pix])
            if anim:
                ph = [[layer[L](q(shown(i, p))) for i in pix] for p in range(NPHASE)]
                if any(any(x) for x in ph):
                    first = len(stamps) + 1
                    for p in range(NPHASE):        # consecutive, not shared
                        stamps.append(stamp_bytes(ph[p]))
                    tile_anim[L][t] = first
    if len(stamps) > MAX_STAMPS:
        raise SystemExit("*** %s: %d stamps, more than %d" % (mapfile, len(stamps), MAX_STAMPS))
    # the hole: an index only the floor uses (the 32X never draws it in the bonus level)
    taken = others | rot | set(range(128, 172)) | set(range(0, 64)) | set(bg[r * 832 + x] for r in range(20) for x in range(512))
    free = [i for i in sorted(floor_idx) if i not in taken] or [i for i in range(64, 256) if i not in taken and i not in floor_idx]
    if not free:
        raise SystemExit("*** %s: no palette index left for the hole" % mapfile)
    hole = free[0]
    h8 = tuple(v * 36 for v in gen[0])
    head = b'OJBM' + struct.pack('>HHHH', 2, len(stamps), hole, NPHASE)
    head += b'\0\0' + b''.join(struct.pack('>H', cram(c)) for c in gen[1:16])
    head += b'\0\0' + b''.join(struct.pack('>H', cram(c)) for c in (gen + [(0, 0, 0)] * 15)[16:31])
    head += bytes(h8) + bytes([layers])
    for L in (0, 1):
        head += b''.join(struct.pack('>H', s) for s in tile_stamp[L])
        head += b''.join(struct.pack('>H', s) for s in tile_anim[L])
    assert len(head) == 560
    head += bytes(1024 - len(head))
    blob = head + b''.join(stamps) + grid
    info = dict(name=name, stamps=len(stamps), hole=hole, colours=len(shows),
                anim=sum(1 for a in tile_anim[0] + tile_anim[1] if a), pal16=gen, layers=layers, e1=e1, e2=e2)
    return blob, info, (rgb6, ts, grid, q, shown)


def convert_job(job):
    data, f, others = job
    blob, info, _ = convert(data, f, others)
    return blob, info


def main():
    import multiprocessing
    data, out = sys.argv[1], sys.argv[2]
    prev = sys.argv[3] if len(sys.argv) > 3 else None
    os.makedirs(out, exist_ok=True)
    others = sprite_indices(os.path.join(data, 'BONUS.000'))
    maps = sorted(x for x in os.listdir(data) if x.upper().startswith('BONUSMAP.') and x[9:].isdigit())
    with multiprocessing.Pool(int(os.environ.get('JOBS', '0')) or None) as pool:
        results = pool.map(convert_job, [(data, f, others) for f in maps])
    for f, (blob, info) in zip(maps, results):
        k = 800 + int(f[9:])
        size = write_pack(os.path.join(out, 'GB%03d.PAK' % k), [('BONUSMD.BIN', blob)])
        print("    GB%03d.PAK %6d bytes  %s: %d colours -> %d (%d layer%s; error %.1f / %.1f), %d stamps, hole %d"
              % (k, size, info['name'], info['colours'], 16 if info['layers'] == 1 else NCOL, info['layers'],
                 's' if info['layers'] > 1 else '', info['e1'], info['e2'], info['stamps'], info['hole']))
        if prev:
            _, info2, model = convert(data, f, others)
            preview(model, info2, os.path.join(prev, 'bonus%03d.png' % int(f[9:])), data, f)
    print("    bonus floors: %d (%d with two layers)" % (len(maps), sum(1 for _, i in results if i['layers'] == 2)))


def preview(model, info, path, data, mapfile):
    """the start view as OpenJazz draws it, and with the 16 Genesis colours"""
    from PIL import Image
    rgb6, ts, grid, q, shown = model
    b = open(os.path.join(data, mapfile), 'rb').read()
    W, H = 320, 200
    out = Image.new('RGB', (W, 200))
    # start position: after the event block and sound names (as jj1bonuslevel.cpp reads)
    p = 90 + 9 + 9 + 13 + 13 + (32 << 6) + 32 * 16
    for ln in (256 * 256, 16 * 16, 256 * 256):
        _, p = rle(b, p, ln)
    p += 16 * 2 + 16 * 9 + 2 + 2 + 2
    px, py = struct.unpack_from('<HH', b, p)
    X, Y = px << 15 | 16384, py << 15 | 16384
    for half, colours in ((0, None), (1, info['pal16'])):
        for y in range(1, H // 2 - 15 + 1):
            distance = ((800 << 10) << 10) // ((92 << 10) - ((y * 84) << 10) // (H // 2 - 16))
            sideX, sideY = distance, 0
            fwdX = X - (sideX >> 1)
            fwdY = Y - (distance - 16384)
            for x in range(W):
                nX = (x << 10) // W
                lx = (fwdX + ((nX * sideX) >> 10)) >> 10
                ly = (fwdY + ((nX * sideY) >> 10)) >> 10
                i = ts[(grid[((ly >> 5) & 255) * 256 + ((lx >> 5) & 255)] << 10) + (ly & 31) * 32 + (lx & 31)]
                c = tuple(v * 4 for v in rgb6[i]) if colours is None else tuple(v * 36 for v in colours[q(shown(i, 0))])
                out.putpixel((x, half * 100 + 100 - y), c)
    out.resize((W * 2, 400), Image.NEAREST).save(path)


if __name__ == '__main__':
    main()
