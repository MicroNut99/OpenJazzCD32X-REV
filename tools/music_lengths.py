#!/usr/bin/env python3
"""music_lengths.py - the length of every song WAV in music/ (J13c), for the CD player in the
credits: it plays a track once and then goes on to the next one, so it needs to know when a
track ends (the CD commands report no "track finished").
    python3 tools/music_lengths.py tools/music_tracks.txt music port/music_lengths.h"""
import os, sys, wave
mapfile, src, out = sys.argv[1:4]
names = []
for line in open(mapfile):
    line = line.split("#")[0].split()
    if len(line) == 2:
        names.append((int(line[0]), line[1].rstrip(".")))
lengths = {}
for t, n in names:
    p = os.path.join(src, n + ".wav")
    if os.path.exists(p):
        w = wave.open(p)
        lengths[t] = int(w.getnframes() * 1000 / w.getframerate())
with open(out, "w") as f:
    f.write("/* made by tools/music_lengths.py - length of CD track n in ms (0 = unknown) */\n")
    f.write("static const unsigned int music_length_ms[100] = {\n")
    f.write(",".join(str(lengths.get(t, 0)) for t in range(100)))
    f.write("\n};\n")
print("  %s: %d track lengths" % (out, len(lengths)))
