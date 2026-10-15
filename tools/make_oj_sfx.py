#!/usr/bin/env python3
"""make_oj_sfx.py - Jazz Jackrabbit's sound effects -> a Sega CD PCM pack (J10)
    python3 tools/make_oj_sfx.py <data folder>/SOUNDS.000 cd32x/subcpu/ROMS/SFX00.BIN

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

SRC_RATE, RATE, MAX_SECS, RAM, PAGE, TAIL = 11025, 5000, 1.0, 65536, 256, 16
VOLUME = 96    # J13: PCM envelope (255 = full) - full volume was far too loud and distorted
AMP = 0.75     # J13: samples at 3/4 of full scale (headroom against clipping)

def sm8(v):
    m = min(126, int(round(abs(v) * 127)))       # 127 would give the 0xFF loop marker
    return (0x80 | m) if v >= 0 else m

def lowpass(a, src, dst):
    # J13f: remove what 5 kHz cannot hold before resampling (moving average over src/dst
    # samples, twice) - without it the high parts folded back as a metallic, "modulated" sound
    w = max(1, int(round(src / dst)))
    for _ in range(2):
        out, acc = [], 0.0
        for i, v in enumerate(a):
            acc += v
            if i >= w:
                acc -= a[i - w]
            out.append(acc / min(i + 1, w))
        a = out
    return a

def resample(a, src, dst):
    n = max(1, int(round(len(a) * dst / src)))
    out = []
    for i in range(n):
        x = i * (len(a) - 1) / max(1, n - 1)
        j = int(x); f = x - j
        out.append(a[j] * (1 - f) + a[min(j + 1, len(a) - 1)] * f)
    return out

def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    d = open(sys.argv[1], "rb").read()
    if d[:3] != b"sfx" or d[3] != 0x1A:
        sys.exit("*** %s: not a Jazz Jackrabbit sound file" % sys.argv[1])
    head, = struct.unpack("<i", d[-4:])
    n = (len(d) - head) // 18
    image, table, smap = bytearray(), [], [0] * 256
    for i in range(n):
        e = d[head + i * 18: head + i * 18 + 18]
        name = e[:12].split(b"\0")[0].decode("latin1")
        off, = struct.unpack("<i", e[12:16])
        ln, = struct.unpack("<H", e[16:18])
        a = [AMP * (b - 128) / 128.0 for b in d[off:off + ln]][: int(MAX_SECS * SRC_RATE)]
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
