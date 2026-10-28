#!/usr/bin/env python3
"""make_oj_sfx.py - Jazz Jackrabbit's sound effects -> a Sega CD PCM pack
    python3 tools/make_oj_sfx.py <data folder>/SOUNDS.000 cd32x/subcpu/ROMS/SFX00.BIN
    python3 tools/make_oj_sfx.py --header port/oj32x_sfx_table.h <SOUNDS.000> [<HH95 SOUNDS.000>]

The pack format is OpenLara's / Kobo's KSFX (cd32x/subcpu/main.c pcm_load, CMD 53 with
COMM2 = 0 loads SFX00.BIN into the PCM chip's 64 KB; an effect plays when the SH2 puts its
number in the upper byte of the pad request, COMM0 = 48 | n << 8, n = engine index + 1).
    0x000 'KSFX' u16 count u16 image_bytes (0 = 65536)
    0x008 count x (u8 start_page, u8 volume, u16 fd, u16 loop_address, u16 length)
    0x100 PCM image (RF5C164: 8-bit sign-magnitude, 0xFF = loop marker)
    after the image: u8 map[256] - engine sample index -> pack effect (0 = not in the pack)
SOUNDS.000 (OpenJazz io/sound.cpp loadSounds): "sfx" + 0x1A, header offset in the last 4
bytes, 18-byte entries (name[12], offset u32, length u16), unsigned 8-bit samples at 11025 Hz.
OpenJazz's engine index = SE::Type = raw index + 1, so map[raw index] = effect.
All 22 sounds must fit the 64 KB: they are played at RATE and cut at MAX_SECS."""
import struct, sys

SRC_RATE, MAX_SECS, RAM, PAGE, TAIL = 11025, 1.0, 65536, 256, 16
# The samples in SOUNDS.000 are SIGNED 8-bit (OpenJazz io/sound.cpp converts them from
# AUDIO_S8). Up to R5 they were read as unsigned: every sample was off by half the range
# (silence, 0x00 / 0xFF, became full-scale + / - steps), which sounded distorted and far too
# loud - and was why VOLUME had been turned down to 96.
VOLUME = 160   # PCM envelope (255 = full)
AMP = 0.85     # samples at 85 % of full scale (headroom against clipping when effects mix)
# the playback rate: the highest that fits all effects (cut at MAX_SECS) in the 64 KB, at most
# the original 11025 Hz (worked out in main)

def sm8(v):
    m = min(126, int(round(abs(v) * 127)))       # 127 would give the 0xFF loop marker
    return (0x80 | m) if v >= 0 else m

def lowpass(a, src, dst):
    """remove what dst Hz cannot hold before resampling: a windowed-sinc filter (63 taps,
    Blackman) at 0.45 x dst - without it the high parts fold back as a metallic sound"""
    if dst >= src:
        return a
    import math
    fc = 0.45 * dst / src
    taps = 63
    h = []
    for k in range(taps):
        m = k - (taps - 1) / 2
        sinc = 2 * fc if m == 0 else math.sin(2 * math.pi * fc * m) / (math.pi * m)
        w = 0.42 - 0.5 * math.cos(2 * math.pi * k / (taps - 1)) + 0.08 * math.cos(4 * math.pi * k / (taps - 1))
        h.append(sinc * w)
    g = sum(h)
    h = [x / g for x in h]
    half = (taps - 1) // 2
    out = []
    for i in range(len(a)):
        acc = 0.0
        for k in range(taps):
            j = i + k - half
            if 0 <= j < len(a):
                acc += a[j] * h[k]
        out.append(acc)
    return out

def resample(a, src, dst):
    n = max(1, int(round(len(a) * dst / src)))
    out = []
    for i in range(n):
        x = i * (len(a) - 1) / max(1, n - 1)
        j = int(x); f = x - j
        out.append(a[j] * (1 - f) + a[min(j + 1, len(a) - 1)] * f)
    return out

def raw_names_lengths(path):
    d = open(path, "rb").read()
    if d[:3] != b"sfx" or d[3] != 0x1A:
        sys.exit("*** %s: not a Jazz Jackrabbit sound file" % path)
    head, = struct.unpack("<i", d[-4:])
    out = []
    for i in range((len(d) - head) // 18):
        e = d[head + i * 18: head + i * 18 + 18]
        ln, = struct.unpack("<H", e[16:18])
        out.append((e[:12].split(b"\0")[0].decode("latin1"), min(ln, int(MAX_SECS * SRC_RATE))))
    return out

def header(out_h, paths):
    """--header: the raw sounds' names and lengths for the engine (oj/src/io/sound.cpp, OJ-SFXRATE):
    a level names the sound of each of its 32 slots and the rate it plays at, and the engine finds the
    pack's effect by that name.  Lengths in samples as packed (cut at MAX_SECS), one row per game
    (Jazz CD, Holiday Hare '95; the second row repeats the first when there is no HH95)."""
    games = [raw_names_lengths(p) for p in paths]
    names = [n for n, _ in games[0]]
    for g in games[1:]:
        if [n for n, _ in g] != names:
            print("  *** the second SOUNDS.000 names its sounds differently - its lengths are not used")
            games = games[:1]
    while len(games) < 2:
        games.append(games[0])
    with open(out_h, "w") as f:
        f.write("/* made by tools/make_oj_sfx.py --header from SOUNDS.000 - not in the repository */\n")
        f.write("#define OJ32X_SFX_RAW %d\n" % len(names))
        f.write("static const char oj32x_sfx_name[OJ32X_SFX_RAW][13] = {\n")
        f.write(",\n".join('    "%s"' % n for n in names) + "\n};\n")
        f.write("static const unsigned short oj32x_sfx_len[2][OJ32X_SFX_RAW] = {\n")
        for g in games:
            f.write("    { " + ", ".join(str(l) for _, l in g) + " },\n")
        f.write("};\n")
    print("  %s: %d sound names for the levels' sound maps" % (out_h, len(names)))

def main():
    if len(sys.argv) >= 4 and sys.argv[1] == "--header":
        header(sys.argv[2], sys.argv[3:])
        return
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    d = open(sys.argv[1], "rb").read()
    if d[:3] != b"sfx" or d[3] != 0x1A:
        sys.exit("*** %s: not a Jazz Jackrabbit sound file" % sys.argv[1])
    head, = struct.unpack("<i", d[-4:])
    n = (len(d) - head) // 18
    entries = []
    for i in range(n):
        e = d[head + i * 18: head + i * 18 + 18]
        name = e[:12].split(b"\0")[0].decode("latin1")
        off, = struct.unpack("<i", e[12:16])
        ln, = struct.unpack("<H", e[16:18])
        entries.append((i, name, off, min(ln, int(MAX_SECS * SRC_RATE))))
    # the highest rate at which every effect fits (each starts on a 256-byte page and ends
    # with TAIL bytes of silence and the loop marker)
    def need(rate):
        used = 0
        for _, _, _, ln in entries:
            used = (used + PAGE - 1) // PAGE * PAGE + max(1, int(round(ln * rate / SRC_RATE))) + TAIL + 1
        return used
    RATE = SRC_RATE
    while need(RATE) > RAM and RATE > 2000:
        RATE -= 25
    print("  playback rate %d Hz (the original %d Hz)" % (RATE, SRC_RATE))
    image, table, smap = bytearray(), [], [0] * 256
    for i, name, off, ln in entries:
        # signed 8-bit samples (see VOLUME above)
        a = [AMP * ((b ^ 0x80) - 128) / 128.0 for b in d[off:off + ln]]
        fade = min(len(a), int(0.02 * SRC_RATE))
        for k in range(fade):
            a[len(a) - fade + k] *= 1 - (k + 1) / fade
        pcm = bytes(sm8(v) for v in resample(lowpass(a, SRC_RATE, RATE), SRC_RATE, RATE))
        start = (len(image) + PAGE - 1) // PAGE * PAGE
        if start + len(pcm) + TAIL + 1 > RAM or len(table) >= 31:
            print("  %-12s does not fit - left out" % name)
            continue
        image += bytes([0x80]) * (start - len(image)) + pcm
        loop = len(image)
        image += bytes([0x80]) * TAIL + b"\xff"
        table.append((start // PAGE, VOLUME, int(round(RATE * 2048 / 32552)), loop, len(pcm)))
        smap[i] = len(table)
    hdr = bytearray(b"KSFX") + struct.pack(">HH", len(table), len(image) & 0xFFFF)
    for t in table:
        hdr += struct.pack(">BBHHH", *t)
    hdr += bytes(256 - len(hdr))
    open(sys.argv[2], "wb").write(bytes(hdr) + bytes(image) + bytes(smap))
    print("  %s: %d of %d sound effects, %d bytes of 65536 PCM memory" % (sys.argv[2], len(table), n, len(image)))

if __name__ == "__main__":
    main()
