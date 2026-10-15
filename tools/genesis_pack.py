#!/usr/bin/env python3
"""genesis_pack.py - the Genesis plane's files for every world: GB<k>.PAK (REV).

    python3 tools/genesis_pack.py <dump folder> <out folder> [k offset]

<dump folder>: the level dumps of tools/dump_levels.sh (GB<world>_<level>.DMP). For each world
(k = world * 10 + k offset: 0 Jazz CD, 100 Holiday Hare '95 - the world packs' numbers,
port/pack.h) every level the Genesis can draw is converted (tools/genesis_convert.py) and the
results go into one pack, GB<k>.PAK: the pack format of port/pack.h with files only
(GENESIS<level>.BIN). The game loads it into the cart at 0x080000 after the world pack
(port/sdl_oj.cpp oj32x_level_pack) and mounts it beside it.

Levels the conversion leaves to the 32X (docs/GENESIS_PLANE.md):
  - palette effect 8 / 9 (parallax backgrounds made of palette rotation) and 11 (water
    darkening): their colours change with the camera - not done yet
  - more cycling colours than the 4 palettes hold
"""
import glob, multiprocessing, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import genesis_convert as gc

SKIP_TYPES = {gc.PE_2D, gc.PE_1D, gc.PE_WATER}
GB_MAX = 0x80000          # cart 0x080000 - 0x100000


def write_pack(path, files):
    """OJPK v2, files only: 'OJPK' 2 F 0 size 0, F x {name[16], offset, size}, data (4-aligned)"""
    head = 24 + 24 * len(files)
    off = (head + 3) & ~3
    entries, data = b'', b''
    for name, blob in files:
        entries += name.encode().ljust(16, b'\0') + struct.pack('>II', off + len(data), len(blob))
        data += blob
        while len(data) % 4:
            data += b'\0'
    total = off + len(data)
    out = b'OJPK' + struct.pack('>IIIII', 2, len(files), 0, total, 0) + entries
    out += bytes(off - len(out)) + data
    assert len(out) == total
    open(path, 'wb').write(out)
    return total


def convert_one(job):
    """one level: (OJGB file or None, what happened)"""
    w, lv, p = job
    d = gc.load_dump(p)
    if d['type'] in SKIP_TYPES:
        return None, "effect %d - 32X" % d['type']
    try:
        data, info = gc.convert(d)
    except SystemExit as e:
        return None, "%s - 32X" % e
    tmp = "%s.%d.tmp" % (p, os.getpid())
    gc.write(tmp, data)
    blob = open(tmp, 'rb').read()
    os.remove(tmp)
    return blob, "%d patterns%s" % (info['patterns'], " (%d merged)" % info['merged'] if info['merged'] else "")


def main():
    dumps, out = sys.argv[1], sys.argv[2]
    koff = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    os.makedirs(out, exist_ok=True)
    worlds = {}
    for p in sorted(glob.glob(os.path.join(dumps, 'GB*_*.DMP'))):
        b = os.path.basename(p)
        worlds.setdefault(int(b[2:5]), []).append((int(b[6:b.index('.')]), p))
    jobs = [(w, lv, p) for w, levels in sorted(worlds.items()) for lv, p in sorted(levels)]
    with multiprocessing.Pool(int(os.environ.get('JOBS', '0')) or None) as pool:
        results = dict(((w, lv), r) for (w, lv, _), r in zip(jobs, pool.map(convert_one, jobs)))
    total_levels = drawn = 0
    for w, levels in sorted(worlds.items()):
        files, notes = [], []
        for lv, p in sorted(levels):
            total_levels += 1
            blob, note = results[(w, lv)]
            notes.append("L%d: %s" % (lv, note))
            if blob:
                files.append(("GENESIS%d.BIN" % lv, blob))
                drawn += 1
        k = w * 10 + koff
        path = os.path.join(out, "GB%03d.PAK" % k)
        if files:
            size = write_pack(path, files)
            if size > GB_MAX:
                raise SystemExit("*** %s: %d bytes, more than the %d the cart has for it" % (path, size, GB_MAX))
            print("    GB%03d.PAK %7d bytes  %s" % (k, size, ", ".join(notes)))
        else:
            if os.path.exists(path):
                os.remove(path)
            print("    world %d: no level for the Genesis (%s)" % (w, ", ".join(notes)))
    print("    Genesis plane: %d of %d levels" % (drawn, total_levels))


if __name__ == '__main__':
    main()
