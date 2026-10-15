#!/usr/bin/env python3
"""tools/heap_group.py - sum tools/heap_peak.sh output by the game function that allocated."""
import re, sys, collections
s, n = collections.Counter(), collections.Counter()
lines = (open(sys.argv[1]) if len(sys.argv) > 1 else sys.stdin).read().splitlines()
for l in lines:
    if l.startswith("["):
        print(l); continue
    m = re.match(r"\s*(\d+)\s+(.*)", l)
    if not m: continue
    fns = [f.strip() for f in m.group(2).split("  ") if f.strip()]
    rest = [f for f in fns if not re.match(r"(Video::createSurface|File::loadSurface|Sprite::setPixels|SDL_)", f)]
    key = (rest or fns or ["?"])[0]
    key = re.sub(r"\(.*?\)\s*\(", "(", key)
    key = re.sub(r" \(discriminator \d+\)", "", key)
    s[key] += int(m.group(1)) + 8; n[key] += 1
for k, v in s.most_common():
    print("%8d %4d  %s" % (v, n[k], k))
