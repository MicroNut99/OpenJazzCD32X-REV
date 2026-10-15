# OpenJazz CD32X – source release

Jazz Jackrabbit (1994, Epic MegaGames) on Sega CD + 32X with a 4 MB RAM cart, built on the
OpenJazz engine.

## What is in here / what is not
- In: the OpenJazz engine with the port's changes (`oj/`, all changes marked `PORT32X`, also as
  `OPENJAZZ_PORT32X.patch`), the port layer (`port/`), the SH2 loader and Sub-CPU program (`cd32x/`,
  OpenLara's, changes marked `OJ32X`), the build and pack tools (`tools/`, `build.sh`), the PC host
  build and regression references (`host/`, `tests/`).
- Not in: any game data. The build needs a copy of Jazz Jackrabbit (CD edition) in
  `JazzJackRabbitGame/`, optionally Holiday Hare '95 in `JAZZXMAS1995/`, and a 320x224 splash
  picture as `Splash.png`. It makes the data packs, sound effects and music tracks from them.

## Build (Linux / WSL, SGDK-style `sh-elf` and `m68k-elf` toolchains under /opt/toolchains/sega)
    tools/make_music_wavs.sh          # the game's music as CD audio WAVs (once)
    ./build.sh                        # packs, engine, loader, Sub-CPU, disc
    LEVEL_PACKS=keep ./build.sh       # later builds when only code changed
Burn `cd32x/subcpu/OpenJazzCD32X.cue` (disc-at-once). Needs real hardware with the 4 MB RAM cart.
Hold A+B+C at power-on to force a full load.

## Documents
- `OPENJAZZ_CD32X_HANDOFF.md` – the project log, round by round.
- `docs/CD32X_ADDENDUM_OPENJAZZ.md` / `.docx` - the addendum to the CD32X boot documents: boot path,
  cart layout, commands, renderer, performance work, features and limitations.

## Licences
- OpenJazz: GPL-2.0 (`oj/COPYING`); bundled libraries: `oj/ext/*/LICENSE`, `oj/doc/licenses.txt`.
- `cd32x/`: from OpenLara's CD32X port and Chilly Willy's Sega CD / 32X framework – check their
  terms before redistributing.
- `port/`, `tools/`: decide and state the licence before publishing (GPL-2.0 matches OpenJazz).
