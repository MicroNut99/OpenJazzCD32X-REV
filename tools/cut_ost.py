#!/usr/bin/env python3
"""cut_ost.py - one long soundtrack video -> the game's CD audio tracks (J11b)

    python3 tools/cut_ost.py videoplayback.mp4 music/        [--max 110] [--check]

The video holds the 37 tracks of "Jazz Jackrabbit Original Soundtrack" (Noxxa) one after the
other, in the playlist's order with the playlist's lengths (TRACKS below). The cut points are
the sums of those lengths; because the lengths are rounded to whole seconds, each cut is moved
to the quietest moment within +-4 s of where it should be (ffmpeg's silencedetect), so the
error does not add up over 37 tracks.

Each track is saved as music/<GAME FILE>.wav (44.1 kHz, 16-bit, stereo) under the name the
game uses (tools/music_tracks.txt), cut to at most --max seconds with a 3 s fade-out: the
playlist versions repeat each song several times (3:45 - 13:13), all of them together would be
about 3.5 hours, and a CD holds 80 minutes - minus the game data. The console repeats a track
when it ends. Default 110 s: 37 tracks fit with the data.
--check only prints the cut points and the total length, without writing anything."""
import argparse, os, re, subprocess, sys

# playlist position: (length "m:ss", game music file or None, title)
TRACKS = [
    ("7:25", "MENUSNG", "Main Menu"),
    ("0:08", "ENDLEVEL", "Level Clear"),
    ("5:33", "BOSS", "Guardian"),
    ("4:39", "ENDSONG", "Ending"),
    ("5:58", "BONUS", "Bonus"),
    ("7:02", "SONG0", "Diamondus"),
    ("7:34", "SONG3", "Tubelectric"),
    ("8:20", "SONG17", "Medivo"),
    ("5:37", "SONG7", "Letni"),
    ("6:51", "SONG2", "Technoir"),
    ("7:56", "SONG11", "Orbitus"),
    ("4:03", "SONG8", "Fanolint"),
    ("7:16", "SONG14", "Scraparap"),
    ("4:20", "SONG16", "Megairbase"),
    ("7:01", "SONG4", "Turtemple"),
    ("5:36", "SONG5", "Nippius"),
    ("5:16", "SONG6", "Jungrock"),
    ("5:42", "SONG9", "Marbelara"),
    ("6:17", "SONG1", "Sluggion"),
    ("3:45", "SONG13", "Dreempipes"),
    ("3:45", "SONG12", "Pezrock"),
    ("7:22", "SONG10", "Crysilis"),
    ("8:50", "SONG15", "Battleships"),
    ("5:30", "SONGCD0", "Exoticus"),
    ("7:16", "SONGCD1", "Industrius"),
    ("5:22", "SONGCD2", "Muckamok"),
    ("8:00", "SONGCD3", "Raneforus"),
    ("10:20", "SONGCD4", "Stonar"),
    ("5:42", "SONGCD5", "Deckstar"),
    ("3:45", "SONGCD6", "Ceramicus"),
    ("4:50", "SONGCD7", "Deserto"),
    ("13:13", "SECRET", "Deserto - Secret Level"),
    ("4:23", "SONGCD8", "Lagunicus"),
    ("6:14", "XM2", "Holiday Hare '94 Stage 1-2: Holidaius"),
    ("5:06", "XM3", "Holiday Hare '94 Stage 3: Holidaius"),
    ("9:02", "XMAS2", "Holiday Hare '95 Planet 1: Candion"),
    ("5:32", "XMAS3", "Holiday Hare '95 Planet 2: Bloxonius"),
]

def secs(t):
    m, s = t.split(":")
    return int(m) * 60 + int(s)

def duration(path):
    out = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of",
                          "default=nw=1:nk=1", path], capture_output=True, text=True).stdout
    return float(out.strip())

def silences(path):
    """(start, end) of every quiet stretch of at least 0.3 s"""
    r = subprocess.run(["ffmpeg", "-hide_banner", "-nostats", "-i", path, "-vn", "-af",
                        "silencedetect=noise=-45dB:d=0.3", "-f", "null", "-"],
                       capture_output=True, text=True)
    st = [float(x) for x in re.findall(r"silence_start: ([0-9.]+)", r.stderr)]
    en = [float(x) for x in re.findall(r"silence_end: ([0-9.]+)", r.stderr)]
    return list(zip(st, en))

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("video"); ap.add_argument("outdir")
    ap.add_argument("--max", type=float, default=110.0, help="seconds per track at most")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()

    total = duration(a.video)
    planned = sum(secs(t[0]) for t in TRACKS)
    print("video %.1f s, playlist %d s (difference %.1f s)" % (total, planned, total - planned))
    if abs(total - planned) > 60:
        sys.exit("*** the video is not the 37-track playlist in one piece (more than 60 s off)")
    quiet = silences(a.video)
    print("%d quiet stretches found" % len(quiet))

    # cut points: the expected boundary, moved to the middle of the nearest silence (+-4 s)
    starts, t = [0.0], 0.0
    for length, _, _ in TRACKS[:-1]:
        t += secs(length)
        near = [(abs((s + e) / 2 - t), (s + e) / 2) for s, e in quiet if abs((s + e) / 2 - t) <= 4.0]
        starts.append(min(near)[1] if near else t)
    starts.append(total)

    os.makedirs(a.outdir, exist_ok=True)
    disc = 0.0
    for i, (length, name, title) in enumerate(TRACKS):
        s, e = starts[i], starts[i + 1]
        n = min(e - s, a.max)
        disc += n
        print("  %2d %-9s %7.1f s +%6.1f s  %s" % (i + 1, name, s, n, title))
        if a.check:
            continue
        fade = min(3.0, n / 4)
        subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-ss", "%.3f" % s,
                        "-t", "%.3f" % n, "-i", a.video, "-vn",
                        "-af", "afade=t=out:st=%.3f:d=%.3f" % (n - fade, fade),
                        "-ar", "44100", "-ac", "2", "-sample_fmt", "s16",
                        os.path.join(a.outdir, name + ".wav")], check=True)
    print("==> %d tracks, %.1f minutes of CD audio (CD: 80 minutes minus about 7 for the data)"
          % (len(TRACKS), disc / 60))

if __name__ == "__main__":
    main()
